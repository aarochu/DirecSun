// R4.6: the status flags the UI cannot otherwise see (FF_TABLE_MISSING,
// RATE_GAIN_REDUCED, PARAMS_RESET) and the refusal strings of DETUMBLE and
// SUN_POINT (FD section 13.1).

#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace harness;

namespace {

Params cascade_params()
{
    Params p;
    p.ff_count = static_cast<uint8_t>(kFfPoints);
    for (std::size_t i = 0; i < kFfPoints; ++i) {
        p.ff_rpm[i] = 500.0f * static_cast<float>(i + 1);
        p.ff_throttle[i] = 48.0f + p.ff_rpm[i] / 2.5f;
    }
    p.rate_gain_scale = 1.0f;
    return p;
}

bool has(const TickOutputs &out, uint32_t flag)
{
    return (out.flags & flag) != 0;
}

}  // namespace

TEST_CASE("R4.6 FF_TABLE_MISSING and RATE_GAIN_REDUCED follow the parameters in every mode")
{
    SUBCASE("the defaults: no table, gain 0.2")
    {
        Rig rig;
        rig.tick();
        CHECK(rig.out.mode == Mode::Safe);
        CHECK(has(rig.out, kFlagFfTableMissing));
        CHECK(has(rig.out, kFlagRateGainReduced));
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.out.mode == Mode::Spinup);
        CHECK(has(rig.out, kFlagFfTableMissing));
        CHECK(has(rig.out, kFlagRateGainReduced));
    }
    SUBCASE("the cascade parameters: neither flag, in SAFE, SPINUP, READY, and MANUAL")
    {
        Rig rig(cascade_params());
        rig.tick();
        CHECK_FALSE(has(rig.out, kFlagFfTableMissing));
        CHECK_FALSE(has(rig.out, kFlagRateGainReduced));
        rig.arm_to_ready();
        REQUIRE(rig.out.mode == Mode::Ready);
        CHECK_FALSE(has(rig.out, kFlagFfTableMissing));
        CHECK_FALSE(has(rig.out, kFlagRateGainReduced));
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
        REQUIRE(rig.out.mode == Mode::Manual);
        CHECK_FALSE(has(rig.out, kFlagFfTableMissing));
    }
    SUBCASE("the flags are in the READY and MANUAL frames of a default box")
    {
        Rig rig;
        rig.arm_to_ready();
        CHECK(has(rig.out, kFlagFfTableMissing));
        CHECK(has(rig.out, kFlagRateGainReduced));
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
        CHECK(has(rig.out, kFlagFfTableMissing));
    }
    SUBCASE("a bypassed Hall reports HALL_BYPASS, not FF_TABLE_MISSING")
    {
        Rig rig;
        ParamUpdate bench;
        add_bool(bench, "bench_mode", true);
        rig.tick({set_params_cmd(&bench)});
        ParamUpdate bypass;
        add_bool(bypass, "hall_installed", false);
        rig.tick({set_params_cmd(&bypass)});
        REQUIRE(rig.last_ok());
        CHECK(has(rig.out, kFlagHallBypass));
        CHECK_FALSE(has(rig.out, kFlagFfTableMissing));
    }
    SUBCASE("set_params clears RATE_GAIN_REDUCED, and a table clears FF_TABLE_MISSING, on the next tick")
    {
        Rig rig;
        rig.tick();
        ParamUpdate gain;
        add_number(gain, "rate_gain_scale", 1.0);
        rig.tick({set_params_cmd(&gain)});
        REQUIRE(rig.last_ok());
        CHECK_FALSE(has(rig.out, kFlagRateGainReduced));
        CHECK(has(rig.out, kFlagFfTableMissing));
        gain.count = 0;
        add_number(gain, "rate_gain_scale", 0.5);
        rig.tick({set_params_cmd(&gain)});
        CHECK(has(rig.out, kFlagRateGainReduced));
    }
}

