// Hall bench bypass (approved 2026-10-10): property tests over random
// parameter sets, adversarial controllers, and random command sequences.

#include <random>

#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace harness;

namespace {

ParamUpdate bench_on()
{
    ParamUpdate u;
    add_bool(u, "bench_mode", true);
    return u;
}

ParamUpdate bypass_on()
{
    ParamUpdate u;
    add_bool(u, "hall_installed", false);
    return u;
}

// A random but valid parameter set; manual_throttle_max often far above the
// bypass ceiling so the clamp is what matters.
Params random_params(std::mt19937 &rng)
{
    Params p;
    p.omega_low_rpm = 500.0f + static_cast<float>(rng() % 1001);
    p.omega_bias_rpm = p.omega_low_rpm + 400.0f +
                       static_cast<float>(rng() % static_cast<unsigned>(kOmegaCapMaxRpm - 800.0f - p.omega_low_rpm + 1));
    p.manual_throttle_min = static_cast<uint16_t>(48 + rng() % 1500);
    p.manual_throttle_max = static_cast<uint16_t>(p.manual_throttle_min + 1 + rng() % (2047 - p.manual_throttle_min));
    p.spinup_throttle = static_cast<uint16_t>(48 + rng() % 2000);
    p.hall_expect_throttle = static_cast<uint16_t>(48 + rng() % 2000);
    return p;
}

Command random_command(std::mt19937 &rng, uint32_t seq, const ParamUpdate *update)
{
    static const CommandKind kinds[] = {CommandKind::Arm, CommandKind::Disarm, CommandKind::Estop,
                                        CommandKind::Mode, CommandKind::Manual, CommandKind::Manual,
                                        CommandKind::Manual, CommandKind::Ping, CommandKind::SetParams};
    Command c = cmd(kinds[rng() % (sizeof kinds / sizeof kinds[0])], (rng() % 4) ? kOperator : kViewer, seq);
    c.target_mode = (rng() % 2) ? Mode::Manual : static_cast<Mode>(rng() % 8);
    c.manual_input = static_cast<ManualInput>(rng() % 4);
    c.setpoint_input = (rng() % 2) ? ManualInput::Throttle : static_cast<ManualInput>(rng() % 4);
    c.setpoint = static_cast<float>(static_cast<int>(rng() % 8000) - 1000);
    c.params = update;
    return c;
}

}  // namespace

TEST_CASE("bypass property: throttle never exceeds kBypassThrottleMax while bypassed")
{
    std::mt19937 rng(0xD12EC5u);
    const ParamUpdate bench = bench_on();
    const ParamUpdate bypass = bypass_on();
    long bypassed_armed_ticks = 0;
    long adversarial_ticks = 0;

    for (int trial = 0; trial < 10000; ++trial) {
        Rig rig(random_params(rng));
        rig.in.built_inputs = manual_input_bit(ManualInput::Throttle);
        rig.tick({set_params_cmd(&bench)});
        rig.tick({set_params_cmd(&bypass)});
        REQUIRE_FALSE(rig.sup.params().hall_installed);
        // Arm deliberately, then 60 ticks of random commands against a
        // controller that asks for anything from -1000 to 65535.
        rig.tick({cmd(CommandKind::Arm)});
        for (int i = 0; i < 60; ++i) {
            rig.ctl.throttle_value = static_cast<int>(rng() % 66536) - 1000;
            rig.in.wheel_rpm_safe = static_cast<float>(rng() % 6000);
            std::vector<Command> cmds;
            if (rng() % 3 == 0) cmds.push_back(random_command(rng, static_cast<uint32_t>(i), &bypass));
            rig.tick(cmds);
            if (!rig.sup.params().hall_installed) {
                REQUIRE(rig.out.throttle <= kBypassThrottleMax);
                REQUIRE((rig.out.flags & kFlagHallBypass) != 0);
                if (rig.out.mode != Mode::Safe) {
                    ++bypassed_armed_ticks;
                    if (rig.ctl.throttle_value > kBypassThrottleMax) ++adversarial_ticks;
                }
            }
        }
    }
    // The instrument saw what it had to: many armed, bypassed ticks in which the
    // controller asked for more than the ceiling.
    CHECK(bypassed_armed_ticks > 50000);
    CHECK(adversarial_ticks > 25000);
}

