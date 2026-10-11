#pragma once

#include <cstdint>

// Mutation-test switch (host selftest builds only): the clock counts executed
// ticks instead of alarms, as the control task did before V-66.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_TICKS_NOT_ALARMS)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

// The control loop runs at 500 Hz (FD section 7.2): one tick is 2 ms. Every
// duration in the supervisor is counted in ticks of the alarm clock below (one
// per 2 ms alarm, missed ticks included), so a test can step it exactly.
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

// The supervisor's clock (FD section 9.2, finding V-66). The control task runs
// once per 2 ms timer alarm, but a tick that overruns misses alarms, and its
// next wait returns them all at once. This clock advances by every alarm, not
// by every executed tick, so time in the supervisor (the heartbeat timeout, the
// ramp-down, interlock durations) is wall time that a missed tick cannot
// stretch. It also keeps the run of consecutive missed alarms (CONTROL_STALL).
class TickClock {
public:
    // Called once per executed tick with the alarms taken since the previous
    // call (1 when the task kept up). Returns how many of them were missed.
    uint32_t advance(uint32_t alarms)
    {
        const uint32_t step = alarms > 0 ? alarms : 1;  // an executed tick is at least one alarm
#ifdef DIRECSUN_MUTANT_TICKS_NOT_ALARMS
        now_ += 1;
#else
        now_ += step;
#endif
        const uint32_t missed = step - 1;
        if (missed == 0) {
            missed_streak_ = 0;
        } else {
            missed_streak_ = missed_streak_ + missed < missed_streak_ ? UINT32_MAX : missed_streak_ + missed;
        }
        return missed;
    }
    uint32_t now() const { return now_; }
    uint32_t missed_streak() const { return missed_streak_; }

private:
    uint32_t now_ = 0;
    uint32_t missed_streak_ = 0;
};

}  // namespace direcsun
