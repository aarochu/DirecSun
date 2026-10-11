// R5.2 (MANUAL wheel and rate) and R5.3 (respin, the FF_TABLE_MISSING and
// RATE_GAIN_REDUCED flags) against the real supervisor and cascade, with the
// wheel and box plant of box_rig.hpp.

#include <cmath>
#include <vector>

#include "box_rig.hpp"
#include "direcsun/core/modes.hpp"
#include "doctest.h"

using namespace direcsun;
using harness::BoxRig;
using harness::cascade_params;
using harness::cmd;
using harness::manual_cmd;
using harness::mode_cmd;

namespace {

constexpr double kDt = 1.0 / static_cast<double>(kTickHz);

Params scale_one()
{
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    return p;
}

bool has_flag(const BoxRig &rig, uint32_t bit) { return (rig.out.flags & bit) != 0; }

// Steps `seconds` and sends `value` as the MANUAL setpoint every 60 ms.
void drive_setpoint(BoxRig &rig, ManualInput input, float value, double seconds)
{
    const int n = static_cast<int>(seconds / kDt);
    const int every = static_cast<int>(0.06 / kDt);
    for (int i = 0; i < n; ++i) {
        if (i % every == 0) rig.tick({manual_cmd(input, value)});
        else rig.run(1);
    }
}

}  // namespace

TEST_CASE("R5.2 MANUAL wheel and rate need the wheel loop, and throttle does not")
{
    harness::Rig rig;   // default params: no T-5 table
    rig.arm_to_ready();
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Wheel)});
    CHECK(rig.last_reason() == "wheel loop not available");
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Rate)});
    CHECK(rig.last_reason() == "wheel loop not available");
    CHECK(rig.out.mode == Mode::Ready);
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    CHECK(rig.last_ok());
    CHECK(rig.out.mode == Mode::Manual);
}

TEST_CASE("R5.2 MANUAL wheel ramps the reference at 1000 rpm/s, clamps at the cap, and the box turns the other way")
{
    BoxRig rig(scale_one());
    rig.arm_to_ready();
    rig.release(0.0);
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Wheel)});
    REQUIRE(rig.out.mode == Mode::Manual);
    CHECK_FALSE(rig.ctl.rate_active());   // the wheel loop alone
    const float start = rig.ctl.wheel_ref_rpm();
    drive_setpoint(rig, ManualInput::Wheel, static_cast<float>(start) + 800.0f, 0.3);
    const float after = rig.ctl.wheel_ref_rpm();
    CHECK(after - start > 250.0f);
    CHECK(after - start < 340.0f);   // 1000 rpm/s for 0.3 s, never the 800 rpm step at once
    drive_setpoint(rig, ManualInput::Wheel, static_cast<float>(start) + 800.0f, 2.0);
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(start + 800.0f).epsilon(0.005));
    CHECK(rig.box.rate_dps() != doctest::Approx(0.0).epsilon(0.01));
    // Speeding the wheel up turned the box the other way: negative in the CCW-positive frame.
    CHECK(rig.box.heading_deg < -5.0);
    // A setpoint far past the cap is clamped by the supervisor, and nothing trips.
    drive_setpoint(rig, ManualInput::Wheel, 9000.0f, 5.0);
    CHECK(rig.ctl.wheel_ref_rpm() <= 4000.0f + 0.5f);
    CHECK(rig.out.mode == Mode::Manual);
    CHECK(rig.out.latched == 0);
}

TEST_CASE("R5.2 MANUAL rate tracks the setpoint, and falls to zero after 250 ms without one")
{
    for (double target : {30.0, -30.0}) {
        CAPTURE(target);
        BoxRig rig(scale_one());
        rig.arm_to_ready();
        rig.release(0.0);
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Rate)});
        REQUIRE(rig.out.mode == Mode::Manual);
        drive_setpoint(rig, ManualInput::Rate, static_cast<float>(target), 2.5);
        CHECK(std::fabs(rig.box.rate_dps() - target) < 0.1 * std::fabs(target));
        CHECK_FALSE(has_flag(rig, kFlagManualStale));
        // No more setpoints: stale at 250 ms, the commanded rate becomes 0, and the box slows.
        rig.run(static_cast<int>(0.4 / kDt));
        CHECK(has_flag(rig, kFlagManualStale));
        rig.run(static_cast<int>(4.0 / kDt));
        CHECK(std::fabs(rig.box.rate_dps()) < 5.0);
        CHECK(rig.out.mode == Mode::Manual);   // the heartbeat pings keep it alive
    }
}

