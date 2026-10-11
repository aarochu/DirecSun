#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace direcsun::drivers {

// A1104 Hall switch edge capture (FD sections 7.4 and 10.4).
//
// The A1104 output is open drain: it pulls low while a south pole is in front
// of it, so each magnet pass is one falling edge. An IRAM interrupt handler
// timestamps every falling edge with esp_timer_get_time() into a ring of
// kHallRingSize entries and publishes an atomic write count. Only one task (the
// control task) may call hall_drain(); it hands the timestamps to
// direcsun::WheelSpeed.
//
// Wiring (TD section 5.3): A1104 on 5 V, output to the XIAO's D10 (GPIO9) with
// an external 12 kOhm pull-up to 3.3 V. The internal pull-up is enabled as well.
//
// The GPIO interrupt is allocated on the core that calls hall_init(). Call it
// from the control task's core (core 1, FD section 7.2), so a Wi-Fi burst on
// core 0 cannot delay the timestamp.

constexpr gpio_num_t kHallGpio = GPIO_NUM_9;  // XIAO D10
constexpr std::size_t kHallRingSize = 8;      // power of two; more than 50 ticks of edges at the cap

// Configures the pin and installs the edge interrupt. Shares the GPIO ISR
// service if another driver installed it first.
esp_err_t hall_init(gpio_num_t gpio = kHallGpio);

struct HallDrain {
    std::size_t count;  // timestamps written to `out`, oldest first
    uint32_t lost;      // edges dropped because the reader fell more than a ring behind
};

// Copies the edge timestamps captured since the previous call into out[0..count),
// oldest first, and advances the read position past them. If more than
// `capacity` edges are waiting, the rest stay for the next call. Lock-free and
// non-blocking. Single caller only.
HallDrain hall_drain(int64_t *out, std::size_t capacity);

// Falling edges seen by the interrupt since hall_init(), including any that
// were later lost. Compare with WheelSpeed::edge_count() to see glitches.
uint32_t hall_edge_total();

}  // namespace direcsun::drivers
