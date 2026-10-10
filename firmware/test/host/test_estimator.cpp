// HT-3 subset (changes/3-teleop/R3.4-control-task.md): gyro bias, its
// stillness gate, tilt, the boot bias seed, and the gyro-only heading.

#include <cmath>
#include <string>

#include "direcsun/core/estimator.hpp"
#include "doctest.h"

using direcsun::Estimator;
using direcsun::EstimatorInputs;
using direcsun::GyroBiasSeed;

namespace {

constexpr float kTau = 5.0f;
constexpr int kTicksPerS = 500;

EstimatorInputs still_sample(float gyro_z_dps)
{
    EstimatorInputs in;
    in.gyro_z_dps = gyro_z_dps;
    in.accel_g[0] = 0.0f;
    in.accel_g[1] = 0.0f;
    in.accel_g[2] = 1.0f;
    in.safe_mode = true;
    in.wheel_known = false;
    in.s_gyro = 1;
    return in;
}

void run(Estimator &est, const EstimatorInputs &in, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        est.update(in);
    }
}

}  // namespace

TEST_CASE("HT-3 bias converges to a constant offset within 5 tau while still")
{
    Estimator est;
    const float offset = 1.5f;
    run(est, still_sample(offset), static_cast<int>(kTau * kTicksPerS));  // 1 tau
    CHECK(est.bias_dps() == doctest::Approx(offset * (1.0f - std::exp(-1.0f))).epsilon(0.01));
    run(est, still_sample(offset), static_cast<int>(4 * kTau * kTicksPerS));  // 5 tau in all
    CHECK(est.bias_dps() == doctest::Approx(offset).epsilon(0.01));
    CHECK(std::fabs(est.rate_dps()) < 0.02f);
    CHECK(est.bias_updating());
}

TEST_CASE("HT-3 bias freezes while the box turns faster than 2 deg/s")
{
    Estimator est;
    est.seed_bias(1.0f);
    run(est, still_sample(1.0f + 3.0f), 10 * kTicksPerS);
    CHECK(est.bias_dps() == 1.0f);
    CHECK_FALSE(est.bias_updating());
    CHECK(est.rate_dps() == doctest::Approx(3.0f));

    // Just inside the gate it learns.
    run(est, still_sample(1.0f + 1.9f), kTicksPerS);
    CHECK(est.bias_dps() > 1.0f);
}

TEST_CASE("HT-3 bias freezes outside SAFE, off 1 g, and with the wheel turning")
{
    // A fresh estimator per condition: the 20 ms accelerometer average would
    // otherwise carry one condition's samples into the next.
    Estimator armed_est;
    armed_est.seed_bias(0.5f);
    EstimatorInputs armed = still_sample(1.2f);
    armed.safe_mode = false;
    run(armed_est, armed, 10 * kTicksPerS);
    CHECK(armed_est.bias_dps() == 0.5f);

    Estimator shaken_est;
    shaken_est.seed_bias(0.5f);
    EstimatorInputs shaken = still_sample(1.2f);
    shaken.accel_g[2] = 1.08f;  // 0.08 g off 1 g
    run(shaken_est, shaken, 10 * kTicksPerS);
    CHECK(shaken_est.bias_dps() == 0.5f);

    Estimator wheel_est;
    wheel_est.seed_bias(0.5f);
    EstimatorInputs wheel = still_sample(1.2f);
    wheel.wheel_known = true;
    wheel.wheel_rpm = 250.0f;
    run(wheel_est, wheel, 10 * kTicksPerS);
    CHECK(wheel_est.bias_dps() == 0.5f);

    wheel.wheel_rpm = 150.0f;  // a known wheel below 200 rpm does not block it
    run(wheel_est, wheel, kTicksPerS);
    CHECK(wheel_est.bias_dps() > 0.5f);
}

TEST_CASE("HT-3 positive control: a spinning box in SAFE leaves the bias unchanged")
{
    // After an e-stop the box can spin at up to 4 rev/s while the supervisor is
    // in SAFE (FD finding V-10). Feed a box decaying from 360 deg/s to 30 deg/s
    // over 20 s with a level 1 g accelerometer: the bias must not move.
    // An estimator without the stillness gate learns this rate as bias.
    Estimator est;
    est.seed_bias(0.8f);
    for (int i = 0; i < 20 * kTicksPerS; ++i) {
        const float t = static_cast<float>(i) / kTicksPerS;
        run(est, still_sample(0.8f + 360.0f - 16.5f * t), 1);
    }
    CHECK(est.bias_dps() == 0.8f);
}