TEST_CASE("R4.6 PARAMS_RESET is a flag until the first accepted set_params")
{
    ScriptedController ctl;
    TickInputs in;
    in.vbat_avg_v = 10.4f;
    TickOutputs out;
    uint32_t t = 1000;
    in.hall_edge_age_ms = 10;
    in.built_inputs = manual_input_bit(ManualInput::Throttle);

    Supervisor sup(Params{}, true, "version");
    in.tick = t;
    sup.step(in, ctl, out);
    CHECK(has(out, kFlagParamsReset));
    REQUIRE(out.events.count == 1);  // the one-shot notice is still posted
    CHECK(std::string(out.events.items[0].notice) == "PARAMS_RESET");

    // The flag stays, with the notice gone, across a long quiet stretch.
    for (int i = 0; i < 100; ++i) {
        in.tick = ++t;
        sup.step(in, ctl, out);
        CHECK(out.events.count == 0);
        REQUIRE(has(out, kFlagParamsReset));
    }

    // A refused set_params (an invalid key) leaves it set.
    ParamUpdate bad;
    add_number(bad, "k_omega", 0.06);
    Command refused = set_params_cmd(&bad);
    in.tick = ++t;
    in.commands = &refused;
    in.command_count = 1;
    sup.step(in, ctl, out);
    REQUIRE_FALSE(out.acks.items[0].ok);
    CHECK(has(out, kFlagParamsReset));

    // It survives an arm, a valid set_params refused because the box is armed,
    // and a disarm.
    Command arm = cmd(CommandKind::Arm);
    in.tick = ++t;
    in.commands = &arm;
    in.command_count = 1;
    sup.step(in, ctl, out);
    REQUIRE(out.acks.items[0].ok);
    REQUIRE(out.mode == Mode::Spinup);
    CHECK(has(out, kFlagParamsReset));
    ParamUpdate valid;
    add_number(valid, "rate_gain_scale", 0.4);
    Command armed_update = set_params_cmd(&valid);
    in.tick = ++t;
    in.commands = &armed_update;
    sup.step(in, ctl, out);
    REQUIRE_FALSE(out.acks.items[0].ok);
    CHECK(std::string(out.acks.items[0].reason) == "not in SAFE");
    CHECK(has(out, kFlagParamsReset));
    Command disarm = cmd(CommandKind::Disarm);
    in.tick = ++t;
    in.commands = &disarm;
    sup.step(in, ctl, out);
    REQUIRE(out.acks.items[0].ok);
    REQUIRE(out.mode == Mode::Safe);
    CHECK(has(out, kFlagParamsReset));

    // The first accepted one clears it, from that tick on.
    ParamUpdate good;
    add_number(good, "rate_gain_scale", 0.4);
    Command accepted = set_params_cmd(&good);
    in.tick = ++t;
    in.commands = &accepted;
    in.command_count = 1;
    sup.step(in, ctl, out);
    REQUIRE(out.acks.items[0].ok);
    CHECK_FALSE(has(out, kFlagParamsReset));
    in.commands = nullptr;
    in.command_count = 0;
    in.tick = ++t;
    sup.step(in, ctl, out);
    CHECK_FALSE(has(out, kFlagParamsReset));
}

TEST_CASE("R4.6 PARAMS_RESET is also set for parameters that failed validation, and never for loaded ones")
{
    ScriptedController ctl;
    TickInputs in;
    in.vbat_avg_v = 10.4f;
    TickOutputs out;

    Params bad;
    bad.spinup_throttle = 100;
    bad.hall_expect_throttle = 2047;  // breaks the cross-key rule, so the defaults load
    Supervisor invalid(bad);
    invalid.step(in, ctl, out);
    CHECK(has(out, kFlagParamsReset));

    Supervisor loaded(Params{});
    loaded.step(in, ctl, out);
    CHECK_FALSE(has(out, kFlagParamsReset));
}

TEST_CASE("R4.6 DETUMBLE and SUN_POINT refusals: not in READY, sun not valid, wheel loop not available")
{
    for (Mode m : {Mode::Detumble, Mode::SunPoint}) {
        CAPTURE(static_cast<int>(m));
        SUBCASE("from SAFE, SPINUP, MANUAL, HOLD, and SLEW")
        {
            Rig rig(cascade_params());
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "not in READY");  // SAFE
            rig.tick({cmd(CommandKind::Arm)});
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "not in READY");  // SPINUP
            CHECK(rig.out.mode == Mode::Spinup);
            rig.in.wheel_rpm_safe = rig.sup.params().omega_bias_rpm;
            rig.run(ticks_from_ms(kSpinupDwellMs) + 2);
            REQUIRE(rig.out.mode == Mode::Ready);
            rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
            REQUIRE(rig.out.mode == Mode::Manual);
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "not in READY");  // MANUAL
            CHECK(rig.out.mode == Mode::Manual);
            CHECK(rig.out.events.count == 0);
            rig.tick({mode_cmd(Mode::Ready)});
            rig.tick({mode_cmd(Mode::Hold)});
            REQUIRE(rig.out.mode == Mode::Hold);
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "not in READY");  // HOLD
            CHECK(rig.out.mode == Mode::Hold);
            rig.tick({mode_cmd(Mode::Ready)});
            Command slew = mode_cmd(Mode::Slew);
            slew.psi_ref_deg = 90.0;
            rig.tick({slew});
            REQUIRE(rig.out.mode == Mode::Slew);
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "not in READY");  // SLEW
            CHECK(rig.out.mode == Mode::Slew);
        }
    }
    SUBCASE("SUN_POINT from READY: sun not valid, nothing changes")
    {
        Rig rig(cascade_params());
        rig.arm_to_ready();
        REQUIRE(rig.out.mode == Mode::Ready);
        rig.tick({mode_cmd(Mode::SunPoint)});
        CHECK(rig.last_reason() == "sun not valid");
        CHECK(rig.out.mode == Mode::Ready);
        CHECK(rig.out.events.count == 0);
    }
    SUBCASE("DETUMBLE and SUN_POINT from READY without the table: wheel loop not available")
    {
        for (Mode m : {Mode::Detumble, Mode::SunPoint}) {
            CAPTURE(static_cast<int>(m));
            Rig rig;
            rig.arm_to_ready();
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "wheel loop not available");
            CHECK(rig.out.mode == Mode::Ready);
        }
    }
}
