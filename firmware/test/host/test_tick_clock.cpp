// Missed control ticks (finding V-66). The control task runs once per 2 ms
// timer alarm; a tick that overruns misses alarms, and its next wait returns
// them all at once. TickClock turns those alarm counts into the supervisor's
// time, exactly as control_task.cpp does, so every duration in the supervisor
// is wall time. Each test keeps its own count of alarms as the independent
// measure of wall time and checks the supervisor against it.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "direcsun/core/control.hpp"
#include "direcsun/core/controllers.hpp"
#include "direcsun/core/dshot.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::add_bool;
using harness::cmd;
using harness::manual_cmd;
using harness::mode_cmd;
using harness::set_params_cmd;

namespace {

// Alarms taken per executed tick, repeated: 14 alarms in 8 ticks, so 6 missed.
// The longest run of missed alarms is 3, far from CONTROL_STALL's 25.
constexpr uint32_t kAlarmPattern[] = {1, 3, 1, 1, 2, 1, 4, 1};
constexpr std::size_t kPatternLength = sizeof kAlarmPattern / sizeof kAlarmPattern[0];
constexpr uint32_t kMostAlarmsPerTick = 4;

// The supervisor driven through a TickClock, as the control task drives it.
struct ClockRig {
    Supervisor sup;
    ThrottleController &ctl;
    TickClock clock;
    TickInputs in;
    TickOutputs out;
    std::size_t step_index = 0;
    uint32_t wall = 0;        // alarms so far: the test's own clock, 2 ms each
    uint32_t prev_wall = 0;   // wall before the latest tick
    uint32_t last_ping_wall = 0;
    bool keepalive = true;

    ClockRig(ThrottleController &controller, const Params &p = Params{}) : sup(p), ctl(controller)
    {
        in.hall_edge_age_ms = 10;
        in.vbat_avg_v = 10.4f;
        in.built_inputs = kRungOneBuiltInputs;
    }

    // One executed tick after `alarms` timer alarms. The operator pings every
    // 100 ms of wall time while keepalive is on.
    const TickOutputs &tick(std::vector<Command> cmds = {})
    {
        const uint32_t alarms = kAlarmPattern[step_index++ % kPatternLength];
        prev_wall = wall;
        wall += alarms;
        if (keepalive && wall - last_ping_wall >= 50) {
            cmds.push_back(cmd(CommandKind::Ping));
            last_ping_wall = wall;
        }
        const uint32_t missed = clock.advance(alarms);
        REQUIRE(missed == alarms - 1);
        in.tick = clock.now();
        in.missed_tick_streak = clock.missed_streak();
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        REQUIRE(dshot_value_is_sendable(static_cast<uint16_t>(out.throttle)));
        return out;
    }

    void run_until(bool (*done)(const ClockRig &), uint32_t max_ticks)
    {
        for (uint32_t i = 0; i < max_ticks && !done(*this); ++i) tick();
        REQUIRE(done(*this));
    }
};

}  // namespace

TEST_CASE("missed ticks: TickClock counts alarms and the missed streak")
{
    TickClock clock;
    CHECK(clock.advance(1) == 0);
    CHECK(clock.now() == 1);
    CHECK(clock.advance(3) == 2);
    CHECK(clock.now() == 4);
    CHECK(clock.missed_streak() == 2);
    CHECK(clock.advance(2) == 1);
    CHECK(clock.missed_streak() == 3);  // consecutive missed alarms add up
    CHECK(clock.advance(1) == 0);
    CHECK(clock.missed_streak() == 0);  // a tick on time ends the streak
    CHECK(clock.now() == 7);
    CHECK(clock.advance(0) == 0);       // an executed tick is at least one alarm
    CHECK(clock.now() == 8);
}