TEST_CASE("bypass property: hall_installed=false is refused outside SAFE and without bench_mode")
{
    std::mt19937 rng(0xB1A5u);
    const ParamUpdate bench = bench_on();
    const ParamUpdate bypass = bypass_on();
    for (int trial = 0; trial < 10000; ++trial) {
        Rig rig(random_params(rng));
        const bool with_bench = (rng() & 1) != 0;
        const bool armed_first = (rng() & 1) != 0;
        if (with_bench) rig.tick({set_params_cmd(&bench)});
        if (armed_first) rig.tick({cmd(CommandKind::Arm)});
        // Random prior traffic must not open a path either.
        for (int i = 0; i < 5; ++i) {
            if (rng() % 2) rig.tick({random_command(rng, 100u + static_cast<uint32_t>(i), &bench)});
        }
        const bool was_safe = rig.out.mode == Mode::Safe && !rig.out.ramping;
        const bool bench_now = rig.sup.params().bench_mode;
        rig.tick({set_params_cmd(&bypass, (rng() % 4) ? kOperator : kViewer)});
        const bool bypassed = !rig.sup.params().hall_installed;
        if (bypassed) {
            REQUIRE(bench_now);
            REQUIRE(rig.out.acks.count >= 1);
        }
        if (!bench_now) REQUIRE_FALSE(bypassed);
        if (!was_safe && rig.out.mode != Mode::Safe) REQUIRE_FALSE(bypassed);
    }
    // And the exact refusals.
    Rig rig;
    rig.tick({set_params_cmd(&bypass)});
    CHECK(rig.last_reason() == "bench_mode required: hall_installed");
    ParamUpdate both;
    add_bool(both, "bench_mode", true);
    add_bool(both, "hall_installed", false);
    rig.tick({set_params_cmd(&both)});
    CHECK(rig.last_reason() == "bench_mode required: hall_installed");  // two deliberate commands, never one
    rig.tick({set_params_cmd(&bench)});
    rig.tick({cmd(CommandKind::Arm)});
    rig.tick({set_params_cmd(&bypass)});
    CHECK(rig.last_reason() == "not in SAFE");
}

TEST_CASE("bypass property: a fresh boot always has hall_installed=true and bench_mode off")
{
    std::mt19937 rng(0xF4E5u);
    for (int trial = 0; trial < 10000; ++trial) {
        Params p = random_params(rng);
        p.bench_mode = (rng() & 1) != 0;
        p.hall_installed = (rng() & 1) != 0;
        Supervisor sup(p, (rng() & 1) != 0);
        REQUIRE(sup.params().hall_installed);
        REQUIRE_FALSE(sup.params().bench_mode);
    }
}

TEST_CASE("bypass: only MANUAL throttle, Hall interlocks suspended, flag and notice shown")
{
    Rig rig;
    const ParamUpdate bench = bench_on();
    const ParamUpdate bypass = bypass_on();
    rig.tick({set_params_cmd(&bench)});
    rig.tick({set_params_cmd(&bypass)});
    REQUIRE(rig.last_ok());
    REQUIRE(rig.out.events.count == 1);
    CHECK(std::string(rig.out.events.items[0].notice) == "HALL_BYPASS");
    CHECK(std::string(rig.out.events.items[0].detail) == "on");
    CHECK((rig.out.flags & kFlagHallBypass) != 0);

    // Open-loop SPINUP: READY after the ramp (spinup_throttle / 200 per s) plus 0.5 s, no speed check.
    rig.in.wheel_rpm_safe = 0.0f;
    rig.in.hall_edge_age_ms = UINT32_MAX;
    rig.tick({cmd(CommandKind::Arm)});
    const uint32_t ramp = (300u * kTickHz + 199u) / 200u;
    rig.run(ramp + ticks_from_ms(kSpinupDwellMs) - 1);
    CHECK(rig.out.mode == Mode::Spinup);
    rig.tick();
    CHECK(rig.out.mode == Mode::Ready);

    // Wheel interlocks are suspended: absurd speeds and silence do not trip.
    rig.in.wheel_rpm_safe = 9000.0f;
    rig.run(500);
    rig.in.wheel_rpm_safe = 0.0f;
    rig.run(500);
    CHECK(rig.out.mode == Mode::Ready);
    CHECK((rig.out.flags & kFlagHallBypass) != 0);

    rig.tick({mode_cmd(Mode::Manual, ManualInput::Wheel)});
    CHECK(rig.last_reason() == "input not available");
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Rate)});
    CHECK(rig.last_reason() == "input not available");
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    CHECK(rig.out.mode == Mode::Manual);
    rig.tick({manual_cmd(ManualInput::Throttle, 2000.0f)});
    CHECK(rig.ctl.last_demand.manual_throttle <= kBypassThrottleMax);

    // Other interlocks stay live in the bypass.
    rig.in.tilt_deg = 45.0f;
    rig.tick();
    CHECK(rig.out.mode == Mode::Safe);
    CHECK(rig.out.throttle == 0);
}

TEST_CASE("bypass: bench_mode cannot be switched off while the Hall is bypassed")
{
    Rig rig;
    const ParamUpdate bench = bench_on();
    const ParamUpdate bypass = bypass_on();
    rig.tick({set_params_cmd(&bench)});
    rig.tick({set_params_cmd(&bypass)});
    ParamUpdate bench_off;
    add_bool(bench_off, "bench_mode", false);
    rig.tick({set_params_cmd(&bench_off)});
    CHECK(rig.last_reason() == "bench_mode required: hall_installed");
    CHECK(rig.sup.params().bench_mode);
}
