#include "direcsun/core/control.hpp"

#include "direcsun/core/timebase.hpp"

// Mutation-test switch (host selftest builds only): SPINUP steps straight to
// spinup_throttle instead of ramping, so the ramp test must notice.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_NO_SPINUP_RAMP)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

int spinup_ramp_throttle(uint32_t ticks_in_mode, int spinup_throttle)
{
    if (spinup_throttle <= 0) return 0;
#ifdef DIRECSUN_MUTANT_NO_SPINUP_RAMP
    (void)ticks_in_mode;
    return spinup_throttle;
#endif
    // 64-bit so a long-stalled SPINUP cannot overflow the product.
    const uint64_t ramp = static_cast<uint64_t>(ticks_in_mode) * static_cast<uint64_t>(kSpinupThrottleRampPerS) / kTickHz;
    return ramp >= static_cast<uint64_t>(spinup_throttle) ? spinup_throttle : static_cast<int>(ramp);
}

int OpenLoopThrottleController::throttle(const ControlDemand &demand, const TickInputs &, const Params &params)
{
    switch (demand.mode) {
    case Mode::Spinup: return spinup_ramp_throttle(demand.ticks_in_mode, params.spinup_throttle);
    case Mode::Ready: return params.spinup_throttle;
    case Mode::Manual:
        // Only the throttle input is built at rung 1; the supervisor refuses the
        // others. Hold the last throttle if one is ever demanded anyway.
        return demand.manual_input == ManualInput::Throttle ? demand.manual_throttle : demand.previous_throttle;
    default: return 0;
    }
}

}  // namespace direcsun
