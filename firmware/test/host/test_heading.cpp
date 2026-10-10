// R4.2: the gyro-integrated heading (FD section 11.3 until Set 5's magnetometer)
// and zero_heading. The DIRECSUN_MUTANT_RAW_RATE_HEADING build integrates the
// raw gyro, bias included, and must fail the seeded-bias case
// (tools/host-test.sh --selftest).

#include <cmath>
#include <cstdint>

#include "direcsun/core/angles.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/modes.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::cmd;
using harness::mode_cmd;
using harness::Rig;

namespace {

constexpr int kTicksPerS = 500;

// One sample of a box turning at `gyro_dps` (raw sensor z), level, outside
// SAFE so the bias holds whatever it was seeded with.
EstimatorInputs turning(float gyro_dps)
{
    EstimatorInputs in;
    in.gyro_z_dps = gyro_dps;
    in.accel_g[2] = 1.0f;
    in.safe_mode = false;
    return in;
}

void run(Estimator &est, const EstimatorInputs &in, int ticks)
{
    for (int i = 0; i < ticks; ++i) est.update(in);
}

}  // namespace

TEST_CASE("R4.2 heading: 30 deg/s for 3 s integrates to 90 deg")
{
    Estimator est;
    run(est, turning(30.0f), 3 * kTicksPerS);
    MESSAGE("heading after 3 s at 30 deg/s: " << est.heading_unwrapped_deg() << " deg");
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(90.0).epsilon(0.1 / 90.0));
    CHECK(std::fabs(est.heading_unwrapped_deg() - 90.0) < 0.1);
}

TEST_CASE("R4.2 heading: a seeded 0.5 deg/s bias is removed before integration")
{
    // The bias the stillness gate learned (R3.4) must be subtracted before the
    // heading integrates; otherwise 0.5 deg/s becomes 1.5 deg in 3 s, half of
    // PR-2's settle band. The raw-rate mutant fails here.
    Estimator est;
    est.seed_bias(0.5f);
    run(est, turning(30.5f), 3 * kTicksPerS);
    MESSAGE("heading with a 0.5 deg/s bias removed: " << est.heading_unwrapped_deg() << " deg");
    CHECK(std::fabs(est.heading_unwrapped_deg() - 90.0) < 0.1);
}

TEST_CASE("R4.2 heading: display wraps to (-180, 180] while control keeps it continuous")
{
    Estimator est;
    run(est, turning(100.0f), 4 * kTicksPerS);  // +400 deg
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(400.0).epsilon(1e-5));
    CHECK(est.heading_deg() == doctest::Approx(40.0f).epsilon(1e-3));
    run(est, turning(-100.0f), 6 * kTicksPerS);  // -600 deg: -200 continuous, +160 displayed
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(-200.0).epsilon(1e-5));
    CHECK(est.heading_deg() == doctest::Approx(160.0f).epsilon(1e-3));
    // The wrap helper itself: the seam belongs to +180, and large values keep precision.
    CHECK(wrap_deg180(180.0) == doctest::Approx(180.0f));
    CHECK(wrap_deg180(-180.0) == doctest::Approx(180.0f));
    CHECK(wrap_deg180(3600.0 + 12.5) == doctest::Approx(12.5f).epsilon(1e-4));
    CHECK(wrap_deg180(-725.0) == doctest::Approx(-5.0f).epsilon(1e-4));
}

TEST_CASE("R4.2 zero_heading: the heading reads 0 in the same tick, in SAFE and READY only")
{
    Rig rig;
    rig.in.heading_deg = 123.4;
    rig.tick();
    CHECK(rig.out.heading_deg == doctest::Approx(123.4));
    CHECK((rig.out.flags & kFlagHeadingGyro) != 0);

    SUBCASE("SAFE: accepted, reads 0 at once, then follows the estimator")
    {
        rig.tick({cmd(CommandKind::ZeroHeading)});
        CHECK(rig.last_ok());
        CHECK(rig.out.heading_deg == doctest::Approx(0.0));
        rig.in.heading_deg = 130.4;
        rig.tick();
        CHECK(rig.out.heading_deg == doctest::Approx(7.0));
    }
    SUBCASE("READY: accepted from the controlling client")
    {
        rig.arm_to_ready();
        REQUIRE(rig.out.mode == Mode::Ready);
        rig.in.heading_deg = -45.0;
        rig.tick({cmd(CommandKind::ZeroHeading)});
        CHECK(rig.last_ok());
        CHECK(rig.out.heading_deg == doctest::Approx(0.0));
    }
    SUBCASE("SPINUP and MANUAL: refused, the heading keeps its zero")
    {
        rig.tick({cmd(CommandKind::Arm)});
        REQUIRE(rig.out.mode == Mode::Spinup);
        rig.tick({cmd(CommandKind::ZeroHeading)});
        CHECK(rig.last_reason() == "not in SAFE or READY");
        CHECK(rig.out.heading_deg == doctest::Approx(123.4));

        rig.in.wheel_rpm_safe = rig.sup.params().omega_bias_rpm;
        rig.run(ticks_from_ms(kSpinupDwellMs) + 2);
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
        REQUIRE(rig.out.mode == Mode::Manual);
        rig.tick({cmd(CommandKind::ZeroHeading)});
        CHECK(rig.last_reason() == "not in SAFE or READY");
    }
    SUBCASE("a viewer cannot zero an armed box")
    {
        rig.arm_to_ready();
        rig.tick({cmd(CommandKind::ZeroHeading, harness::kViewer)});
        CHECK(rig.last_reason() == "not controlling client");
        CHECK(rig.out.heading_deg == doctest::Approx(123.4));
    }
}
