#include "direcsun/core/sun.hpp"

#include <cmath>

// Mutation-test switch (host selftest builds only): the table extrapolates by
// clamping to its end points instead of refusing d outside the calibrated range.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_SUN_EXTRAPOLATE)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

SunReading sun_angle_from_d(float d, const Params &params)
{
    SunReading r;
    r.d = d;
    const std::size_t n = params.sun_cal_count;
    if (n < 2 || !std::isfinite(d)) return r;
    const float *x = params.sun_cal_d;
    const float *y = params.sun_cal_deg;
#ifdef DIRECSUN_MUTANT_SUN_EXTRAPOLATE
    d = d < x[0] ? x[0] : (d > x[n - 1] ? x[n - 1] : d);
#else
    if (d < x[0] || d > x[n - 1]) return r;
#endif
    for (std::size_t i = 1; i < n; ++i) {
        if (d <= x[i]) {
            const float f = (d - x[i - 1]) / (x[i] - x[i - 1]);
            r.angle_deg = y[i - 1] + f * (y[i] - y[i - 1]);
            r.valid = true;
            return r;
        }
    }
    return r;
}

void SunSensor::update(float a_mv, float b_mv, float dt_s, const Params &params)
{
    if (!std::isfinite(a_mv) || !std::isfinite(b_mv)) {
        reading_ = SunReading{};
        return;
    }
    if (a_mv < 0.0f) a_mv = 0.0f;
    if (b_mv < 0.0f) b_mv = 0.0f;
    if (!started_) {
        a_mv_ = a_mv;
        b_mv_ = b_mv;
        started_ = true;
    } else {
        const float w = dt_s / (kTauS + dt_s);
        a_mv_ += w * (a_mv - a_mv_);
        b_mv_ += w * (b_mv - b_mv_);
    }
    const float sum = a_mv_ + b_mv_;
    // A dark pair (both channels near 0) has no direction whatever the threshold.
    if (!(sum > params.light_threshold_mv) || !(sum > 1.0f)) {
        reading_ = SunReading{};
        return;
    }
    reading_ = sun_angle_from_d((a_mv_ - b_mv_) / sum, params);
}

}  // namespace direcsun
