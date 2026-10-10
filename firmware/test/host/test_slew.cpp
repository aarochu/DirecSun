// R4.3: the slew trapezoid (HT-4), the attitude loop, and the supervisor's
// HOLD and SLEW: entry guards, the heading reference, completion, timeout, and
// exits. The closed loop against the plant is in test_closed_loop.cpp.

#include <cmath>
#include <cstdint>
#include <vector>

#include "direcsun/core/controllers.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::cmd;
using harness::mode_cmd;
using harness::Rig;

namespace {

constexpr double kDt = 1.0 / static_cast<double>(kTickHz);

// A box whose T-5 feed-forward table is set, so the closed wheel loop (and
// with it HOLD and SLEW) is available.
Params characterised()
{
    Params p;
    p.ff_count = static_cast<uint8_t>(kFfPoints);
    for (std::size_t i = 0; i < kFfPoints; ++i) {
        p.ff_rpm[i] = 500.0f * static_cast<float>(i + 1);
        p.ff_throttle[i] = 48.0f + p.ff_rpm[i] / 2.5f;
    }
    return p;
}

// `mode SLEW` with its target, internal frame (the decoder has already
// converted it from the wire).
Command slew_cmd(float target_deg, uint32_t client = harness::kOperator)
{
    Command c = mode_cmd(Mode::Slew, ManualInput::None, client);
    c.psi_ref_deg = target_deg;
    return c;
}

// Ticks after the command until the mode leaves SLEW; the box follows the
// reference exactly when `follow` is set and stays where it is otherwise.
uint32_t ticks_until_slew_ends(Rig &rig, bool follow, uint32_t limit)
{
    uint32_t n = 0;
    while (rig.out.mode == Mode::Slew && n < limit) {
        if (follow) rig.in.heading_deg = rig.out.psi_ref_deg;
        rig.tick(n % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
        ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("HT-4: the 90 deg trapezoid is 0.5 s accelerating, 1.0 s at 60 deg/s, 0.5 s decelerating")
{
    SlewProfile s;
    s.start(0.0, 90.0);
    CHECK(s.duration_s() == doctest::Approx(2.0));
    CHECK(s.target_deg() == doctest::Approx(90.0));
    CHECK(s.position_deg(0.5) == doctest::Approx(15.0));
    CHECK(s.velocity_dps(0.5) == doctest::Approx(60.0));
    CHECK(s.velocity_dps(1.0) == doctest::Approx(60.0));
    CHECK(s.position_deg(1.5) == doctest::Approx(75.0));
    CHECK(s.position_deg(2.0) == doctest::Approx(90.0));
    CHECK_FALSE(s.finished(2.0 - kDt));
    CHECK(s.finished(2.0));

    // Sampled every tick, as the supervisor does: never faster than 60 deg/s,
    // never accelerating harder than 120 deg/s^2, and never moving backwards.
    double peak_v = 0.0;
    double peak_a = 0.0;
    double previous_v = 0.0;
    for (int k = 0; k <= 1100; ++k) {
        const double t = k * kDt;
        const double v = (s.position_deg(t + kDt) - s.position_deg(t)) / kDt;
        CHECK(v >= 0.0);
        peak_v = std::fmax(peak_v, v);
        if (k > 0) peak_a = std::fmax(peak_a, std::fabs(v - previous_v) / kDt);
        previous_v = v;
    }
    MESSAGE("peak " << peak_v << " deg/s, " << peak_a << " deg/s^2");
    CHECK(peak_v <= 60.0 + 1e-6);
    CHECK(peak_a <= 120.0 + 1e-3);

    // Short moves never reach 60 deg/s: a triangle at 120 deg/s^2.
    s.start(10.0, 4.0);
    CHECK(s.duration_s() == doctest::Approx(2.0 * std::sqrt(6.0 / 120.0)));
    CHECK(s.position_deg(s.duration_s()) == doctest::Approx(4.0));
    CHECK(s.velocity_dps(s.duration_s() / 2.0) == doctest::Approx(-std::sqrt(6.0 * 120.0)));
}

TEST_CASE("HT-4: through the supervisor, the 90 deg reference reaches its target 2.0 s after entry")
{
    Rig rig(characterised());
    rig.arm_to_ready();
    rig.tick({slew_cmd(90.0f)});
    REQUIRE(rig.last_ok());
    REQUIRE(rig.out.mode == Mode::Slew);
    CHECK(rig.out.psi_ref_valid);
    CHECK(rig.out.psi_ref_deg == doctest::Approx(0.0));
    uint32_t n = 0;
    while (rig.out.psi_ref_deg < 90.0 - 1e-9 && n < 2000) {
        rig.tick(n % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
        ++n;
    }
    MESSAGE("reference at 90 deg " << n << " ticks after entry");
    CHECK(n >= 999);
    CHECK(n <= 1001);
}

TEST_CASE("HT-4: SLEW takes the shortest wrapped path, and a half-turn is deterministic")
{
    struct Case {
        double from_deg;    // heading at entry, relative to the zero, continuous
        float target_deg;   // the command's target
        double end_deg;     // where the reference must end
    };
    const Case cases[] = {
        {0.0, -90.0f, -90.0},    // the judge's left turn
        {0.0, 270.0f, -90.0},    // -90 over +270
        {170.0, -170.0f, 190.0}, // across the wrap: 20 deg, not 340
        {-170.0, 170.0f, -190.0},
        {0.0, 180.0f, 180.0},    // a half-turn always goes counter-clockwise
        {0.0, -180.0f, 180.0},
        {10.0, -170.0f, 190.0},
        {-10.0, 170.0f, 170.0},
    };
    for (const Case &c : cases) {
        CAPTURE(c.from_deg);
        CAPTURE(c.target_deg);
        // Twice, each from a fresh supervisor: the same command gives the same path.
        for (int run = 0; run < 2; ++run) {
            Rig rig(characterised());
            rig.arm_to_ready();
            rig.in.heading_deg = c.from_deg;
            rig.tick({slew_cmd(c.target_deg)});
            REQUIRE(rig.out.mode == Mode::Slew);
            rig.run(50);
            const double direction = c.end_deg > c.from_deg ? 1.0 : -1.0;
            CHECK((rig.out.psi_ref_deg - c.from_deg) * direction > 0.0);
            rig.run(1950);  // 4 s after entry: a half-turn takes 3.5 s, the timeout is 6
            REQUIRE(rig.out.mode == Mode::Slew);
            CHECK(rig.out.psi_ref_deg == doctest::Approx(c.end_deg));
        }
    }
}

TEST_CASE("R4.3 attitude loop: omega_cmd = K_psi wrap(psi_ref - psi), limited to 90 deg/s")
{
    CHECK(attitude_rate_cmd_dps(10.0, 0.0, 3.1f) == doctest::Approx(31.0));
    CHECK(attitude_rate_cmd_dps(0.0, 10.0, 3.1f) == doctest::Approx(-31.0));
    CHECK(attitude_rate_cmd_dps(0.0, 350.0, 3.1f) == doctest::Approx(31.0));  // wraps to +10
    CHECK(attitude_rate_cmd_dps(725.0, 0.0, 3.1f) == doctest::Approx(15.5));  // two turns and 5 deg
    CHECK(attitude_rate_cmd_dps(90.0, 0.0, 3.1f) == doctest::Approx(kAttitudeRateLimitDps));
    CHECK(attitude_rate_cmd_dps(-90.0, 0.0, 3.1f) == doctest::Approx(-kAttitudeRateLimitDps));
    // The hold deadband: nothing inside it, the full law outside it.
    CHECK(attitude_rate_cmd_dps(0.9, 0.0, 3.1f) == doctest::Approx(2.79));
    CHECK(attitude_rate_cmd_dps(0.9, 0.0, 3.1f, kHoldDeadbandDeg) == 0.0f);
    CHECK(attitude_rate_cmd_dps(-0.9, 0.0, 3.1f, kHoldDeadbandDeg) == 0.0f);
    CHECK(attitude_rate_cmd_dps(1.1, 0.0, 3.1f, kHoldDeadbandDeg) == doctest::Approx(3.41));
    CHECK(attitude_rate_cmd_dps(0.0, 361.1, 3.1f, kHoldDeadbandDeg) == doctest::Approx(-3.41));
}

TEST_CASE("R4.3 HOLD: captures the heading at entry and steers back to it until mode READY")
{
    Rig rig(characterised());
    rig.arm_to_ready();
    CHECK_FALSE(rig.out.psi_ref_valid);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);  // READY holds rate

    // Zeroed at 100 deg, so a heading of 125 is +25 relative.
    rig.in.heading_deg = 100.0;
    rig.tick({cmd(CommandKind::ZeroHeading)});
    REQUIRE(rig.last_ok());
    rig.in.heading_deg = 125.0;
    rig.tick({mode_cmd(Mode::Hold)});
    REQUIRE(rig.last_ok());
    CHECK(rig.out.mode == Mode::Hold);
    CHECK(rig.has_event_cause("mode"));
    CHECK(rig.out.psi_ref_valid);
    CHECK(rig.out.psi_ref_deg == doctest::Approx(25.0));
    CHECK(rig.out.heading_deg == doctest::Approx(25.0));
    CHECK(rig.ctl.last_demand.mode == Mode::Hold);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == doctest::Approx(0.0));

    // Pushed 2 deg counter-clockwise: the command turns it back. Inside the
    // hold deadband it commands nothing, so friction can hold the box.
    rig.in.heading_deg = 127.0;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps == doctest::Approx(-3.1 * 2.0).epsilon(1e-4));
    CHECK(rig.out.psi_ref_deg == doctest::Approx(25.0));
    rig.in.heading_deg = 125.5;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);
    rig.in.heading_deg = 125.0;

    // HOLD has no timeout.
    rig.run(10 * kTickHz);
    CHECK(rig.out.mode == Mode::Hold);
    CHECK(rig.out.psi_ref_deg == doctest::Approx(25.0));

    rig.tick({mode_cmd(Mode::Ready)});
    CHECK(rig.last_ok());
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.has_event_cause("mode"));
    CHECK_FALSE(rig.out.psi_ref_valid);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);
}

