#include "direcsun/core/sun.hpp"

#include <cmath>

namespace direcsun {

SunReading evaluate_sun(float a_mv, float b_mv, const Params &params)
{
    SunReading r;
    if (!std::isfinite(a_mv) || !std::isfinite(b_mv) || a_mv < 0.0f || b_mv < 0.0f) return r;
    r.sum_mv = a_mv + b_mv;
    if (!(r.sum_mv > 0.0f) || !(r.sum_mv > params.light_threshold_mv)) return r;
    r.d = (a_mv - b_mv) / r.sum_mv;

    if (params.sun_cal_count < 2) {
        r.angle_deg = kSunIdentityDegPerD * r.d;
        r.valid = true;
        return r;
    }
    const std::size_t n = params.sun_cal_count;
    // Outside the calibrated range the table says nothing: invalid, not extrapolated [V] TD 6.4.
    if (r.d < params.sun_cal_d[0] || r.d > params.sun_cal_d[n - 1]) return r;
    for (std::size_t i = 1; i < n; ++i) {
        if (r.d <= params.sun_cal_d[i]) {
            const float span = params.sun_cal_d[i] - params.sun_cal_d[i - 1];
            const float f = (r.d - params.sun_cal_d[i - 1]) / span;
            r.angle_deg = params.sun_cal_deg[i - 1] + f * (params.sun_cal_deg[i] - params.sun_cal_deg[i - 1]);
            r.valid = true;
            return r;
        }
    }
    return r;
}

}  // namespace direcsun