TEST_CASE("HT-3 tilt reads 0 deg level and 30 deg at the matching vector")
{
    Estimator est;
    run(est, still_sample(0.0f), Estimator::kAccelWindow);
    CHECK(est.tilt_deg() == doctest::Approx(0.0f).epsilon(1e-3));

    EstimatorInputs tilted = still_sample(0.0f);
    const float rad = 30.0f * 3.14159265f / 180.0f;
    tilted.accel_g[0] = std::sin(rad);
    tilted.accel_g[2] = std::cos(rad);
    run(est, tilted, Estimator::kAccelWindow);  // the 20 ms window is now all tilted
    CHECK(est.tilt_deg() == doctest::Approx(30.0f).epsilon(1e-3));

    // Half a window in, the average lies between the two.
    Estimator mid;
    run(mid, still_sample(0.0f), Estimator::kAccelWindow);
    run(mid, tilted, Estimator::kAccelWindow / 2);
    CHECK(mid.tilt_deg() > 1.0f);
    CHECK(mid.tilt_deg() < 29.0f);
}

TEST_CASE("HT-3 tilt reads 90 deg with no usable gravity vector")
{
    Estimator est;
    EstimatorInputs falling = still_sample(0.0f);
    falling.accel_g[2] = 0.05f;
    run(est, falling, Estimator::kAccelWindow);
    CHECK(est.tilt_deg() == 90.0f);
}

TEST_CASE("HT-3 s_gyro flips the box rate and the heading integrates it")
{
    Estimator est;
    EstimatorInputs in = still_sample(10.0f);
    in.safe_mode = false;  // keep the bias at 0
    in.s_gyro = -1;
    run(est, in, kTicksPerS);  // 1 s at -10 deg/s
    CHECK(est.rate_dps() == doctest::Approx(-10.0f));
    CHECK(est.heading_deg() == doctest::Approx(-10.0f).epsilon(1e-3));

    in.s_gyro = 1;
    in.gyro_z_dps = 100.0f;
    run(est, in, 2 * kTicksPerS);  // +200 deg: 190 deg unwrapped, -170 wrapped
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(190.0).epsilon(1e-4));
    CHECK(est.heading_deg() == doctest::Approx(-170.0f).epsilon(1e-3));
}

TEST_CASE("boot bias seed: a still window seeds, a moving or tilted one does not")
{
    const float level[3] = {0.0f, 0.0f, 1.0f};
    float bias = 0.0f;

    GyroBiasSeed still;
    for (int i = 0; i < 500; ++i) {
        still.add(3.4f + ((i % 2) ? 0.1f : -0.1f), level);  // 3.4 deg/s offset with noise
    }
    REQUIRE(still.result(bias));
    CHECK(bias == doctest::Approx(3.4f).epsilon(1e-3));

    GyroBiasSeed turning;  // a hand-turned box: the rate changes over the window
    for (int i = 0; i < 500; ++i) {
        turning.add(3.0f + 0.01f * static_cast<float>(i), level);
    }
    CHECK_FALSE(turning.result(bias));

    GyroBiasSeed spinning;  // steady but beyond the sensor's offset tolerance
    for (int i = 0; i < 500; ++i) {
        spinning.add(40.0f, level);
    }
    CHECK_FALSE(spinning.result(bias));

    GyroBiasSeed bumped;  // one accelerometer sample 0.2 g off the window's mean norm
    for (int i = 0; i < 500; ++i) {
        const float a[3] = {0.0f, 0.0f, i == 250 ? 1.2f : 1.0f};
        bumped.add(3.4f, a);
    }
    CHECK_FALSE(bumped.result(bias));

    GyroBiasSeed short_window;
    for (int i = 0; i < 100; ++i) {
        short_window.add(3.4f, level);
    }
    CHECK_FALSE(short_window.result(bias));
}

TEST_CASE("HT-3 a large offset seeded at boot is then tracked by the gate")
{
    // Without the seed a 6 deg/s offset never enters the 2 deg/s gate; with it,
    // a slow drift of the offset is followed.
    Estimator unseeded;
    run(unseeded, still_sample(6.0f), 10 * kTicksPerS);
    CHECK(unseeded.bias_dps() == 0.0f);

    Estimator seeded;
    seeded.seed_bias(6.0f);
    run(seeded, still_sample(6.5f), static_cast<int>(5 * kTau * kTicksPerS));
    CHECK(seeded.bias_dps() == doctest::Approx(6.5f).epsilon(0.01));
}

