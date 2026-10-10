// DShot bring-up test app (changes/3-teleop/R3.1-dshot-output.md).
//
// Wiring: a jumper from D2 (GPIO3) to D3 (GPIO4). The ESC signal stays on
// D8 (GPIO7) and receives only DShot 0 from this app, so the ESC may stay
// connected: the throttle vectors go out on D2 only.
//
// 1. Starts the boot-time DShot 0 stream on D8 (FD section 8).
// 2. Loopback: sends the four HT-1 vectors on D2 at DShot150 and DShot300,
//    captures them on D3 with an RMT RX channel, decodes the pulse widths,
//    and checks every frame bit for bit. Then sends one frame with a wrong
//    CRC (positive control) and expects the decoder to reject it.
// 3. Keeps streaming DShot 0 on D8 forever for the oscilloscope and the ESC
//    arming tones, printing a status line every 5 s.

#include <array>
#include <cinttypes>
#include <cstdint>
#include <vector>

#include "direcsun/core/dshot.hpp"
#include "direcsun/drivers/dshot_rmt.hpp"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

using direcsun::DshotTiming;
using direcsun::drivers::DshotRate;
using direcsun::drivers::DshotRmt;
using direcsun::drivers::kDshotRmtResolutionHz;

namespace {

constexpr const char* TAG = "dshot_loopback";

constexpr gpio_num_t kEscPin = GPIO_NUM_7;       // D8: ESC signal, zeros only
constexpr gpio_num_t kLoopbackTxPin = GPIO_NUM_3;  // D2: loopback source
constexpr gpio_num_t kLoopbackRxPin = GPIO_NUM_4;  // D3: loopback capture

constexpr size_t kRxSymbols = 64;

struct Vector {
    uint16_t value11;
    uint16_t frame;
};
// FD section 10.6 test vectors.
constexpr std::array<Vector, 4> kVectors{{
    {0, 0x0000},
    {48, 0x0606},
    {1046, 0x82C6},
    {2047, 0xFFEE},
}};
// The 48 vector with its CRC changed from 0x6 to 0x7.
constexpr uint16_t kBadCrcFrame = 0x0607;

QueueHandle_t s_rx_done = nullptr;
std::array<rmt_symbol_word_t, kRxSymbols> s_rx_buf{};

bool on_rx_done(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata, void*)
{
    BaseType_t woken = pdFALSE;
    const size_t n = edata->num_symbols;
    xQueueSendFromISR(s_rx_done, &n, &woken);
    return woken == pdTRUE;
}

struct Capture {
    bool received = false;
    std::vector<uint32_t> highs;  // high-pulse widths in RX ticks, in order
};

Capture capture_one(rmt_channel_handle_t rx, DshotRmt& tx, bool raw, uint16_t value_or_frame)
{
    Capture c;
    xQueueReset(s_rx_done);
    rmt_receive_config_t rcfg{};
    rcfg.signal_range_min_ns = 200;    // glitch filter, far below the 1.25 us shortest pulse
    rcfg.signal_range_max_ns = 30000;  // 30 us of steady low ends the frame
    ESP_ERROR_CHECK(rmt_receive(rx, s_rx_buf.data(), sizeof(s_rx_buf), &rcfg));
    vTaskDelay(pdMS_TO_TICKS(1));

#ifdef DIRECSUN_DSHOT_ALLOW_RAW
    const esp_err_t err = raw ? tx.send_raw_frame_for_test(value_or_frame) : tx.send(value_or_frame);
#else
    const esp_err_t err = tx.send(value_or_frame);
    (void)raw;
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send failed: %s", esp_err_to_name(err));
        return c;
    }

    size_t n = 0;
    if (xQueueReceive(s_rx_done, &n, pdMS_TO_TICKS(50)) != pdTRUE) {
        return c;
    }
    c.received = true;
    for (size_t i = 0; i < n && i < s_rx_buf.size(); ++i) {
        const rmt_symbol_word_t& s = s_rx_buf[i];
        if (s.level0 == 1 && s.duration0 > 0) {
            c.highs.push_back(s.duration0);
        }
        if (s.level1 == 1 && s.duration1 > 0) {
            c.highs.push_back(s.duration1);
        }
    }
    return c;
}

uint32_t ticks_to_ns(uint32_t ticks)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(ticks) * 1'000'000'000ULL) / kDshotRmtResolutionHz);
}

