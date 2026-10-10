// R3.7: rung 1, the open-loop throttle controller driven through the real
// supervisor, so every number below has already passed the supervisor's clamps
// and interlocks.

#include <cstdint>
#include <cstring>
#include <vector>

#include "direcsun/core/control.hpp"
#include "direcsun/core/protocol.hpp"
#include "direcsun/core/snapshot.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::cmd;
using harness::manual_cmd;
using harness::mode_cmd;
using protocol::encode_tlm;
using protocol::kMaxTlmBytes;

namespace {

struct RungOneRig {
    Supervisor sup;
    OpenLoopThrottleController ctl;
    TickInputs in;
    TickOutputs out;
    uint32_t now = 1000;

    explicit RungOneRig(const Params &p = Params{}) : sup(p)
    {
        in.rate_dps = 0.0f;
        in.wheel_rpm_safe = 0.0f;
        in.wheel_rpm_fast = 0.0f;
        in.hall_edge_age_ms = 10;
        in.vbat_avg_v = 10.4f;
        in.tilt_deg = 0.0f;
        in.built_inputs = kRungOneBuiltInputs;
    }

    const TickOutputs &tick(std::vector<Command> cmds = {})
    {
        ++now;
        in.tick = now;
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        return out;
    }

    // Quiet ticks with an operator ping every 100 ms (the heartbeat).
    void run(uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i) {
            if (i % 50 == 0) {
                tick({cmd(CommandKind::Ping)});
            } else {
                tick();
            }
        }
    }

    void bypass_hall()
    {
        ParamUpdate bench;
        harness::add_bool(bench, "bench_mode", true);
        tick({harness::set_params_cmd(&bench)});
        REQUIRE(out.acks.count == 1);
        REQUIRE(out.acks.items[0].ok);
        ParamUpdate bypass;
        harness::add_bool(bypass, "hall_installed", false);
        tick({harness::set_params_cmd(&bypass)});
        REQUIRE(out.acks.count == 1);
        REQUIRE(out.acks.items[0].ok);
        REQUIRE_FALSE(sup.params().hall_installed);
    }

    void arm_to_ready_with_hall()
    {
        tick({cmd(CommandKind::Arm)});
        REQUIRE(sup.mode() == Mode::Spinup);
        in.wheel_rpm_safe = sup.params().omega_bias_rpm;
        run(ticks_from_ms(kSpinupDwellMs) + 2);
        REQUIRE(sup.mode() == Mode::Ready);
    }

    bool last_ok() const { return out.acks.count != 0 && out.acks.items[out.acks.count - 1].ok; }
    std::string last_reason() const
    {
        return out.acks.count == 0 ? std::string("<no ack>") : std::string(out.acks.items[out.acks.count - 1].reason);
    }
};

}  // namespace

TEST_CASE("rung 1: spin-up ramp arithmetic")
{
    CHECK(spinup_ramp_throttle(0, 300) == 0);
    CHECK(spinup_ramp_throttle(2, 300) == 0);      // 0.8 units
    CHECK(spinup_ramp_throttle(3, 300) == 1);      // 1.2 units
    CHECK(spinup_ramp_throttle(500, 300) == 200);  // 1 s at 200 units/s
    CHECK(spinup_ramp_throttle(750, 300) == 300);  // reaches the target at 1.5 s
    CHECK(spinup_ramp_throttle(751, 300) == 300);  // and holds it
    CHECK(spinup_ramp_throttle(UINT32_MAX, 300) == 300);
    CHECK(spinup_ramp_throttle(100, 0) == 0);
}

