#include "direcsun/core/controllers.hpp"

#include <cmath>

// Mutation-test switch (host selftest builds only): the wheel PI integrates
// even while the throttle is clamped, so the windup test must notice.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_NO_WHEEL_ANTIWINDUP)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

bool wheel_loop_available(const Params &params)
{
    return params.hall_installed && params.ff_count == kFfPoints;
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
