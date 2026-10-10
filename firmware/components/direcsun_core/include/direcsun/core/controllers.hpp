#pragma once

// Control laws of the cascade (FD section 12), as pure functions and small
// state objects. The CascadeController (control.hpp) wires them to the
// supervisor's modes; the supervisor clamps whatever they propose and the
// interlocks override it.
//
//   wheel loop (R3.8)  throttle = FF(omega_ref) + PI(omega_ref - speed_fast)
//
// Everything here is in the firmware's internal frame (FD section 2.3); the
// wire frame exists only in protocol.cpp.

#include <cstdint>

#include "direcsun/core/params.hpp"

namespace direcsun {

// Wheel loop constants (FD sections 12.2, 13.2, 13.3).
constexpr float kWheelRefRampRpmPerS = 400.0f;  // SPINUP omega_ref slope
constexpr float kWheelPiBandwidthHz = 2.0f;     // starting bandwidth, before measuring the delay
constexpr float kWheelThrottleMin = 48.0f;      // running throttle range while the loop is closed
constexpr float kWheelThrottleMax = 2047.0f;
constexpr float kWheelStepMaxRpm = 500.0f;      // bench `wheel_step` limit (FD section 15.3)

// True when the closed wheel loop can run: the Hall sensor is fitted and the
// T-5 feed-forward table has been set. Until then the box runs rung 1, the
// open-loop throttle controller [D].
bool wheel_loop_available(const Params &params);

// Steady-state throttle for `rpm`: linear interpolation on params.ff_table,
// held at the first and last points outside the table. Returns 0 when the
// table is not set (ff_count != kFfPoints).
float ff_throttle(const Params &params, float rpm);

// PI gains for a first-order wheel plant, steady speed = slope * throttle with
// time constant tau_s: the zero cancels the plant pole (Ti = tau_s), so the
// open loop is a pure integrator crossing at bandwidth_hz.
//   kp = 2 pi f tau / slope   (throttle units per rpm)
//   ki = 2 pi f / slope       (throttle units per rpm per second)
// T-5 measures slope and tau; this is how wheel_kp and wheel_ki are derived.
struct WheelPiGains {
    float kp = 0.0f;
    float ki = 0.0f;
};
WheelPiGains wheel_pi_gains(float slope_rpm_per_unit, float tau_s, float bandwidth_hz);

// The wheel-speed PI with feed-forward (FD section 12.1 step 4, 12.3).
//
// Below omega_low the PI is off: the throttle is the feed-forward alone and the
// integrator is cleared (sensorless commutation is not trusted there, FD 13.3).
// Above it the integrator runs conditionally: it stops while the throttle sits
// on a clamp in the direction of the error, so a long saturated step cannot
// wind it up.
class WheelSpeedPi {
public:
    // Throttle request in [48, 2047] for this tick.
    float step(float omega_ref_rpm, float omega_rpm, float dt_s, const Params &params);

    // Clears the integrator, or presets it for a bumpless start.
    void reset(float integrator = 0.0f) { integrator_ = integrator; }

    float integrator() const { return integrator_; }
    bool closed() const { return closed_; }

private:
    float integrator_ = 0.0f;  // throttle units
    bool closed_ = false;      // PI active on the last step
};

}  // namespace direcsun
