#include "direcsun/core/angles.hpp"

#include <cmath>

namespace direcsun {

float wrap_pi(float angle_rad)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    // remainder() returns a value in [-pi, pi]; fold -pi onto +pi.
    float wrapped = std::remainder(angle_rad, kTwoPi);
    if (wrapped <= -kPi) {
        wrapped += kTwoPi;
    }
    return wrapped;
}

float wrap_deg180(double angle_deg)
{
    constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
    constexpr double kDegPerRad = 180.0 / 3.14159265358979323846;
    const double reduced = std::remainder(angle_deg, 360.0);
    return static_cast<float>(wrap_pi(static_cast<float>(reduced * kRadPerDeg)) * kDegPerRad);
}

}  // namespace direcsun