TEST_CASE("R5.3 respin refusals carry their exact reasons")
{
    SUBCASE("not in READY")
    {
        BoxRig rig(scale_one());
        rig.tick({cmd(CommandKind::Respin)});
        CHECK(rig.out.acks.items[0].ok == false);
        CHECK(std::string(rig.out.acks.items[0].reason) == "not in READY");
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Detumble)});
        rig.tick({cmd(CommandKind::Respin)});
        CHECK(std::string(rig.out.acks.items[0].reason) == "not in READY");
    }
    SUBCASE("a turning box")
    {
        BoxRig rig(scale_one());
        rig.arm_to_ready();
        rig.in.rate_dps = 20.0f;
        Command c = cmd(CommandKind::Respin);
        rig.in.commands = &c;
        rig.in.command_count = 1;
        rig.sup.step(rig.in, rig.ctl, rig.out);
        CHECK(std::string(rig.out.acks.items[0].reason) == "box not still");
        CHECK(rig.out.mode == Mode::Ready);
    }
    SUBCASE("start speed range [Omega_low + 200, Omega_cap - 200]")
    {
        for (float rpm : {999.0f, 3801.0f, 0.0f, 5000.0f}) {
            BoxRig rig(scale_one());
            rig.arm_to_ready();
            Command c = cmd(CommandKind::Respin);
            c.has_wheel_rpm = true;
            c.wheel_rpm = rpm;
            rig.tick({c});
            CAPTURE(rpm);
            CHECK(std::string(rig.out.acks.items[0].reason) == "invalid setpoint");
            CHECK(rig.out.mode == Mode::Ready);
        }
    }
    SUBCASE("a second client and no wheel loop")
    {
        BoxRig rig(scale_one());
        rig.arm_to_ready();
        rig.tick({cmd(CommandKind::Respin, harness::kViewer)});
        CHECK(std::string(rig.out.acks.items[0].reason) == "not controlling client");
        harness::Rig bare;
        bare.arm_to_ready();
        bare.tick({cmd(CommandKind::Respin)});
        CHECK(bare.last_reason() == "wheel loop not available");
    }
}

TEST_CASE("R5.3 respin to 1000 rpm and to 3800 rpm returns to READY at that speed, with no interlock")
{
    for (float target : {1000.0f, 3800.0f}) {
        CAPTURE(target);
        BoxRig rig(scale_one());
        rig.arm_to_ready();
        Command c = cmd(CommandKind::Respin);
        c.has_wheel_rpm = true;
        c.wheel_rpm = target;
        rig.tick({c});
        REQUIRE(rig.out.acks.count > 0);
        REQUIRE(rig.out.acks.items[0].ok);
        CHECK(rig.out.mode == Mode::Spinup);
        CHECK(rig.has_event_cause("respin"));
        const float from = rig.ctl.wheel_ref_rpm();
        rig.run(static_cast<int>(1.0 / kDt));
        // 400 rpm/s toward the target, in either direction: no step.
        const float expect = from + (target > from ? 400.0f : -400.0f);
        CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(expect).epsilon(0.02));
        bool ready = false;
        for (int i = 0; i < static_cast<int>(10.0 / kDt) && !ready; ++i) {
            rig.run(1);
            REQUIRE(rig.out.latched == 0);
            ready = rig.out.mode == Mode::Ready;
        }
        REQUIRE(ready);
        CHECK(rig.box.wheel_rpm == doctest::Approx(target).epsilon(0.05));
        rig.run(static_cast<int>(3.0 / kDt));
        CHECK(rig.out.mode == Mode::Ready);
        CHECK(rig.out.latched == 0);
    }
}

TEST_CASE("R5.3 respin without a speed returns the wheel to the bias")
{
    BoxRig rig(scale_one());
    rig.arm_to_ready();
    Command low = cmd(CommandKind::Respin);
    low.has_wheel_rpm = true;
    low.wheel_rpm = 1000.0f;
    rig.tick({low});
    for (int i = 0; i < static_cast<int>(10.0 / kDt) && rig.out.mode != Mode::Ready; ++i) rig.run(1);
    REQUIRE(rig.out.mode == Mode::Ready);
    rig.tick({cmd(CommandKind::Respin)});
    REQUIRE(rig.out.acks.count > 0);
        REQUIRE(rig.out.acks.items[0].ok);
    for (int i = 0; i < static_cast<int>(10.0 / kDt) && rig.out.mode != Mode::Ready; ++i) rig.run(1);
    REQUIRE(rig.out.mode == Mode::Ready);
    CHECK(rig.box.wheel_rpm == doctest::Approx(rig.sup.params().omega_bias_rpm).epsilon(0.05));
}

TEST_CASE("R5.3 FF_TABLE_MISSING and RATE_GAIN_REDUCED are status flags in every mode")
{
    {
        harness::Rig rig;   // defaults: Hall installed, no table, scale 0.2
        rig.tick();
        CHECK((rig.out.flags & kFlagFfTableMissing) != 0);
        CHECK((rig.out.flags & kFlagRateGainReduced) != 0);
        rig.arm_to_ready();
        CHECK((rig.out.flags & kFlagFfTableMissing) != 0);
    }
    {
        BoxRig rig(scale_one());
        rig.arm_to_ready();
        CHECK_FALSE(has_flag(rig, kFlagFfTableMissing));
        CHECK_FALSE(has_flag(rig, kFlagRateGainReduced));
        Params p = scale_one();
        p.rate_gain_scale = 0.99f;
        BoxRig reduced(p);
        reduced.tick();
        CHECK(has_flag(reduced, kFlagRateGainReduced));
    }
    CHECK(std::string(flag_wire_name(kFlagFfTableMissing)) == "FF_TABLE_MISSING");
    CHECK(std::string(flag_wire_name(kFlagRateGainReduced)) == "RATE_GAIN_REDUCED");
}
