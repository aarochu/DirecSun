#pragma once

// Sun angle from the two OP550 photodiodes (FD section 11.4, R5.4):
//   d = (A - B) / (A + B), valid only when A + B > light_threshold,
//   angle = the T-10 piecewise-linear table of d.
// Pure arithmetic, host-tested. Angles are internal (counter-clockwise
// positive, seen from above) like everything in the core; the wire frame is
// clockwise positive and the protocol layer converts, including the table.

#include "direcsun/core/params.hpp"

namespace direcsun {

// Identity table used while `sun_cal_count` is 0: d of -1 to +1 maps to 90 to -90
// deg, i.e. the wire reads +90 clockwise when sensor A alone is lit [D].
constexpr float kSunIdentityDegPerD = -90.0f;

struct SunReading {
    bool valid = false;
    float d = 0.0f;           // the normalised difference, meaningful when sum_mv > 0
    float sum_mv = 0.0f;
    float angle_deg = 0.0f;   // internal frame; only meaningful when valid
};

SunReading evaluate_sun(float a_mv, float b_mv, const Params &params);

}  // namespace direcsun
