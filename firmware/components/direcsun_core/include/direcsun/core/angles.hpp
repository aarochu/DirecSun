#pragma once

namespace direcsun {

// Wrap an angle in radians onto (-pi, pi], the interval FD section 11.3 defines
// for wrap(). +pi is included and -pi is not, so a half-turn has exactly one
// representation. Non-finite input returns NaN.
float wrap_pi(float angle_rad);

}  // namespace direcsun
