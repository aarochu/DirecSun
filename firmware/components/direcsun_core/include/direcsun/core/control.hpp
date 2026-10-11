#pragma once

// The throttle controllers. Rung 1 (R3.7) is the open-loop throttle controller;
// the cascade (R3.8 on) adds the closed wheel loop on top of it.
//
//   SPINUP  throttle ramps from 0 to params.spinup_throttle at
//           kSpinupThrottleRampPerS DShot units per second (open loop, Hall or not)
//   READY   throttle holds params.spinup_throttle (open loop: no rate loop)
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

// Every MANUAL input after R5.2: throttle, wheel and rate. The supervisor still
// offers only throttle while the Hall is bypassed, and wheel and rate need the wheel loop.
constexpr ManualInputSet kAllBuiltInputs = kRungOneBuiltInputs | manual_input_bit(ManualInput::Wheel) |
                                          manual_input_bit(ManualInput::Rate);

// Open-loop spin-up throttle `ticks_in_mode` ticks after entering SPINUP.
int spinup_ramp_throttle(uint32_t ticks_in_mode, int spinup_throttle);

class OpenLoopThrottleController final : public ThrottleController {
public:
    int throttle(const ControlDemand &demand, const TickInputs &in, const Params &params) override;
    void reset() override {}
};

// The flight controller from R3.8 on: rung 1 until the closed wheel loop can
// run (Hall fitted and the T-5 feed-forward table set, wheel_loop_available()),
// then the cascade of FD section 12.1:
//
//   SPINUP  omega_ref ramps from the measured speed toward the bias at
//           kWheelRefRampRpmPerS; feed-forward only below omega_low, the PI
//           closes above it (FD section 13.3). Only the cap clamps the ramp.
//   READY   the rate loop (R3.9) moves omega_ref to hold demand.omega_cmd_dps
//   HOLD    (0 in READY; the attitude loop's output in HOLD and SLEW, R4.3),
//   SLEW    clamped to [omega_low + 100, cap in force] (V-52), and the wheel
//           loop tracks it. A bench `wheel_step` suspends the rate loop for
//           the hold and moves omega_ref by the requested amount (FD 15.3).
//   MANUAL  throttle input: open loop, exactly as rung 1.
//
// Entering the closed loop from open-loop throttle (MANUAL back to READY)
// starts omega_ref at the measured speed and presets the integrator to the
// last throttle, so the hand-over is bumpless.
class CascadeController final : public ThrottleController {
public:
    int throttle(const ControlDemand &demand, const TickInputs &in, const Params &params) override;
    void reset() override;
    // MOMENTUM_SATURATED while the rate loop holds omega_ref on a limit.
    uint32_t flags() const override;

    // For telemetry: omega_ref while the closed wheel loop runs, else 0.
    float wheel_ref_rpm() const { return closed_loop_ ? omega_ref_rpm_ : 0.0f; }
    bool closed_loop() const { return closed_loop_; }
    // For telemetry: the rate loop's torque on the box, internal frame, N m;
    // 0 while the rate loop is not running.
    float tau_cmd_nm() const { return rate_active_ ? tau_cmd_nm_ : 0.0f; }
    bool rate_active() const { return rate_active_; }

private:
    void stop_rate_loop();

    OpenLoopThrottleController open_loop_;
    WheelSpeedPi wheel_pi_;
    RateLoop rate_;
    bool closed_loop_ = false;   // the wheel loop ran on the last tick
    bool rate_active_ = false;   // the rate loop ran on the last tick
    Mode last_mode_ = Mode::Safe;
    float omega_ref_rpm_ = 0.0f;
    float tau_cmd_nm_ = 0.0f;
    bool momentum_saturated_ = false;
};

}  // namespace direcsun