TEST_CASE("boot bias seed: an uncalibrated 0.871 g accelerometer at rest still seeds")
{
    // The XIAO bench MPU-6050 (Oct 10): accel (+0.033, -0.025, +0.871) g at rest,
    // gyro z about -2.1 deg/s with a 0.12 deg/s range over 425 samples. FD 11.1's
    // absolute test (|a| within 0.05 g of 1 g) refused this window; the relative
    // test must accept it.
    GyroBiasSeed seed;
    for (int i = 0; i < 425; ++i) {
        const float wobble = (i % 3 == 0) ? 0.003f : ((i % 3 == 1) ? -0.002f : 0.0f);
        const float a[3] = {0.033f, -0.025f, 0.871f + wobble};
        seed.add(-2.1f + ((i % 2) ? 0.06f : -0.06f), a);
    }
    float bias = 0.0f;
    REQUIRE(seed.result(bias));
    CHECK(seed.failure() == nullptr);
    CHECK(bias == doctest::Approx(-2.1f).epsilon(1e-3));
    CHECK(seed.reference_norm_g() == doctest::Approx(0.8719f).epsilon(2e-3));
}

TEST_CASE("boot bias seed: each refusal names its criterion")
{
    float bias = 0.0f;
    const auto contains = [](const char *text, const char *part) {
        return text != nullptr && std::string(text).find(part) != std::string::npos;
    };

    GyroBiasSeed turning;
    for (int i = 0; i < 500; ++i) {
        const float a[3] = {0.0f, 0.0f, 0.871f};
        turning.add(3.0f + 0.01f * static_cast<float>(i), a);
    }
    CHECK_FALSE(turning.result(bias));
    CHECK(contains(turning.failure(), "gyro range"));

    GyroBiasSeed spinning;
    for (int i = 0; i < 500; ++i) {
        const float a[3] = {0.0f, 0.0f, 0.871f};
        spinning.add(40.0f, a);
    }
    CHECK_FALSE(spinning.result(bias));
    CHECK(contains(spinning.failure(), "gyro mean"));

    GyroBiasSeed shaken;  // norm swings 0.1 g around its mean: lifted or bumped
    for (int i = 0; i < 500; ++i) {
        const float a[3] = {0.0f, 0.0f, 0.871f + ((i % 50 < 25) ? 0.1f : -0.1f)};
        shaken.add(-2.1f, a);
    }
    CHECK_FALSE(shaken.result(bias));
    CHECK(contains(shaken.failure(), "varied"));

    GyroBiasSeed dead;  // a dead accelerometer reads a steady near-zero vector
    for (int i = 0; i < 500; ++i) {
        const float a[3] = {0.0f, 0.0f, 0.02f};
        dead.add(-2.1f, a);
    }
    CHECK_FALSE(dead.result(bias));
    CHECK(contains(dead.failure(), "implausible"));

    GyroBiasSeed short_window;
    for (int i = 0; i < 100; ++i) {
        const float a[3] = {0.0f, 0.0f, 0.871f};
        short_window.add(-2.1f, a);
    }
    CHECK_FALSE(short_window.result(bias));
    CHECK(contains(short_window.failure(), "too few"));
}

TEST_CASE("HT-3 the bias gate uses the seed's accel reference, not 1 g")
{
    EstimatorInputs at_rest = still_sample(-2.3f);
    at_rest.accel_g[0] = 0.033f;
    at_rest.accel_g[1] = -0.025f;
    at_rest.accel_g[2] = 0.871f;

    // With the reference from the seed, the gate opens and the bias follows a
    // slow drift of the offset.
    Estimator referenced;
    referenced.seed_bias(-2.1f);
    referenced.set_accel_reference(0.8719f);
    run(referenced, at_rest, static_cast<int>(5 * kTau * kTicksPerS));
    CHECK(referenced.bias_dps() == doctest::Approx(-2.3f).epsilon(0.01));

    // Without it (reference 1 g), the same box never opens the gate.
    Estimator unreferenced;
    unreferenced.seed_bias(-2.1f);
    run(unreferenced, at_rest, static_cast<int>(5 * kTau * kTicksPerS));
    CHECK(unreferenced.bias_dps() == -2.1f);
}