TEST_CASE("R4.3 HOLD and SLEW guards")
{
    SUBCASE("from READY only")
    {
        Rig rig(characterised());
        rig.tick({mode_cmd(Mode::Hold)});
        CHECK(rig.last_reason() == "not in READY");
        rig.tick({slew_cmd(90.0f)});
        CHECK(rig.last_reason() == "not in READY");
        CHECK(rig.out.mode == Mode::Safe);
        rig.tick({cmd(CommandKind::Arm)});
        rig.tick({mode_cmd(Mode::Hold)});
        CHECK(rig.last_reason() == "not in READY");
        CHECK(rig.out.mode == Mode::Spinup);
    }
    SUBCASE("not from MANUAL, and not from each other")
    {
        Rig rig(characterised());
        rig.enter_manual(ManualInput::Throttle);
        REQUIRE(rig.out.mode == Mode::Manual);
        rig.tick({mode_cmd(Mode::Hold)});
        CHECK(rig.last_reason() == "not in READY");
        rig.tick({mode_cmd(Mode::Ready)});
        rig.tick({mode_cmd(Mode::Hold)});
        REQUIRE(rig.out.mode == Mode::Hold);
        rig.tick({slew_cmd(90.0f)});
        CHECK(rig.last_reason() == "not in READY");
        CHECK(rig.out.mode == Mode::Hold);
        rig.tick({mode_cmd(Mode::Ready)});
        rig.tick({slew_cmd(90.0f)});
        REQUIRE(rig.out.mode == Mode::Slew);
        rig.tick({mode_cmd(Mode::Hold)});
        CHECK(rig.last_reason() == "not in READY");
        CHECK(rig.out.mode == Mode::Slew);
    }
    SUBCASE("refused until the closed wheel loop exists")
    {
        Rig rig;  // no feed-forward table
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Hold)});
        CHECK(rig.last_reason() == "wheel loop not available");
        rig.tick({slew_cmd(90.0f)});
        CHECK(rig.last_reason() == "wheel loop not available");
        CHECK(rig.out.mode == Mode::Ready);
    }
    SUBCASE("only the controlling client")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Hold, ManualInput::None, harness::kViewer)});
        CHECK(rig.last_reason() == "not controlling client");
        rig.tick({slew_cmd(90.0f, harness::kViewer)});
        CHECK(rig.last_reason() == "not controlling client");
        CHECK(rig.out.mode == Mode::Ready);
    }
    SUBCASE("zero_heading is refused while a reference is held")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Hold)});
        rig.tick({cmd(CommandKind::ZeroHeading)});
        CHECK(rig.last_reason() == "not in SAFE or READY");
        rig.tick({mode_cmd(Mode::Ready)});
        rig.tick({slew_cmd(45.0f)});
        rig.tick({cmd(CommandKind::ZeroHeading)});
        CHECK(rig.last_reason() == "not in SAFE or READY");
    }
}

