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

}  // namespace direcsun
