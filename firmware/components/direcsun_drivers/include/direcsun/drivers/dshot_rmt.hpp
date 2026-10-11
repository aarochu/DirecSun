#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "driver/rmt_types.h"
#include "esp_err.h"
#include "soc/gpio_num.h"

// DShot output over one RMT TX channel (FD section 10.6).
//
// Flight code sends exactly one frame per 2 ms control tick with send(). The
// only values it may send are 0 (stop) and 48 to 2047 (throttle); ESC
// commands 1 to 47 are never emitted (FD section 21 rule 1). A command value
// is a programming error: it trips an assert, and is sent as 0 if assertions
// are compiled out.

namespace direcsun::drivers {

enum class DshotRate : uint32_t {
    kDshot150 = 150,
    kDshot300 = 300,  // the default: the bench ESCs misread DShot150 (FD section 16, V-68)
};

// RMT channel tick rate. 25 ns resolves both rates' bit timing to the tick.
inline constexpr uint32_t kDshotRmtResolutionHz = 40'000'000;

class DshotRmt {
public:
    DshotRmt() = default;
    ~DshotRmt() { deinit(); }
    DshotRmt(const DshotRmt&) = delete;
    DshotRmt& operator=(const DshotRmt&) = delete;

    // Creates and enables the TX channel and its bytes encoder on gpio.
    esp_err_t init(gpio_num_t gpio, DshotRate rate);

    // Waits up to 10 ms for queued frames, then deletes the encoder and the
    // channel, which releases the GPIO. Safe to call when not initialised.
    esp_err_t deinit();

    // Queues one frame without blocking. value11 must be 0 or 48 to 2047.
    // ESP_ERR_INVALID_STATE before init. A full queue drops the frame and
    // returns the RMT error; dropped() counts it.
    esp_err_t send(uint16_t value11);

    uint32_t sent() const { return sent_.load(std::memory_order_relaxed); }
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    DshotRate rate() const { return rate_; }
    bool initialised() const { return channel_ != nullptr; }

#ifdef DIRECSUN_DSHOT_ALLOW_RAW
    // Test-only, compiled only into apps that define DIRECSUN_DSHOT_ALLOW_RAW
    // (firmware/test_apps/dshot_loopback). Sends a raw 16-bit frame, but only
    // if its CRC is wrong or it is the all-zero frame, so even a test app
    // cannot emit a valid ESC command or throttle through this path.
    esp_err_t send_raw_frame_for_test(uint16_t frame16);
#endif

private:
    esp_err_t transmit_frame(uint16_t frame16);

    rmt_channel_handle_t channel_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
    DshotRate rate_ = DshotRate::kDshot150;
    // rmt_transmit() reads its payload until the frame has gone out, so each
    // queued frame gets its own buffer. At one frame per 2 ms and about
    // 0.1 ms per frame, a ring of 4 is never reused while still in flight.
    std::array<std::array<uint8_t, 2>, 4> payload_{};
    std::atomic<uint32_t> next_payload_{0};
    std::atomic<uint32_t> sent_{0};
    std::atomic<uint32_t> dropped_{0};
};

// Boot-time DShot 0 stream (FD section 8): from the first milliseconds after
// boot until the control task takes over the output, an esp_timer sends
// DShot 0 on `output` every 2 ms. The ESC arms only after seeing zeros, and a
// reset caused by the watchdog then stops drive within milliseconds.
esp_err_t dshot_zero_sender_start(DshotRmt& output);

// Hands the output over to the caller. Returns only when no zero-sender
// callback is still in flight, so the caller's next send() cannot interleave
// with one. The caller must send its first frame within 2 ms.
void dshot_zero_sender_stop();

}  // namespace direcsun::drivers