TEST_CASE("R4.3 HOLD and SLEW end in SAFE on disarm, estop, and interlocks")
{
    SUBCASE("disarm in HOLD")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Hold)});
        rig.tick({cmd(CommandKind::Disarm)});
        CHECK(rig.out.mode == Mode::Safe);
        CHECK(rig.has_event_cause("disarm"));
        CHECK_FALSE(rig.out.psi_ref_valid);
    }
    SUBCASE("estop in SLEW")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({slew_cmd(-90.0f)});
        rig.run(100);
        rig.tick({}, true);
        CHECK(rig.out.mode == Mode::Safe);
        CHECK(rig.has_event_cause("ESTOP"));
        CHECK_FALSE(rig.out.psi_ref_valid);
    }
    SUBCASE("heartbeat loss in HOLD")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Hold)});
        rig.run(ticks_from_ms(kHeartbeatTimeoutMs) + 2, false);
        CHECK(rig.out.mode == Mode::Safe);
        CHECK_FALSE(rig.out.psi_ref_valid);
    }
}

TEST_CASE("R4.3 SLEW: complete 0.5 s after the box settles on the target, then READY")
{
    Rig rig(characterised());
    rig.arm_to_ready();
    rig.tick({slew_cmd(90.0f)});
    REQUIRE(rig.out.mode == Mode::Slew);
    // The box follows the reference one tick late: settled from the profile's
    // end at 2.0 s, so complete 0.5 s later.
    const uint32_t n = ticks_until_slew_ends(rig, true, 4000);
    MESSAGE("slew complete " << n << " ticks after entry");
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.has_event_cause("slew_complete"));
    CHECK_FALSE(rig.has_event_cause("SLEW_TIMEOUT"));
    CHECK(n >= 1249);
    CHECK(n <= 1251);
    CHECK_FALSE(rig.out.psi_ref_valid);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);
}