// The first spin's configuration: bypass on, real rung-1 controller, MANUAL at
// 400. The operator stops pinging; the heartbeat must expire 1.0 s of alarms
// later, and the ramp must reach DShot 0 5.0 s of alarms after it starts.
TEST_CASE("missed ticks: HEARTBEAT_LOST trips at 1.0 s and the ramp ends at 5.0 s of wall time")
{
    OpenLoopThrottleController ctl;
    ClockRig r(ctl);
    r.in.hall_edge_age_ms = UINT32_MAX;
    ParamUpdate bench;
    add_bool(bench, "bench_mode", true);
    ParamUpdate bypass;
    add_bool(bypass, "hall_installed", false);
    r.tick({set_params_cmd(&bench)});
    r.tick({set_params_cmd(&bypass)});
    REQUIRE_FALSE(r.sup.params().hall_installed);
    r.tick({cmd(CommandKind::Arm)});
    REQUIRE(r.sup.mode() == Mode::Spinup);
    r.run_until([](const ClockRig &x) { return x.sup.mode() == Mode::Ready; }, 2000);
    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(r.sup.mode() == Mode::Manual);
    for (int i = 0; i < 30; ++i) r.tick();
    r.tick({manual_cmd(ManualInput::Throttle, 400.0f)});
    REQUIRE(r.out.acks.items[0].ok);
    for (int i = 0; i < 30; ++i) r.tick();
    REQUIRE(r.out.throttle == 400);

    // The operator's last ping, then silence.
    r.tick({cmd(CommandKind::Ping)});
    const uint32_t ping_wall = r.wall;
    r.keepalive = false;
    r.run_until([](const ClockRig &x) { return x.out.mode == Mode::Safe; }, 2000);
    const uint32_t timeout = ticks_from_ms(kHeartbeatTimeoutMs);
    CHECK((r.out.latched & interlock_bit(Interlock::HeartbeatLost)) != 0);
    // It trips on the first tick at or after 1.0 s of alarms, not before.
    CHECK(r.wall - ping_wall >= timeout);
    CHECK(r.prev_wall - ping_wall < timeout);
    CHECK(r.wall - ping_wall < timeout + kMostAlarmsPerTick);

    // The ramp: throttle_0 x (1 - t / 5 s) with t in alarms, DShot 0 below 48.
    REQUIRE(r.out.ramping);
    REQUIRE(r.out.throttle == 400);
    const uint32_t ramp_wall = r.wall;
    const uint32_t total = ticks_from_ms(kRampDownMs);
    uint32_t first_zero = 0;
    while (r.out.ramping) {
        r.tick();
        const uint32_t t = r.wall - ramp_wall;
        const long raw = t >= total ? 0 : 400L * static_cast<long>(total - t) / static_cast<long>(total);
        REQUIRE(r.out.throttle == (raw < 48 ? 0 : static_cast<int>(raw)));
        if (r.out.throttle == 0 && first_zero == 0) first_zero = t;
        REQUIRE(r.wall - ramp_wall < total + kMostAlarmsPerTick);
    }
    // It ends on the first tick at or after 5.0 s of alarms.
    CHECK(r.wall - ramp_wall >= total);
    CHECK(r.prev_wall - ramp_wall < total);
    // DShot 0 from 4.4 s (400 x 300 / 2500 = 48 is the last running value).
    CHECK(first_zero >= 2201);
    CHECK(first_zero < 2201 + kMostAlarmsPerTick);
    CHECK(r.out.throttle == 0);
    CHECK(r.out.mode == Mode::Safe);
}

// The duration counters of the supervisor (HALL_SILENT, UNDERSPEED, the SPINUP
// dwell) add the alarms each tick stands for. A tick that missed alarms counts
// the condition it sees for the whole interval since the tick before, so a trip
// can come up to one tick early, never late.
TEST_CASE("missed ticks: HALL_SILENT UNDERSPEED and the SPINUP dwell run on wall time")
{
    SUBCASE("HALL_SILENT after 500 ms of alarms")
    {
        harness::ScriptedController ctl;
        ctl.throttle_value = 300;  // above hall_expect_throttle (200) from the arm tick on
        ClockRig r(ctl);
        r.in.hall_edge_age_ms = UINT32_MAX;
        r.tick();
        const uint32_t before_high = r.wall;  // the last tick with the throttle at 0
        r.tick({cmd(CommandKind::Arm)});
        REQUIRE(r.out.mode == Mode::Spinup);
        r.run_until([](const ClockRig &x) { return x.out.mode == Mode::Safe; }, 2000);
        CHECK((r.out.latched & interlock_bit(Interlock::HallSilent)) != 0);
        const uint32_t need = ticks_from_ms(kHallSilentMs);
        CHECK(r.wall - before_high >= need);
        CHECK(r.prev_wall - before_high < need);
    }
    SUBCASE("UNDERSPEED after 200 ms of alarms")
    {
        harness::ScriptedController ctl;
        ctl.throttle_value = 300;
        ClockRig r(ctl);
        r.tick({cmd(CommandKind::Arm)});
        r.in.wheel_rpm_safe = r.sup.params().omega_bias_rpm;
        r.run_until([](const ClockRig &x) { return x.out.mode == Mode::Ready; }, 2000);
        r.tick();
        const uint32_t before_low = r.wall;
        r.in.wheel_rpm_safe = r.sup.params().omega_low_rpm - 1.0f;
        r.run_until([](const ClockRig &x) { return x.out.mode == Mode::Safe; }, 2000);
        CHECK((r.out.latched & interlock_bit(Interlock::Underspeed)) != 0);
        const uint32_t need = ticks_from_ms(kUnderspeedMs);
        CHECK(r.wall - before_low >= need);
        CHECK(r.prev_wall - before_low < need);
    }
    SUBCASE("SPINUP to READY after 500 ms of alarms in band")
    {
        harness::ScriptedController ctl;
        ctl.throttle_value = 300;
        ClockRig r(ctl);
        r.tick({cmd(CommandKind::Arm)});
        r.tick();
        const uint32_t before_band = r.wall;
        r.in.wheel_rpm_safe = r.sup.params().omega_bias_rpm;
        r.run_until([](const ClockRig &x) { return x.out.mode == Mode::Ready; }, 2000);
        const uint32_t need = ticks_from_ms(kSpinupDwellMs);
        CHECK(r.wall - before_band >= need);
        CHECK(r.prev_wall - before_band < need);
    }
}

