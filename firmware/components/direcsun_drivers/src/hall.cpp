#include "direcsun/drivers/hall.hpp"

#include <algorithm>
#include <atomic>

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace direcsun::drivers {

namespace {

constexpr const char *TAG = "hall";

static_assert((kHallRingSize & (kHallRingSize - 1)) == 0, "ring size must be a power of two");

// Zero-initialised statics live in internal DRAM, so the IRAM handler can touch
// them while the flash cache is disabled.
int64_t s_ring[kHallRingSize];
std::atomic<uint32_t> s_written{0};  // edges published by the ISR; only the ISR writes it
uint32_t s_read = 0;                 // edges consumed; only hall_drain() touches it

void IRAM_ATTR hall_isr(void * /*arg*/)
{
    const uint32_t index = s_written.load(std::memory_order_relaxed);
    s_ring[index % kHallRingSize] = esp_timer_get_time();
    s_written.store(index + 1, std::memory_order_release);
}

}  // namespace

esp_err_t hall_init(gpio_num_t gpio)
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << gpio;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_NEGEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "gpio_config(GPIO%d)", gpio);

    // The service is shared by every GPIO interrupt in the firmware; another
    // driver may have installed it already, which is not an error here.
    const esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service: %s", esp_err_to_name(err));
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(gpio, hall_isr, nullptr), TAG, "gpio_isr_handler_add");
    ESP_LOGI(TAG, "A1104 edge capture on GPIO%d, falling edge, ring of %u", gpio,
             static_cast<unsigned>(kHallRingSize));
    return ESP_OK;
}

HallDrain hall_drain(int64_t *out, std::size_t capacity)
{
    HallDrain result{0, 0};

    // The ISR may be writing slot `written` (not yet published) at any moment, so
    // only kHallRingSize - 1 published entries are guaranteed intact.
    constexpr uint32_t kSafe = kHallRingSize - 1;
    uint32_t written = s_written.load(std::memory_order_acquire);
    if (written - s_read > kSafe) {
        result.lost = written - s_read - kSafe;
        s_read = written - kSafe;
    }

    const std::size_t n = std::min<std::size_t>(written - s_read, capacity);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = s_ring[(s_read + i) % kHallRingSize];
    }

    // Seqlock-style check: if the ISR advanced far enough during the copy to
    // overwrite slots we read, drop those (the oldest) as lost.
    std::atomic_thread_fence(std::memory_order_acquire);
    written = s_written.load(std::memory_order_relaxed);
    std::size_t overwritten = 0;
    if (written - s_read > kSafe) {
        overwritten = std::min<std::size_t>(written - s_read - kSafe, n);
    }
    if (overwritten > 0) {
        std::copy(out + overwritten, out + n, out);
        result.lost += static_cast<uint32_t>(overwritten);
    }

    s_read += static_cast<uint32_t>(n);
    result.count = n - overwritten;
    return result;
}

uint32_t hall_edge_total()
{
    return s_written.load(std::memory_order_relaxed);
}

}  // namespace direcsun::drivers
