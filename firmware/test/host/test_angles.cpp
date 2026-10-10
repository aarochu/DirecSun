#include <cmath>
#include <initializer_list>
#include <limits>

#include "direcsun/core/angles.hpp"
#include "doctest.h"

using direcsun::wrap_pi;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPiD = 6.283185307179586476925;

}  // namespace

TEST_CASE("wrap_pi leaves angles already in (-pi, pi] alone")
{
    CHECK(wrap_pi(0.0f) == 0.0f);
    CHECK(wrap_pi(1.0f) == doctest::Approx(1.0f));
    CHECK(wrap_pi(-1.0f) == doctest::Approx(-1.0f));
    CHECK(wrap_pi(3.0f) == doctest::Approx(3.0f));
}

TEST_CASE("wrap_pi maps both half-turns to +pi")
{
    CHECK(wrap_pi(kPi) == kPi);
    CHECK(wrap_pi(-kPi) == kPi);
}

TEST_CASE("wrap_pi maps odd multiples of pi to a half-turn")
{
    // 3.0f * kPi is not exactly 3 pi in float, so its correct wrap can land a
    // rounding step on either side of the +pi/-pi seam. What must hold is that
    // the result is a half-turn; the seam itself is pinned by the exact +-kPi
    // cases above.
    for (const float k : {3.0f, -3.0f, 5.0f, -5.0f}) {
        const float out = wrap_pi(k * kPi);
        CHECK(std::fabs(std::fabs(out) - kPi) < 1e-5f);
        CHECK(out > -kPi);
        CHECK(out <= kPi);
    }
}

TEST_CASE("wrap_pi wraps just past +pi to just above -pi")
{
    const float eps = 1e-3f;
    const float wrapped = wrap_pi(kPi + eps);
    CHECK(wrapped == doctest::Approx(-kPi + eps).epsilon(1e-5));
    CHECK(wrapped > -kPi);
}

TEST_CASE("wrap_pi stays in (-pi, pi] and preserves the angle over a sweep")
{
    // 20001 samples from -100 to +100 rad; each result must lie in the interval
    // and differ from the input by a whole number of turns.
    int checked = 0;
    for (int i = -10000; i <= 10000; ++i) {
        const float in = static_cast<float>(i) * 0.01f;
        const float out = wrap_pi(in);
        REQUIRE(out > -kPi);
        REQUIRE(out <= kPi);
        const double turns = (static_cast<double>(in) - out) / kTwoPiD;
        REQUIRE(std::fabs(turns - std::round(turns)) < 1e-4);
        ++checked;
    }
    CHECK(checked == 20001);
}

TEST_CASE("wrap_pi handles large angles within float precision")
{
    const float in = 1000.0f;
    const double expected = std::remainder(1000.0, kTwoPiD);
    CHECK(static_cast<double>(wrap_pi(in)) == doctest::Approx(expected).epsilon(1e-3));
}

TEST_CASE("wrap_pi returns NaN for non-finite input")
{
    CHECK(std::isnan(wrap_pi(std::numeric_limits<float>::quiet_NaN())));
    CHECK(std::isnan(wrap_pi(std::numeric_limits<float>::infinity())));
}
