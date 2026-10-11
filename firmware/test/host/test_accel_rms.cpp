// T-7 (FD section 19.2, TD section 4.6): the accelerometer vibration figure
// the bench tlm carries as accel_rms_g.

#include <cmath>

#include "doctest.h"
#include "direcsun/core/estimator.hpp"

using namespace direcsun;

namespace {

constexpr float kPi = 3.14159265358979f;

// `samples` ticks at 500 Hz of gravity on z plus a sinusoid at `hz` with the
// given amplitude on x and y.
void feed(AccelRms &rms, int samples, float hz, float ax, float ay, int first = 0)
{
    for (int i = first; i < first + samples; ++i) {
        const float phase = 2.0f * kPi * hz * static_cast<float>(i) / 500.0f;
        const float a[3] = {ax * std::sin(phase), ay * std::sin(phase), 0.87f};
        rms.add(a);
    }
}

}  // namespace

TEST_CASE("T-7 accel RMS: gravity and tilt drop out and a 60 Hz vibration reads A over root two")
{
    SUBCASE("a still box reads zero whatever its tilt")
    {
        AccelRms rms;
        const float tilted[3] = {0.31f, -0.22f, 0.81f};
        for (int i = 0; i < 300; ++i) rms.add(tilted);
        CHECK(rms.rms_g() < 1e-4f);
    }
    SUBCASE("100 samples are exactly twelve turns of 60 Hz: 0.05 g on x reads 0.05 over root two")
    {
        AccelRms rms;
        feed(rms, 100, 60.0f, 0.05f, 0.0f);
        CHECK(rms.rms_g() == doctest::Approx(0.05f / std::sqrt(2.0f)).epsilon(0.02));
    }
    SUBCASE("axes add in quadrature")
    {
        AccelRms rms;
        feed(rms, 100, 60.0f, 0.03f, 0.04f);
        CHECK(rms.rms_g() == doctest::Approx(0.05f / std::sqrt(2.0f)).epsilon(0.02));
    }
    SUBCASE("a vibration on z alone reads zero: only the x-y plane counts (R4.7)")
    {
        AccelRms rms;
        for (int i = 0; i < 100; ++i) {
            const float phase = 2.0f * kPi * 60.0f * static_cast<float>(i) / 500.0f;
            const float a[3] = {0.0f, 0.0f, 0.87f + 0.05f * std::sin(phase)};
            rms.add(a);
        }
        CHECK(rms.rms_g() < 1e-4f);
    }
    SUBCASE("a vibration that stops leaves the window after 200 ms")
    {
        AccelRms rms;
        feed(rms, 100, 60.0f, 0.05f, 0.0f);
        REQUIRE(rms.rms_g() > 0.03f);
        const float still[3] = {0.0f, 0.0f, 0.87f};
        for (int i = 0; i < 99; ++i) rms.add(still);
        CHECK(rms.rms_g() > 0.0f);  // one vibrating sample is still in the window
        rms.add(still);
        CHECK(rms.rms_g() < 1e-4f);
    }
    SUBCASE("fewer than two samples read zero")
    {
        AccelRms rms;
        CHECK(rms.rms_g() == 0.0f);
        const float one[3] = {0.1f, 0.2f, 0.9f};
        rms.add(one);
        CHECK(rms.rms_g() == 0.0f);
        CHECK(rms.count() == 1);
    }
}
