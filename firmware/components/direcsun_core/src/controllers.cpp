#include "direcsun/core/controllers.hpp"

#include <cmath>

// Mutation-test switches (host selftest builds only), each caught by one test:
//   NO_WHEEL_ANTIWINDUP  the wheel PI integrates while the throttle is clamped
//   RATE_SIGN_FLIP       the torque-to-wheel step uses -s_w (TD R-1)
//   NO_RATE_ANTIWINDUP   the rate integral runs while saturated
//   NO_RATE_BLEED        no stick-slip bleed
//   FLOOR_AT_LOW         omega_ref's floor back on omega_low, the UNDERSPEED threshold
#if defined(ESP_PLATFORM) && (defined(DIRECSUN_MUTANT_NO_WHEEL_ANTIWINDUP) || defined(DIRECSUN_MUTANT_RATE_SIGN_FLIP) || \
                              defined(DIRECSUN_MUTANT_NO_RATE_ANTIWINDUP) || defined(DIRECSUN_MUTANT_NO_RATE_BLEED) ||   \
                              defined(DIRECSUN_MUTANT_FLOOR_AT_LOW))
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kDegToRad = kTwoPi / 360.0f;
constexpr float kRadPerSToRpm = 60.0f / kTwoPi;

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

bool wheel_loop_available(const Params &params)
{
    return params.hall_installed && params.ff_count == kFfPoints;
}

float omega_ref_floor_rpm(const Params &params)
{
#ifdef DIRECSUN_MUTANT_FLOOR_AT_LOW
    return params.omega_low_rpm;
#else
    return params.omega_low_rpm + kOmegaRefFloorMarginRpm;
#endif
}

float ff_throttle(const Params &params, float rpm)
{
    if (params.ff_count != kFfPoints) return 0.0f;
    const float *x = params.ff_rpm;
    const float *y = params.ff_throttle;
    if (!(rpm > x[0])) return y[0];
    if (!(rpm < x[kFfPoints - 1])) return y[kFfPoints - 1];
    for (std::size_t i = 1; i < kFfPoints; ++i) {
        if (rpm <= x[i]) {
            const float f = (rpm - x[i - 1]) / (x[i] - x[i - 1]);
            return y[i - 1] + f * (y[i] - y[i - 1]);
        }
    }
    return y[kFfPoints - 1];
}

WheelPiGains wheel_pi_gains(float slope_rpm_per_unit, float tau_s, float bandwidth_hz)
{
    WheelPiGains g;
    if (!(slope_rpm_per_unit > 0.0f) || !(tau_s > 0.0f) || !(bandwidth_hz > 0.0f)) return g;
    const float w = kTwoPi * bandwidth_hz;
    g.kp = w * tau_s / slope_rpm_per_unit;
    g.ki = w / slope_rpm_per_unit;
    return g;
}

RateLoopStep RateLoop::step(float omega_cmd_dps, float omega_hat_dps, float omega_ref_rpm, float dt_s,
                           const Params &params, float cap_rpm)
{
    RateLoopStep out;
    const float scale = params.rate_gain_scale;
    const float k_omega = params.k_omega * scale;
    const float k_i = params.k_i * scale;
    const float error = (omega_cmd_dps - omega_hat_dps) * kDegToRad;

    const float unclamped = k_omega * error + k_i * integral_;
    out.tau_cmd_nm = clampf(unclamped, -kTauCmdLimitNm, kTauCmdLimitNm);
    out.torque_saturated = unclamped > kTauCmdLimitNm || unclamped < -kTauCmdLimitNm;

    // Torque wanted on the box to a wheel reference (FD section 12.1 step 3):
    // the motor applies -tau to the wheel, so its spin about +z changes at
    // -tau (1/I_w + 1/I_b); s_w maps that onto the forward speed. Getting this
    // sign wrong turns damping into acceleration (TD R-1); HT-6 shows it.
    float s_w = static_cast<float>(params.s_w);
#ifdef DIRECSUN_MUTANT_RATE_SIGN_FLIP
    s_w = -s_w;
#endif
    const float delta_rpm =
        -s_w * out.tau_cmd_nm * (1.0f / kWheelInertiaKgM2 + 1.0f / kBoxInertiaKgM2) * dt_s * kRadPerSToRpm;
    float ref = omega_ref_rpm + delta_rpm;
    if (ref > cap_rpm) {
        ref = cap_rpm;
        out.momentum_saturated = delta_rpm > 0.0f;
    }
    const float floor_rpm = omega_ref_floor_rpm(params);
    if (ref < floor_rpm) {
        ref = floor_rpm;
        out.momentum_saturated = delta_rpm < 0.0f;
    }
    out.omega_ref_rpm = ref;

    // Conditional integration: stop while pushing into a limit (FD 12.3).
    const bool pushing = (out.torque_saturated || out.momentum_saturated) && error * out.tau_cmd_nm > 0.0f;
#ifdef DIRECSUN_MUTANT_NO_RATE_ANTIWINDUP
    (void)pushing;
    integral_ += error * dt_s;
#else
    if (!pushing) integral_ += error * dt_s;
#endif
    if (k_i > 0.0f) {
        const float bound = kRateIntegralLimitNm / k_i;
        integral_ = clampf(integral_, -bound, bound);
    } else {
        integral_ = 0.0f;  // no integral gain: nothing to hold
    }

#ifndef DIRECSUN_MUTANT_NO_RATE_BLEED
    // Stick-slip bleed [D]: a still box with nothing commanded needs no torque.
    if (omega_cmd_dps == 0.0f && std::fabs(omega_hat_dps) < kRateStillDps) {
        integral_ *= std::exp(-dt_s / kRateBleedTauS);
    }
#endif
    return out;
}

float WheelSpeedPi::step(float omega_ref_rpm, float omega_rpm, float dt_s, const Params &params)
{
    const float ff = ff_throttle(params, omega_ref_rpm);
    closed_ = omega_rpm >= params.omega_low_rpm;
    if (!closed_) {
        integrator_ = 0.0f;
        return clampf(ff, kWheelThrottleMin, kWheelThrottleMax);
    }

    const float error = omega_ref_rpm - omega_rpm;
    const float unclamped = ff + params.wheel_kp * error + integrator_;
    const float out = clampf(unclamped, kWheelThrottleMin, kWheelThrottleMax);

    const bool pushing_high = unclamped > kWheelThrottleMax && error > 0.0f;
    const bool pushing_low = unclamped < kWheelThrottleMin && error < 0.0f;
#ifdef DIRECSUN_MUTANT_NO_WHEEL_ANTIWINDUP
    (void)pushing_high;
    (void)pushing_low;
    integrator_ += params.wheel_ki * error * dt_s;
#else
    if (!pushing_high && !pushing_low) integrator_ += params.wheel_ki * error * dt_s;
#endif
    return out;
}

}  // namespace direcsun
