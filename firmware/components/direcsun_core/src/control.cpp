#include "direcsun/core/control.hpp"

#include <cmath>

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

namespace {

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int to_throttle(float request)
{
    return static_cast<int>(std::lround(clampf(request, 0.0f, kWheelThrottleMax)));
}

}  // namespace

void CascadeController::reset()
{
    closed_loop_ = false;
    last_mode_ = Mode::Safe;
    omega_ref_rpm_ = 0.0f;
    wheel_pi_.reset();
}

int CascadeController::throttle(const ControlDemand &demand, const TickInputs &in, const Params &params)
{
    const Mode previous_mode = last_mode_;
    last_mode_ = demand.mode;

    // Rung 1 whenever the closed loop cannot run: no Hall (or the bench
    // bypass), or no T-5 feed-forward table yet; and MANUAL throttle always.
    const bool manual_throttle = demand.mode == Mode::Manual && demand.manual_input == ManualInput::Throttle;
    if (!wheel_loop_available(params) || demand.spinup_open_loop || manual_throttle ||
        (demand.mode != Mode::Spinup && demand.mode != Mode::Ready)) {
        closed_loop_ = false;
        wheel_pi_.reset();
        return open_loop_.throttle(demand, in, params);
    }

    constexpr float dt = 1.0f / static_cast<float>(kTickHz);
    const float cap = effective_cap_rpm(params);

    if (demand.mode == Mode::Spinup) {
        if (!closed_loop_ || previous_mode != Mode::Spinup) {
            omega_ref_rpm_ = in.wheel_rpm_safe > 0.0f ? in.wheel_rpm_safe : 0.0f;
            wheel_pi_.reset();
        }
        const float target = demand.omega_target_rpm < cap ? demand.omega_target_rpm : cap;
        if (omega_ref_rpm_ < target) {
            omega_ref_rpm_ += kWheelRefRampRpmPerS * dt;
            if (omega_ref_rpm_ > target) omega_ref_rpm_ = target;
        } else {
            omega_ref_rpm_ = target;
        }
    } else {  // READY
        if (!closed_loop_) {
            // Bumpless start from open-loop throttle: reference at the measured
            // speed, integrator at the last throttle minus the feed-forward.
            omega_ref_rpm_ = clampf(in.wheel_rpm_safe, params.omega_low_rpm, cap);
            wheel_pi_.reset(static_cast<float>(demand.previous_throttle) - ff_throttle(params, omega_ref_rpm_));
        }
        omega_ref_rpm_ += demand.wheel_step_rpm;
        omega_ref_rpm_ = clampf(omega_ref_rpm_, params.omega_low_rpm, cap);
    }

    closed_loop_ = true;
    return to_throttle(wheel_pi_.step(omega_ref_rpm_, in.wheel_rpm_fast, dt, params));
}

}  // namespace direcsun
