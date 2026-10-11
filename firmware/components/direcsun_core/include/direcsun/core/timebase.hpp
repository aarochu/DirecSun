#pragma once

#include <cstdint>

namespace direcsun {

// The control loop runs at 500 Hz (FD section 7.2): one tick is 2 ms. Every
// duration in the supervisor is counted in ticks so a test can step it exactly.
constexpr uint32_t kTickHz = 500;
constexpr uint32_t kTickMs = 1000 / kTickHz;

constexpr uint32_t ticks_from_ms(uint32_t ms)
{
    return ms / kTickMs;
}

// Ticks elapsed from `since` to `now`, correct across uint32 wrap-around.
constexpr uint32_t ticks_since(uint32_t now, uint32_t since)
{
    return now - since;
}

}  // namespace direcsun
