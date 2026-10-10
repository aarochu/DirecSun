#pragma once

namespace direcsun {

// Wrap an angle in radians onto (-pi, pi], the interval FD section 11.3 defines
// for wrap(). +pi is included and -pi is not, so a half-turn has exactly one
// representation. Non-finite input returns NaN.
float wrap_pi(float angle_rad);

// Wrap an angle in degrees onto (-180, 180], reducing in double first so a
// long-running continuous heading keeps its precision. For display and for the
// wire; control keeps headings continuous (FD section 11.3).
float wrap_deg180(double angle_deg);

}  // namespace direcsun
