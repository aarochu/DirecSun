#pragma once

// The throttle controllers. Rung 1 (R3.7) is the open-loop throttle controller;
// the cascade (R3.8 on) adds the closed wheel loop on top of it.
//
//   SPINUP  throttle ramps from 0 to params.spinup_throttle at
//           kSpinupThrottleRampPerS DShot units per second (open loop, Hall or not)
//   READY   throttle holds params.spinup_throttle (the rate loop of R3.9
//           replaces this hold)
//   MANUAL  throttle input: the supervisor's clamped, staleness-held setpoint
//
// The controller only proposes a throttle. The supervisor clamps it (0..2047,
// and kBypassThrottleMax while the Hall is bypassed) and the interlocks
// override it, so nothing here can bypass a safety limit.

#include "direcsun/core/commands.hpp"
#include "direcsun/core/controllers.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"

namespace direcsun {

// The MANUAL inputs whose loops exist after R3.7: open-loop throttle only.
constexpr ManualInputSet kRungOneBuiltInputs = manual_input_bit(ManualInput::Throttle);

// Open-loop spin-up throttle `ticks_in_mode` ticks after entering SPINUP.
int spinup_ramp_throttle(uint32_t ticks_in_mode, int spinup_throttle);

class OpenLoopThrottleController final : public ThrottleController {
public:
    int throttle(const ControlDemand &demand, const TickInputs &in, const Params &params) override;
    void reset() override {}
};

// The flight controller from R3.8 on: rung 1 until the closed wheel loop can
// run (Hall fitted and the T-5 feed-forward table set, wheel_loop_available()),
// then the wheel-speed loop:
//
//   SPINUP  omega_ref ramps from the measured speed toward the bias at
//           kWheelRefRampRpmPerS; feed-forward only below omega_low, the PI
//           closes above it (FD section 13.3). Only the cap clamps the ramp.
//   READY   omega_ref holds, clamped to [omega_low, cap in force]. A bench
//           `wheel_step` moves it by the requested amount (FD section 15.3).
//   MANUAL  throttle input: open loop, exactly as rung 1.
//
// Entering the closed loop from open-loop throttle (MANUAL back to READY)
// starts omega_ref at the measured speed and presets the integrator to the
// last throttle, so the hand-over is bumpless.
class CascadeController final : public ThrottleController {
public:
    int throttle(const ControlDemand &demand, const TickInputs &in, const Params &params) override;
    void reset() override;

    // For telemetry: omega_ref while the closed wheel loop runs, else 0.
    float wheel_ref_rpm() const { return closed_loop_ ? omega_ref_rpm_ : 0.0f; }
    bool closed_loop() const { return closed_loop_; }

private:
    OpenLoopThrottleController open_loop_;
    WheelSpeedPi wheel_pi_;
    bool closed_loop_ = false;   // the wheel loop ran on the last tick
    Mode last_mode_ = Mode::Safe;
    float omega_ref_rpm_ = 0.0f;
};

}  // namespace direcsun