TEST_CASE("rung 1: SPINUP ramps at 200 DShot units per second through the supervisor")
{
    RungOneRig r;
    r.tick({cmd(CommandKind::Arm)});  // the arm tick is tick 0 of SPINUP
    REQUIRE(r.sup.mode() == Mode::Spinup);

    std::vector<int> sent;  // final throttle on SPINUP ticks 1..800
    for (uint32_t k = 1; k <= 800; ++k) {
        r.tick(k % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
        REQUIRE(r.sup.mode() == Mode::Spinup);  // wheel reads 0 rpm: never in band
        sent.push_back(r.out.throttle);
    }
    // Below 48 the legal-value pass sends 0; from there the ramp is visible.
    CHECK(sent[100 - 1] == 0);    // 40 units at 0.2 s, below 48
    CHECK(sent[200 - 1] == 80);   // 0.4 s
    CHECK(sent[700 - 1] == 280);  // 1.4 s
    CHECK(sent[750 - 1] == 300);  // 1.5 s, at spinup_throttle
    CHECK(sent[800 - 1] == 300);  // held
    // Slope: 200 units over 500 ticks is 200 units/s.
    CHECK(sent[700 - 1] - sent[200 - 1] == 200);
    // Never above the target on the way up.
    for (int v : sent) CHECK(v <= 300);
}

TEST_CASE("rung 1: READY with the Hall installed needs the wheel within 5 % of bias for 0.5 s")
{
    RungOneRig r;
    r.tick({cmd(CommandKind::Arm)});
    r.in.wheel_rpm_safe = r.sup.params().omega_bias_rpm * 1.06f;  // 6 % fast: out of band
    r.run(ticks_from_ms(kSpinupDwellMs) + 50);
    CHECK(r.sup.mode() == Mode::Spinup);

    r.in.wheel_rpm_safe = r.sup.params().omega_bias_rpm * 1.04f;  // 4 % fast: in band
    r.run(ticks_from_ms(kSpinupDwellMs) - 2);
    CHECK(r.sup.mode() == Mode::Spinup);
    r.run(3);
    CHECK(r.sup.mode() == Mode::Ready);

    // READY holds spinup_throttle until the rate loop of R3.9 exists.
    r.run(100);
    CHECK(r.out.throttle == r.sup.params().spinup_throttle);
}

TEST_CASE("rung 1: READY with the Hall bypassed follows the ramp plus 0.5 s")
{
    RungOneRig r;
    r.bypass_hall();
    r.tick({cmd(CommandKind::Arm)});
    REQUIRE(r.sup.mode() == Mode::Spinup);
    const uint32_t ramp_ticks = 750;  // 300 units at 200 units/s
    const uint32_t ready_after = ramp_ticks + ticks_from_ms(kSpinupDwellMs);
    r.run(ready_after - 1);
    CHECK(r.sup.mode() == Mode::Spinup);
    r.run(1);
    CHECK(r.sup.mode() == Mode::Ready);
    r.run(10);
    CHECK(r.out.throttle == 300);
    CHECK((r.out.flags & kFlagHallBypass) != 0);
}

TEST_CASE("rung 1: entering MANUAL throttle starts from the current throttle (no step)")
{
    RungOneRig r;
    r.arm_to_ready_with_hall();
    r.run(10);
    const int before = r.out.throttle;
    REQUIRE(before == 300);
    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(r.last_ok());
    CHECK(r.sup.mode() == Mode::Manual);
    CHECK(r.out.throttle == before);
    r.run(20);
    CHECK(r.out.throttle == before);
}

TEST_CASE("rung 1: MANUAL throttle clamps to the band edges")
{
    RungOneRig r;
    r.arm_to_ready_with_hall();
    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(r.sup.mode() == Mode::Manual);
    const uint32_t gap = ticks_from_ms(kManualMinIntervalMs) + 1;

    r.tick({manual_cmd(ManualInput::Throttle, 50.0f)});
    REQUIRE(r.last_ok());
    CHECK(r.out.throttle == 150);  // manual_throttle_min
    r.run(gap);
    r.tick({manual_cmd(ManualInput::Throttle, 1000.0f)});
    REQUIRE(r.last_ok());
    CHECK(r.out.throttle == 400);  // manual_throttle_max
    r.run(gap);
    r.tick({manual_cmd(ManualInput::Throttle, 250.0f)});
    REQUIRE(r.last_ok());
    CHECK(r.out.throttle == 250);  // inside the band: passed through
}

TEST_CASE("rung 1: with the Hall bypassed MANUAL throttle never exceeds kBypassThrottleMax")
{
    Params p;
    p.manual_throttle_max = 900;  // a band wider than the bypass ceiling
    RungOneRig r(p);
    r.bypass_hall();
    r.tick({cmd(CommandKind::Arm)});
    r.run(750 + ticks_from_ms(kSpinupDwellMs) + 2);
    REQUIRE(r.sup.mode() == Mode::Ready);
    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(r.sup.mode() == Mode::Manual);
    r.tick({manual_cmd(ManualInput::Throttle, 900.0f)});
    REQUIRE(r.last_ok());
    CHECK(r.out.throttle == kBypassThrottleMax);
    int max_seen = 0;
    for (int i = 0; i < 2000; ++i) {
        r.tick(i % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
        if (r.out.throttle > max_seen) max_seen = r.out.throttle;
    }
    CHECK(max_seen == kBypassThrottleMax);
}

TEST_CASE("rung 1: a stale MANUAL throttle setpoint holds its value and flags MANUAL_STALE")
{
    RungOneRig r;
    r.arm_to_ready_with_hall();
    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    r.tick({manual_cmd(ManualInput::Throttle, 350.0f)});
    REQUIRE(r.last_ok());
    REQUIRE(r.out.throttle == 350);
    r.run(ticks_from_ms(kManualStaleMs) + 50);  // pings keep the heartbeat alive
    CHECK(r.sup.mode() == Mode::Manual);
    CHECK((r.out.flags & kFlagManualStale) != 0);
    CHECK(r.out.throttle == 350);  // held, not dropped
}

TEST_CASE("rung 1: a setpoint for another input, or an unbuilt input, is refused")
{
    RungOneRig r;
    r.arm_to_ready_with_hall();
    r.tick({mode_cmd(Mode::Manual, ManualInput::Wheel)});
    CHECK_FALSE(r.last_ok());
    CHECK(r.last_reason() == "input not available");
    CHECK(r.sup.mode() == Mode::Ready);
    r.tick({mode_cmd(Mode::Manual, ManualInput::Rate)});
    CHECK(r.last_reason() == "input not available");

    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(r.sup.mode() == Mode::Manual);
    const int held = r.out.throttle;
    r.tick({manual_cmd(ManualInput::Wheel, 3000.0f)});
    CHECK_FALSE(r.last_ok());
    CHECK(r.last_reason() == "input mismatch");
    CHECK(r.out.throttle == held);
    r.run(ticks_from_ms(kManualMinIntervalMs) + 1);
    r.tick({manual_cmd(ManualInput::Rate, 30.0f)});
    CHECK(r.last_reason() == "input mismatch");
    CHECK(r.out.throttle == held);
}

TEST_CASE("rung 1: the reported throttle band is the band the supervisor enforces")
{
    Params p;  // defaults: 150..400, Hall installed
    ThrottleBand b = manual_throttle_band(p);
    CHECK(b.lo == 150);
    CHECK(b.hi == 400);

    p.manual_throttle_max = 900;
    b = manual_throttle_band(p);
    CHECK(b.hi == 900);  // Hall installed: no bypass ceiling
    p.hall_installed = false;
    b = manual_throttle_band(p);
    CHECK(b.lo == 150);
    CHECK(b.hi == kBypassThrottleMax);
    p.manual_throttle_min = 450;  // a minimum above the ceiling collapses onto it
    b = manual_throttle_band(p);
    CHECK(b.lo == kBypassThrottleMax);
    CHECK(b.hi == kBypassThrottleMax);

    // The supervisor's clamp agrees with the band at both edges for every
    // request from -100 to 2200 (Hall installed, wide band).
    Params wide;
    wide.manual_throttle_max = 900;
    RungOneRig r(wide);
    r.arm_to_ready_with_hall();
    r.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(r.sup.mode() == Mode::Manual);
    const ThrottleBand wb = manual_throttle_band(r.sup.params());
    for (int req = -100; req <= 2200; req += 50) {
        r.run(ticks_from_ms(kManualMinIntervalMs) + 1);
        r.tick({manual_cmd(ManualInput::Throttle, static_cast<float>(req))});
        REQUIRE(r.last_ok());
        CHECK(r.out.throttle >= wb.lo);
        CHECK(r.out.throttle <= wb.hi);
    }
}

TEST_CASE("rung 1: tlm carries throttle_band, or null before the band is known")
{
    char buf[kMaxTlmBytes];
    TelemetrySnapshot s;  // fresh snapshot: band not filled yet
    REQUIRE(encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"throttle_band\":null") != nullptr);

    s.throttle_band_lo = 150;
    s.throttle_band_hi = 400;
    REQUIRE(encode_tlm(s, 2, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"throttle_band\":[150,400]") != nullptr);

    s.throttle_band_lo = 30;  // below DShot throttle: never reported as a band
    REQUIRE(encode_tlm(s, 3, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"throttle_band\":null") != nullptr);
}