TEST_CASE("R4.3 SLEW: the hold deadband applies only once the reference stands still")
{
    Rig rig(characterised());
    rig.arm_to_ready();
    rig.tick({slew_cmd(90.0f)});
    rig.run(250);  // 0.5 s in: the reference moves at 60 deg/s
    rig.in.heading_deg = rig.out.psi_ref_deg - 0.5;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps > 0.0f);
    rig.run(1000);  // past the profile's end at 2.0 s
    REQUIRE(rig.out.mode == Mode::Slew);
    rig.in.heading_deg = 89.5;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);
    rig.in.heading_deg = 88.5;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps == doctest::Approx(3.1 * 1.5).epsilon(1e-4));
}

TEST_CASE("R4.3 SLEW: still turning, or off target, is not complete")
{
    SUBCASE("4 deg short of the target times out")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({slew_cmd(90.0f)});
        uint32_t n = 0;
        while (rig.out.mode == Mode::Slew && n < 4000) {
            rig.in.heading_deg = rig.out.psi_ref_deg - 4.0;
            rig.tick(n % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
            ++n;
        }
        CHECK(rig.has_event_cause("SLEW_TIMEOUT"));
    }
    SUBCASE("on target but turning at 6 deg/s times out")
    {
        Rig rig(characterised());
        rig.arm_to_ready();
        rig.tick({slew_cmd(90.0f)});
        rig.in.rate_dps = 6.0f;
        ticks_until_slew_ends(rig, true, 4000);
        CHECK(rig.has_event_cause("SLEW_TIMEOUT"));
    }
}

TEST_CASE("R4.3 SLEW: a box that never arrives times out at 6.0 s with SLEW_TIMEOUT, then READY")
{
    Rig rig(characterised());
    rig.arm_to_ready();
    rig.tick({slew_cmd(90.0f)});
    REQUIRE(rig.out.mode == Mode::Slew);
    const uint32_t n = ticks_until_slew_ends(rig, false, 4000);
    MESSAGE("timed out " << n << " ticks after entry");
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.has_event_cause("SLEW_TIMEOUT"));
    CHECK(n >= 2999);
    CHECK(n <= 3001);
}

TEST_CASE("R4.3 SLEW: mode READY cancels it")
{
    Rig rig(characterised());
    rig.arm_to_ready();
    rig.tick({slew_cmd(90.0f)});
    rig.run(200);
    rig.tick({mode_cmd(Mode::Ready)});
    CHECK(rig.last_ok());
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.has_event_cause("mode"));
    CHECK_FALSE(rig.out.psi_ref_valid);
}
