#include <cmath>

#include "direcsun/core/controllers.hpp"
#include "direcsun/core/modes.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using namespace harness;

namespace {

Params closed_loop_params()
{
    Params p;
    p.ff_count = static_cast<uint8_t>(kFfPoints);
    for (std::size_t i = 0; i < kFfPoints; ++i) {
        p.ff_rpm[i] = 500.0f * static_cast<float>(i + 1);
        p.ff_throttle[i] = 48.0f + p.ff_rpm[i] / 2.5f;
    }
    const WheelPiGains g = wheel_pi_gains(2.5f, 0.8f, kWheelPiBandwidthHz);
    p.wheel_kp = g.kp;
    p.wheel_ki = g.ki;
    return p;
}

Command slew_to(float heading_deg)
{
    Command c = mode_cmd(Mode::Slew);
    c.psi_ref_deg = heading_deg;
    return c;
}

}  // namespace

TEST_CASE("HT-4 slew profile is two seconds for 90 degrees and uses the shortest path")
{
    SlewProfile profile;
    profile.begin(0.0f, 90.0f);
    CHECK(profile.duration_s() == doctest::Approx(2.0f));
    CHECK(profile.reference_deg(0.5f) == doctest::Approx(15.0f));
    CHECK(profile.reference_deg(1.5f) == doctest::Approx(75.0f));
    CHECK(profile.reference_deg(2.0f) == doctest::Approx(90.0f));
    profile.begin(0.0f, 270.0f);
    CHECK(profile.goal_deg() == doctest::Approx(-90.0f));
    CHECK(profile.reference_deg(1.0f) == doctest::Approx(-45.0f));
    profile.begin(0.0f, -180.0f);
    CHECK(profile.goal_deg() == doctest::Approx(180.0f));
    profile.begin(0.0f, 180.0f);
    CHECK(profile.goal_deg() == doctest::Approx(180.0f));
}

TEST_CASE("R4.3 HOLD captures heading and SLEW reports its moving reference")
{
    Rig rig(closed_loop_params());
    rig.arm_to_ready();
    rig.in.heading_deg = 23.0f;
    rig.tick({mode_cmd(Mode::Hold)});
    REQUIRE(rig.last_ok());
    CHECK(rig.out.heading_ref_valid);
    CHECK(rig.out.heading_ref_deg == doctest::Approx(23.0f));
    rig.in.heading_deg = 20.0f;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps > 0.0f);
    rig.tick({mode_cmd(Mode::Ready)});
    rig.in.heading_deg = 0.0f;
    rig.tick({slew_to(90.0f)});
    REQUIRE(rig.last_ok());
    CHECK(rig.out.mode == Mode::Slew);
    rig.run(ticks_from_ms(1000));
    CHECK(rig.out.heading_ref_deg == doctest::Approx(45.0f).epsilon(0.01));
    CHECK(rig.ctl.last_demand.omega_cmd_dps > 0.0f);
}

TEST_CASE("R4.3 SLEW times out at six seconds and returns to READY")
{
    Rig rig(closed_loop_params());
    rig.arm_to_ready();
    rig.tick({slew_to(90.0f)});
    REQUIRE(rig.last_ok());
    rig.run(ticks_from_ms(kSlewTimeoutMs) - 1);
    CHECK(rig.out.mode == Mode::Slew);
    rig.tick();
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.has_event_cause("SLEW_TIMEOUT"));
}

TEST_CASE("R4.3 HOLD and SLEW refuse absent wheel loop or invalid heading")
{
    Rig unavailable;
    unavailable.arm_to_ready();
    unavailable.tick({mode_cmd(Mode::Hold)});
    CHECK(unavailable.last_reason() == "wheel loop not available");
    Rig bad_heading(closed_loop_params());
    bad_heading.arm_to_ready();
    bad_heading.in.imu_invalid_streak = 1;
    bad_heading.tick({slew_to(90.0f)});
    CHECK(bad_heading.last_reason() == "input not available");
}
