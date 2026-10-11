#include "direcsun/drivers/dshot_rmt.hpp"

#include <cassert>
#include <cinttypes>

#include "direcsun/core/dshot.hpp"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace direcsun::drivers {

namespace {

constexpr const char* TAG = "dshot";

// One frame is 16 symbols plus the end marker. 48 words is the S3's whole
// per-channel RMT memory and the smallest legal block.
constexpr size_t kMemBlockSymbols = 48;
// One frame in flight and one waiting is all a 2 ms tick can produce.
constexpr size_t kTransQueueDepth = 2;

rmt_symbol_word_t symbol(uint32_t high_ticks, uint32_t low_ticks)
{
    rmt_symbol_word_t s{};
    s.level0 = 1;
    s.duration0 = high_ticks;
    s.level1 = 0;
    s.duration1 = low_ticks;
    return s;
}

}  // namespace

esp_err_t DshotRmt::init(gpio_num_t gpio, DshotRate rate)
{
    ESP_RETURN_ON_FALSE(channel_ == nullptr, ESP_ERR_INVALID_STATE, TAG, "already initialised");
    rate_ = rate;

    rmt_tx_channel_config_t chan_cfg{};
    chan_cfg.gpio_num = gpio;
    chan_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    chan_cfg.resolution_hz = kDshotRmtResolutionHz;
    chan_cfg.mem_block_symbols = kMemBlockSymbols;
    chan_cfg.trans_queue_depth = kTransQueueDepth;
    chan_cfg.flags.init_level = 0;  // DShot idles low
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chan_cfg, &channel_), TAG, "rmt_new_tx_channel");

    const DshotTiming t = dshot_timing(static_cast<uint32_t>(rate), kDshotRmtResolutionHz);
    rmt_bytes_encoder_config_t enc_cfg{};
    enc_cfg.bit0 = symbol(t.zero_high_ticks, t.bit_ticks - t.zero_high_ticks);
    enc_cfg.bit1 = symbol(t.one_high_ticks, t.bit_ticks - t.one_high_ticks);
    enc_cfg.flags.msb_first = 1;
    esp_err_t err = rmt_new_bytes_encoder(&enc_cfg, &encoder_);
    if (err == ESP_OK) {
        err = rmt_enable(channel_);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        if (encoder_ != nullptr) {
            rmt_del_encoder(encoder_);
            encoder_ = nullptr;
        }
        rmt_del_channel(channel_);
        channel_ = nullptr;
        return err;
    }
    ESP_LOGI(TAG, "GPIO%d DShot%" PRIu32 ": bit %" PRIu32 " ticks, '1' high %" PRIu32
             ", '0' high %" PRIu32 " at %" PRIu32 " Hz",
             static_cast<int>(gpio), static_cast<uint32_t>(rate), t.bit_ticks, t.one_high_ticks,
             t.zero_high_ticks, kDshotRmtResolutionHz);
    return ESP_OK;
}

esp_err_t DshotRmt::deinit()
{
    if (channel_ == nullptr) {
        return ESP_OK;
    }
    rmt_tx_wait_all_done(channel_, 10);
    rmt_disable(channel_);
    if (encoder_ != nullptr) {
        rmt_del_encoder(encoder_);
        encoder_ = nullptr;
    }
    const esp_err_t err = rmt_del_channel(channel_);
    channel_ = nullptr;
    return err;
}

esp_err_t DshotRmt::send(uint16_t value11)
{
    if (!dshot_value_is_sendable(value11)) {
        // A command value here is a bug in the caller. Stop in testing; never
        // emit it in a build without assertions.
        assert(!"DShot value 1 to 47 or above 2047 requested");
        value11 = 0;
    }
    return transmit_frame(dshot_frame_for_value(value11));
}

#ifdef DIRECSUN_DSHOT_ALLOW_RAW
esp_err_t DshotRmt::send_raw_frame_for_test(uint16_t frame16)
{
    const bool crc_ok = dshot_check_frame(frame16, nullptr);
    ESP_RETURN_ON_FALSE(frame16 == 0 || !crc_ok, ESP_ERR_INVALID_ARG, TAG,
                        "raw test path only sends frames with a bad CRC, or zero");
    return transmit_frame(frame16);
}
#endif

esp_err_t DshotRmt::transmit_frame(uint16_t frame16)
{
    if (channel_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t slot = next_payload_.fetch_add(1, std::memory_order_relaxed) % payload_.size();
    std::array<uint8_t, 2>& buf = payload_[slot];
    buf[0] = static_cast<uint8_t>(frame16 >> 8);  // MSB first on the wire
    buf[1] = static_cast<uint8_t>(frame16 & 0xFFu);

    rmt_transmit_config_t tx_cfg{};
    tx_cfg.loop_count = 0;
    tx_cfg.flags.eot_level = 0;
    tx_cfg.flags.queue_nonblocking = 1;  // the control loop never waits here
    const esp_err_t err = rmt_transmit(channel_, encoder_, buf.data(), buf.size(), &tx_cfg);
    if (err == ESP_OK) {
        sent_.fetch_add(1, std::memory_order_relaxed);
    } else {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    return err;
}

// ---------------------------------------------------------------------------
// Boot-time zero sender
// ---------------------------------------------------------------------------

namespace {

constexpr uint64_t kZeroSenderPeriodUs = 2000;  // one frame per control tick

esp_timer_handle_t s_zero_timer = nullptr;
DshotRmt* s_zero_output = nullptr;
std::atomic<bool> s_handed_over{false};
std::atomic<int> s_in_callback{0};

void zero_sender_callback(void*)
{
    // Count first, then check the flag: dshot_zero_sender_stop() sets the flag
    // first, then waits for this count to reach zero, so no frame from here
    // can follow a frame from the new owner.
    s_in_callback.fetch_add(1);
    if (!s_handed_over.load() && s_zero_output != nullptr) {
        s_zero_output->send(0);
    }
    s_in_callback.fetch_sub(1);
}

}  // namespace

esp_err_t dshot_zero_sender_start(DshotRmt& output)
{
    ESP_RETURN_ON_FALSE(output.initialised(), ESP_ERR_INVALID_STATE, TAG, "output not initialised");
    ESP_RETURN_ON_FALSE(s_zero_timer == nullptr, ESP_ERR_INVALID_STATE, TAG, "zero sender already running");
    s_zero_output = &output;
    s_handed_over.store(false);

    esp_timer_create_args_t args{};
    args.callback = zero_sender_callback;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "dshot_zero";
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_zero_timer), TAG, "esp_timer_create");
    // First frame now, not 2 ms from now.
    zero_sender_callback(nullptr);
    const esp_err_t err = esp_timer_start_periodic(s_zero_timer, kZeroSenderPeriodUs);
    if (err != ESP_OK) {
        esp_timer_delete(s_zero_timer);
        s_zero_timer = nullptr;
    }
    return err;
}

void dshot_zero_sender_stop()
{
    s_handed_over.store(true);
    if (s_zero_timer != nullptr) {
        esp_timer_stop(s_zero_timer);
        // A callback that started before the flag was set may still be
        // sending; it finishes within one RMT queue call.
        while (s_in_callback.load() != 0) {
        }
        esp_timer_delete(s_zero_timer);
        s_zero_timer = nullptr;
    }
}

}  // namespace direcsun::drivers
