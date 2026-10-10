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

// Rate loop constants (FD sections 12.1, 12.2, 12.3).
constexpr float kTauCmdLimitNm = 0.031f;        // 3 A x 10.38 mN m/A
constexpr float kRateIntegralLimitNm = 0.003f;  // K_i int(e) bound: the planned friction range, 1 to 3 mN m
// Plant inertias in the torque-to-wheel step (TD section 3.2). I_b is the
// calculated box [C on A] until T-3 measures it; I_w is the baseline washer
// wheel [A] until Set 2's printed wheel is weighed (R2.3).
constexpr float kBoxInertiaKgM2 = 1.03e-3f;
constexpr float kWheelInertiaKgM2 = 6.50e-5f;
// Stick-slip bleed [D, R3.9]: with a zero rate command and the box still,
// the integral decays with this time constant. Without it, an integral torque
// left below the bearing's breakaway keeps ramping omega_ref while the box
// sits still (about 570 rpm over the 15 s after a 1 rev/s detumble at 1 mN m
// in the host simulation, test_closed_loop.cpp).
constexpr float kRateStillDps = 1.0f;
constexpr float kRateBleedTauS = 0.5f;

struct RateLoopStep {
    float omega_ref_rpm = 0.0f;       // the new wheel reference, inside [omega_low, cap]
    float tau_cmd_nm = 0.0f;          // torque wanted on the box, internal frame
    bool torque_saturated = false;    // tau_cmd at its limit
    bool momentum_saturated = false;  // omega_ref on a limit in the direction the loop needs
};

// The rate loop (FD section 12.1 steps 2 and 3). omega_cmd_dps is its only
// command input: 0 in READY and DETUMBLE, the attitude loop's output in HOLD
// and SLEW (Set 4). Gains are params.k_omega and params.k_i, both scaled by
// params.rate_gain_scale. The integral runs conditionally: it stops while
// tau_cmd is saturated, or omega_ref is clamped, in the direction of the error
// (FD section 12.3), its torque is bounded by kRateIntegralLimitNm (FD section
// 12.2), and it bleeds away while the box is still with a zero command [D].
class RateLoop {
public:
    RateLoopStep step(float omega_cmd_dps, float omega_hat_dps, float omega_ref_rpm, float dt_s,
                      const Params &params, float cap_rpm);
    void reset() { integral_ = 0.0f; }
    float integral() const { return integral_; }

private:
    float integral_ = 0.0f;  // rad: the integral of the rate error
};

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