namespace {

// A box whose T-5 feed-forward table is set, so HOLD and SLEW are available.
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

bool mode_event(const TickOutputs &out, const char *cause)
{
    for (std::size_t i = 0; i < out.events.count; ++i) {
        if (out.events.items[i].kind == Event::Kind::Mode && std::strcmp(out.events.items[i].cause, cause) == 0) {
            return true;
        }
    }
    return false;
}

// Arms a characterised box to READY through the clock, then sends `mode SLEW`
// to 90 deg. Returns the wall time of the tick that entered SLEW.
uint32_t enter_slew(ClockRig &r)
{
    r.tick({cmd(CommandKind::Arm)});
    r.in.wheel_rpm_safe = r.sup.params().omega_bias_rpm;
    r.run_until([](const ClockRig &x) { return x.out.mode == Mode::Ready; }, 2000);
    Command slew = mode_cmd(Mode::Slew);
    slew.psi_ref_deg = 90.0f;
    r.tick({slew});
    REQUIRE(r.out.mode == Mode::Slew);
    return r.wall;
}

}  // namespace

// R4.3's SLEW is timed like the rest of the supervisor: the 0.5 s settle dwell
// is a duration counter that adds the alarms each tick stands for, and the 6 s
// timeout and the reference profile run on the clock.
TEST_CASE("missed ticks: the SLEW settle dwell and timeout run on wall time")
{
    SUBCASE("slew_complete after 500 ms of alarms on target")
    {
        harness::ScriptedController ctl;
        ctl.throttle_value = 300;
        ClockRig r(ctl, characterised());
        enter_slew(r);
        // The box stays off target until the reference stands at 90 deg, then
        // is on target from the next tick on.
        r.run_until([](const ClockRig &x) { return x.out.psi_ref_deg >= 90.0 - 1e-9; }, 2000);
        REQUIRE(r.out.mode == Mode::Slew);
        const uint32_t before_on = r.wall;  // the last tick off target
        r.in.heading_deg = 90.0;
        r.run_until([](const ClockRig &x) { return x.out.mode != Mode::Slew; }, 2000);
        CHECK(mode_event(r.out, "slew_complete"));
        CHECK(r.out.mode == Mode::Ready);
        const uint32_t need = ticks_from_ms(kSlewSettleMs);
        CHECK(r.wall - before_on >= need);
        CHECK(r.prev_wall - before_on < need);
    }
    SUBCASE("SLEW_TIMEOUT 6.0 s of alarms after entry")
    {
        harness::ScriptedController ctl;
        ctl.throttle_value = 300;
        ClockRig r(ctl, characterised());
        const uint32_t entry = enter_slew(r);  // the box never turns
        r.run_until([](const ClockRig &x) { return x.out.mode != Mode::Slew; }, 4000);
        CHECK(mode_event(r.out, "SLEW_TIMEOUT"));
        const uint32_t need = ticks_from_ms(kSlewTimeoutMs);
        CHECK(r.wall - entry >= need);
        CHECK(r.prev_wall - entry < need);
    }
}