// Runs every vector and the bad-CRC control at one rate. Returns passes.
int run_rate(rmt_channel_handle_t rx, DshotRate rate, int* total)
{
    DshotRmt tx;
    ESP_ERROR_CHECK(tx.init(kLoopbackTxPin, rate));
    const DshotTiming t = direcsun::dshot_timing(static_cast<uint32_t>(rate), kDshotRmtResolutionHz);
    int pass = 0;

    for (const Vector& v : kVectors) {
        ++*total;
        const Capture c = capture_one(rx, tx, false, v.value11);
        if (!c.received) {
            ESP_LOGE(TAG, "FAIL DShot%" PRIu32 " value %u: nothing captured on D3. Is the D2-D3 jumper fitted?",
                     static_cast<uint32_t>(rate), v.value11);
            continue;
        }
        uint16_t frame = 0;
        if (!direcsun::dshot_frame_from_pulses(c.highs.data(), c.highs.size(), t.one_threshold_ticks, &frame)) {
            ESP_LOGE(TAG, "FAIL DShot%" PRIu32 " value %u: %u high pulses captured, expected 16",
                     static_cast<uint32_t>(rate), v.value11, static_cast<unsigned>(c.highs.size()));
            continue;
        }
        uint32_t one_sum = 0, one_n = 0, zero_sum = 0, zero_n = 0;
        for (const uint32_t h : c.highs) {
            if (h >= t.one_threshold_ticks) {
                one_sum += h;
                ++one_n;
            } else {
                zero_sum += h;
                ++zero_n;
            }
        }
        const bool crc_ok = direcsun::dshot_check_frame(frame, nullptr);
        const bool ok = frame == v.frame && crc_ok;
        pass += ok ? 1 : 0;
        ESP_LOGI(TAG, "%s DShot%" PRIu32 " value %4u: sent 0x%04X got 0x%04X crc %s | '1' high %" PRIu32
                 " ns (n=%" PRIu32 "), '0' high %" PRIu32 " ns (n=%" PRIu32 ")",
                 ok ? "PASS" : "FAIL", static_cast<uint32_t>(rate), v.value11, v.frame, frame,
                 crc_ok ? "ok" : "BAD", one_n ? ticks_to_ns(one_sum / one_n) : 0, one_n,
                 zero_n ? ticks_to_ns(zero_sum / zero_n) : 0, zero_n);
    }

#ifdef DIRECSUN_DSHOT_ALLOW_RAW
    // Positive control: a frame with a wrong CRC must be captured intact and
    // then rejected. If the decoder accepted everything, this would say FAIL.
    ++*total;
    const Capture c = capture_one(rx, tx, true, kBadCrcFrame);
    uint16_t frame = 0;
    const bool decoded = c.received &&
        direcsun::dshot_frame_from_pulses(c.highs.data(), c.highs.size(), t.one_threshold_ticks, &frame);
    const bool rejected = decoded && frame == kBadCrcFrame && !direcsun::dshot_check_frame(frame, nullptr);
    pass += rejected ? 1 : 0;
    ESP_LOGI(TAG, "%s DShot%" PRIu32 " control: sent bad-CRC 0x%04X, got 0x%04X, decoder %s",
             rejected ? "PASS" : "FAIL", static_cast<uint32_t>(rate), kBadCrcFrame, frame,
             rejected ? "reported a CRC error" : "did NOT report a CRC error");
#endif

    tx.deinit();
    return pass;
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGW(TAG, "==============================================================");
    ESP_LOGW(TAG, " DShot loopback test app (R3.1)");
    ESP_LOGW(TAG, " ESC pin D8 (GPIO7) receives ONLY DShot 0 from this app.");
    ESP_LOGW(TAG, " Throttle test vectors go out on D2 (GPIO3) only.");
    ESP_LOGW(TAG, " Required wiring: jumper D2 (GPIO3) -> D3 (GPIO4).");
    ESP_LOGW(TAG, " Never connect the ESC signal wire to D2.");
    ESP_LOGW(TAG, "==============================================================");

    // FD section 8: zeros on the ESC pin before anything else.
    static DshotRmt esc;
    ESP_ERROR_CHECK(esc.init(kEscPin, DshotRate::kDshot150));
    ESP_ERROR_CHECK(direcsun::drivers::dshot_zero_sender_start(esc));
    ESP_LOGI(TAG, "D8: DShot150 zero stream started (one frame every 2 ms)");

    s_rx_done = xQueueCreate(1, sizeof(size_t));
    rmt_rx_channel_config_t rx_cfg{};
    rx_cfg.gpio_num = kLoopbackRxPin;
    rx_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    rx_cfg.resolution_hz = kDshotRmtResolutionHz;
    rx_cfg.mem_block_symbols = 48;
    rmt_channel_handle_t rx = nullptr;
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx));
    rmt_rx_event_callbacks_t cbs{};
    cbs.on_recv_done = on_rx_done;
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx, &cbs, nullptr));
    ESP_ERROR_CHECK(rmt_enable(rx));

    int total = 0;
    int pass = run_rate(rx, DshotRate::kDshot150, &total);
    pass += run_rate(rx, DshotRate::kDshot300, &total);
    if (pass == total) {
        ESP_LOGI(TAG, "LOOPBACK RESULT: PASS %d/%d", pass, total);
    } else {
        ESP_LOGE(TAG, "LOOPBACK RESULT: FAIL %d/%d", pass, total);
    }

    ESP_LOGI(TAG, "D8: streaming DShot150 zeros for the oscilloscope and ESC checks");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "D8 status: %" PRIu32 " frames sent, %" PRIu32 " dropped", esc.sent(), esc.dropped());
    }
}
