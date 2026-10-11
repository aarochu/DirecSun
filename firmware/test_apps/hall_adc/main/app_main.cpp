// R3.3 bring-up: Hall edges and wheel speed, battery and sun ADC.
//
// Every accepted or rejected Hall edge prints one line. Every 200 ms a status
// line prints the battery (instantaneous and 1 s average), both sun channels,
// and the speed estimates with their staleness. The loop runs every 2 ms on
// core 1 like the future control task, and reads the battery every 20 ms (the
// FD's 50 Hz), so the 1 s average has the same window as the interlock's.
//
// Nothing here drives the ESC; it is safe with the motor connected.

#include <cinttypes>

#include "direcsun/core/wheel_speed.hpp"
#include "direcsun/drivers/adc_inputs.hpp"
#include "direcsun/drivers/hall.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "hall_adc";
constexpr TickType_t kTick = pdMS_TO_TICKS(2);
constexpr int kBatteryEveryTicks = 10;  // 50 Hz
constexpr int kStatusEveryTicks = 100;  // 5 Hz

using direcsun::EdgeResult;
using direcsun::WheelSpeed;
namespace drv = direcsun::drivers;

int read_mv(drv::AdcInput input)
{
    int mv = -1;
    const esp_err_t err = drv::adc_read_mv(input, &mv);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc read failed: %s", esp_err_to_name(err));
        return -1;
    }
    return mv;
}

void bringup_task(void * /*arg*/)
{
    // Installed from this task so the GPIO interrupt lands on core 1.
    ESP_ERROR_CHECK(drv::hall_init());
    ESP_ERROR_CHECK(drv::adc_inputs_init());

    WheelSpeed wheel;
    drv::BatteryAverage battery;
    int64_t last_accepted_us = -1;
    int last_battery_mv = -1;
    uint32_t lost_total = 0;
    int tick = 0;

    ESP_LOGI(TAG, "running on core %d: wave a magnet (south pole) past the A1104", xPortGetCoreID());
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&wake, kTick);
        const int64_t now = esp_timer_get_time();

        int64_t edges[drv::kHallRingSize];
        const drv::HallDrain drained = drv::hall_drain(edges, drv::kHallRingSize);
        if (drained.lost > 0) {
            lost_total += drained.lost;
            ESP_LOGW(TAG, "lost %" PRIu32 " edge(s), %" PRIu32 " in total", drained.lost, lost_total);
        }
        for (std::size_t i = 0; i < drained.count; ++i) {
            const int64_t t = edges[i];
            const int64_t since = last_accepted_us < 0 ? 0 : t - last_accepted_us;
            switch (wheel.add_edge(t)) {
            case EdgeResult::kFirst:
                ESP_LOGI(TAG, "edge %" PRIu32 ": first edge", wheel.edge_count());
                last_accepted_us = t;
                break;
            case EdgeResult::kPeriod:
                ESP_LOGI(TAG, "edge %" PRIu32 ": period %" PRId64 " us, fast %.0f rpm, safe %.0f rpm",
                         wheel.edge_count(), since, wheel.speed_fast_rpm(t), wheel.speed_safe_rpm(t));
                last_accepted_us = t;
                break;
            case EdgeResult::kTooShort:
                ESP_LOGW(TAG, "glitch: %" PRId64 " us after the last edge (< 3 ms), ignored, glitches %" PRIu32,
                         since, wheel.glitch_count());
                break;
            case EdgeResult::kTooLong:
                ESP_LOGI(TAG, "edge %" PRIu32 ": %" PRId64 " ms gap (> 200 ms), speed restarts",
                         wheel.edge_count(), since / 1000);
                last_accepted_us = t;
                break;
            }
        }

        if (tick % kBatteryEveryTicks == 0) {
            last_battery_mv = read_mv(drv::AdcInput::kBattery);
            if (last_battery_mv >= 0) {
                battery.add(drv::battery_volts_from_pin_mv(last_battery_mv));
            }
        }

        if (tick % kStatusEveryTicks == 0) {
            const int sun_a = read_mv(drv::AdcInput::kSunA);
            const int sun_b = read_mv(drv::AdcInput::kSunB);
            ESP_LOGI(TAG,
                     "vbat %.2f V (avg %.2f V over %u, pin %d mV) | sun A %d mV, B %d mV | "
                     "hall isr %" PRIu32 ", accepted %" PRIu32 ", glitches %" PRIu32 " | "
                     "speed fast %.0f, safe %.0f rpm%s",
                     drv::battery_volts_from_pin_mv(last_battery_mv), battery.average(),
                     static_cast<unsigned>(battery.count()), last_battery_mv, sun_a, sun_b,
                     drv::hall_edge_total(), wheel.edge_count(), wheel.glitch_count(),
                     wheel.speed_fast_rpm(now), wheel.speed_safe_rpm(now),
                     wheel.stale(now) ? " (stale bound)" : "");
        }
        ++tick;
    }
}

}  // namespace

extern "C" void app_main(void)
{
    xTaskCreatePinnedToCore(bringup_task, "hall_adc", 4096, nullptr, 5, nullptr, 1);
}
