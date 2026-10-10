// R3.8: the wheel-speed loop (feed-forward and PI with conditional
// integration), the cascade controller that runs it, the bench wheel step, and
// the bench-only overrides. Closed-loop checks use a first-order wheel plant
// whose steady map is exactly the feed-forward table, so the loop, not the
// fixture, is what the numbers measure.

#include <cmath>
#include <cstdint>
#include <vector>

#include "direcsun/core/control.hpp"
#include "direcsun/core/controllers.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::add_bool;
using harness::add_number;
using harness::cmd;
using harness::mode_cmd;
using harness::set_params_cmd;

namespace {

constexpr float kDt = 1.0f / static_cast<float>(kTickHz);

// The test wheel: steady speed = slope x (throttle - 48), first-order lag tau.
// slope 2.5 rpm per DShot unit puts 2047 at about 5000 rpm (920 KV at 10 V is
// about 9200 rpm unloaded); tau 0.8 s is a heavy wheel [A, for the fixture].
struct WheelPlant {
    float slope = 2.5f;
    float tau_s = 0.8f;
    float rpm = 0.0f;

    float steady_rpm(int throttle) const { return throttle <= 48 ? 0.0f : slope * static_cast<float>(throttle - 48); }
    void step(int throttle, float dt)
    {
        rpm += (steady_rpm(throttle) - rpm) / tau_s * dt;
        if (rpm < 0.0f) rpm = 0.0f;
    }
};

// Feed-forward and gains matched to WheelPlant: 8 points from 500 to 4000 rpm,
// throttle = 48 + rpm / 2.5 (integers, as the box requires).
Params plant_params()
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

struct StepResult {
    float overshoot = 0.0f;     // fraction of the step size
    float settle_s = 1e9f;      // last entry into the +-10 % band
    std::vector<float> throttle;
};

// Drives WheelSpeedPi alone against the plant from a steady start.
StepResult pi_step(float from_rpm, float to_rpm, float seconds)
{
    const Params p = plant_params();
    WheelPlant plant;
    plant.rpm = from_rpm;
    WheelSpeedPi pi;
    StepResult r;
    const float step = to_rpm - from_rpm;
    float peak = from_rpm;
    const int n = static_cast<int>(seconds / kDt);
    for (int k = 0; k < n; ++k) {
        const int u = static_cast<int>(std::lround(pi.step(to_rpm, plant.rpm, kDt, p)));
        r.throttle.push_back(static_cast<float>(u));
        plant.step(u, kDt);
        if (plant.rpm > peak) peak = plant.rpm;
        if (std::fabs(plant.rpm - to_rpm) > 0.1f * std::fabs(step)) r.settle_s = static_cast<float>(k + 1) * kDt;
    }
    r.overshoot = (peak - to_rpm) / step;
    return r;
}

Command wheel_step_cmd(float delta, uint32_t seq = 1)
{
    Command c = cmd(CommandKind::WheelStep, harness::kOperator, seq);
    c.delta_rpm = delta;
    return c;
}

// Supervisor + the real cascade controller + the test wheel, closed loop.
struct CascadeRig {
    Supervisor sup;
    CascadeController ctl;
    WheelPlant plant;
    TickInputs in;
    TickOutputs out;
    uint32_t now = 5000;

    explicit CascadeRig(const Params &p) : sup(p)
    {
        in.rate_dps = 0.0f;
        in.hall_edge_age_ms = 5;
        in.vbat_avg_v = 10.4f;
        in.tilt_deg = 0.0f;
        in.built_inputs = kRungOneBuiltInputs;
    }

    const TickOutputs &tick(std::vector<Command> cmds = {})
    {
        ++now;
        in.tick = now;
        in.wheel_rpm_safe = plant.rpm;
        in.wheel_rpm_fast = plant.rpm;
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        plant.step(out.throttle, kDt);
        return out;
    }

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

    void bench_on()
    {
        ParamUpdate u;
        add_bool(u, "bench_mode", true);
        tick({set_params_cmd(&u)});
        REQUIRE(sup.params().bench_mode);
    }

    // Arm and run the closed-loop spin-up until READY (at most 15 s).
    void arm_to_ready()
    {
        tick({cmd(CommandKind::Arm)});
        for (int i = 0; i < 7500 && out.mode != Mode::Ready; ++i) {
            if (i % 50 == 0) {
                tick({cmd(CommandKind::Ping)});
            } else {
                tick();
            }
        }
        REQUIRE(out.mode == Mode::Ready);
    }

    std::string last_reason() const
    {
        return out.acks.count == 0 ? std::string("<no ack>") : std::string(out.acks.items[out.acks.count - 1].reason);
    }
    bool last_ok() const { return out.acks.count != 0 && out.acks.items[out.acks.count - 1].ok; }
};

}  // namespace

TEST_CASE("R3.8 feed-forward: linear between points, held at both ends, monotonic")
{
    const Params p = plant_params();
    CHECK(ff_throttle(p, 500.0f) == doctest::Approx(248.0f));
    CHECK(ff_throttle(p, 750.0f) == doctest::Approx(348.0f));
    CHECK(ff_throttle(p, 4000.0f) == doctest::Approx(1648.0f));
    CHECK(ff_throttle(p, 0.0f) == doctest::Approx(248.0f));      // below the table: first point
    CHECK(ff_throttle(p, -100.0f) == doctest::Approx(248.0f));
    CHECK(ff_throttle(p, 9000.0f) == doctest::Approx(1648.0f));  // above: last point
    CHECK(ff_throttle(p, NAN) == doctest::Approx(248.0f));
    float previous = -1.0f;
    for (float rpm = 0.0f; rpm <= 5000.0f; rpm += 7.0f) {
        const float t = ff_throttle(p, rpm);
        CHECK(t >= previous);
        previous = t;
    }
    Params none;
    CHECK(ff_throttle(none, 2400.0f) == 0.0f);
}

TEST_CASE("R3.8 wheel loop availability needs the Hall and the T-5 table")
{
    Params p = plant_params();
    CHECK(wheel_loop_available(p));
    p.hall_installed = false;
    CHECK_FALSE(wheel_loop_available(p));
    Params no_table;
    CHECK_FALSE(wheel_loop_available(no_table));
}

TEST_CASE("R3.8 PI gains place the crossover at the requested bandwidth")
{
    const WheelPiGains g = wheel_pi_gains(2.5f, 0.8f, 2.0f);
    CHECK(g.kp == doctest::Approx(2.0 * 3.14159265 * 2.0 * 0.8 / 2.5).epsilon(1e-5));
    CHECK(g.ki == doctest::Approx(2.0 * 3.14159265 * 2.0 / 2.5).epsilon(1e-5));
    CHECK(g.kp / g.ki == doctest::Approx(0.8f));  // Ti equals the plant tau
    const WheelPiGains bad = wheel_pi_gains(0.0f, 0.8f, 2.0f);
    CHECK(bad.kp == 0.0f);
    CHECK(bad.ki == 0.0f);
}

TEST_CASE("R3.8 PI is off below omega_low: feed-forward only, integrator cleared")
{
    const Params p = plant_params();
    WheelSpeedPi pi;
    pi.reset(100.0f);
    const float u = pi.step(2400.0f, 700.0f, kDt, p);
    CHECK_FALSE(pi.closed());
    CHECK(u == doctest::Approx(ff_throttle(p, 2400.0f)));
    CHECK(pi.integrator() == 0.0f);
    const float u2 = pi.step(2400.0f, 900.0f, kDt, p);
    CHECK(pi.closed());
    CHECK(u2 > ff_throttle(p, 2400.0f));  // P term adds on a positive error
}

TEST_CASE("R3.8 anti-windup holds the integrator while saturated and recovers within 1 s")
{
    const Params p = plant_params();
    WheelSpeedPi pi;
    // A wheel held at 1000 rpm with the reference at 3800: the throttle sits at
    // 2047 and the error is positive, so the integrator must not move.
    for (int k = 0; k < 1000; ++k) {
        const float u = pi.step(3800.0f, 1000.0f, kDt, p);
        CHECK(u == doctest::Approx(2047.0f));
    }
    CHECK(pi.integrator() == 0.0f);

    // Released at 3790 rpm: the loop leaves saturation and settles near the
    // feed-forward within 1 s.
    WheelPlant plant;
    plant.rpm = 3790.0f;
    float last = 0.0f;
    for (int k = 0; k < static_cast<int>(1.0f / kDt); ++k) {
        const int u = static_cast<int>(std::lround(pi.step(3800.0f, plant.rpm, kDt, p)));
        plant.step(u, kDt);
        last = static_cast<float>(u);
    }
    CHECK(last < 2047.0f);
    CHECK(std::fabs(plant.rpm - 3800.0f) < 0.02f * 3800.0f);
}

TEST_CASE("R3.8 wheel step 2400 to 2700 settles within 10 % in under 0.5 s, under 20 % overshoot")
{
    const StepResult r = pi_step(2400.0f, 2700.0f, 3.0f);
    MESSAGE("overshoot " << r.overshoot * 100.0f << " %, settle " << r.settle_s << " s");
    CHECK(r.settle_s < 0.5f);
    CHECK(r.overshoot < 0.20f);
}

TEST_CASE("R3.8 positive control: a saturating step overshoots under 10 % with anti-windup")
{
    // 800 to 3800 rpm drives the throttle to 2047 for about 0.7 s. With
    // conditional integration the overshoot stays small; the
    // DIRECSUN_MUTANT_NO_WHEEL_ANTIWINDUP build integrates through the
    // saturation and must fail this check (tools/host-test.sh --selftest), with
    // the overshoot it reached in the failure message.
    const StepResult r = pi_step(800.0f, 3800.0f, 6.0f);
    bool saturated = false;
    for (float u : r.throttle) saturated = saturated || u >= 2047.0f;
    REQUIRE(saturated);  // otherwise this step could not show windup at all
    MESSAGE("saturating step overshoot " << r.overshoot * 100.0f << " %");
    CHECK(r.overshoot < 0.10f);
}

TEST_CASE("R3.8 cascade without the T-5 table is exactly rung 1")
{
    // Same session through both controllers: every throttle must match.
    Params p;  // no ff_table
    Supervisor sup_a(p), sup_b(p);
    OpenLoopThrottleController open_loop;
    CascadeController cascade;
    TickInputs in;
    in.hall_edge_age_ms = 5;
    in.vbat_avg_v = 10.4f;
    in.built_inputs = kRungOneBuiltInputs;
    TickOutputs out_a, out_b;
    uint32_t now = 100;
    auto step_both = [&](std::vector<Command> cmds, float wheel) {
        ++now;
        in.tick = now;
        in.wheel_rpm_safe = wheel;
        in.wheel_rpm_fast = wheel;
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup_a.step(in, open_loop, out_a);
        sup_b.step(in, cascade, out_b);
        CHECK(out_a.throttle == out_b.throttle);
        CHECK(out_a.mode == out_b.mode);
    };
    step_both({cmd(CommandKind::Arm)}, 0.0f);
    for (int i = 0; i < 1500; ++i) step_both(i % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{}, 2400.0f);
    REQUIRE(out_b.mode == Mode::Ready);
    step_both({mode_cmd(Mode::Manual, ManualInput::Throttle)}, 2400.0f);
    for (int i = 0; i < 200; ++i) step_both({}, 2400.0f);
    CHECK(cascade.wheel_ref_rpm() == 0.0f);
}

TEST_CASE("R3.8 closed-loop SPINUP ramps omega_ref at 400 rpm/s and reaches READY at bias")
{
    CascadeRig rig(plant_params());
    rig.tick({cmd(CommandKind::Arm)});
    REQUIRE(rig.out.mode == Mode::Spinup);
    rig.run(250);  // 0.5 s
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(200.0f).epsilon(0.02));
    rig.run(2250);  // 5 s in total
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(2000.0f).epsilon(0.02));
    for (int i = 0; i < 2500 && rig.out.mode != Mode::Ready; ++i) rig.run(1);
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(2400.0f));
    rig.run(1000);
    CHECK(rig.plant.rpm == doctest::Approx(2400.0f).epsilon(0.01));
}

TEST_CASE("R3.8 wheel_step is bench-only, READY-only, needs the loop, and holds until mode READY")
{
    SUBCASE("refused without bench mode")
    {
        CascadeRig rig(plant_params());
        rig.arm_to_ready();
        rig.tick({wheel_step_cmd(300.0f)});
        CHECK(rig.last_reason() == "bench_mode required");
    }
    SUBCASE("refused outside READY")
    {
        CascadeRig rig(plant_params());
        rig.bench_on();
        rig.tick({wheel_step_cmd(300.0f)});
        CHECK(rig.last_reason() == "not in READY");
    }
    SUBCASE("refused while the loop cannot run (no T-5 table)")
    {
        CascadeRig rig(Params{});
        rig.bench_on();
        rig.tick({cmd(CommandKind::Arm)});
        rig.plant.rpm = 2400.0f;
        rig.in.wheel_rpm_safe = 2400.0f;
        for (int i = 0; i < 1500 && rig.out.mode != Mode::Ready; ++i) {
            rig.plant.rpm = 2400.0f;  // hold the fixture at bias: open-loop spin-up
            rig.run(1);
        }
        REQUIRE(rig.out.mode == Mode::Ready);
        rig.tick({wheel_step_cmd(300.0f)});
        CHECK(rig.last_reason() == "wheel loop not available");
    }
    SUBCASE("accepted: omega_ref steps, WHEEL_STEP flag until mode READY")
    {
        CascadeRig rig(plant_params());
        rig.bench_on();
        rig.arm_to_ready();
        rig.tick({wheel_step_cmd(300.0f)});
        CHECK(rig.last_ok());
        rig.run(1);
        CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(2700.0f));
        CHECK((rig.out.flags & kFlagWheelStep) != 0);
        rig.run(1500);
        CHECK(rig.plant.rpm == doctest::Approx(2700.0f).epsilon(0.01));
        rig.tick({mode_cmd(Mode::Ready)});
        CHECK(rig.last_ok());
        CHECK((rig.out.flags & kFlagWheelStep) == 0);
        CHECK(rig.out.mode == Mode::Ready);
    }
    SUBCASE("a step past the cap is clamped to it")
    {
        Params p = plant_params();
        p.omega_bias_rpm = 3500.0f;
        CascadeRig rig(p);
        rig.bench_on();
        rig.arm_to_ready();
        rig.tick({wheel_step_cmd(500.0f)});
        CHECK(rig.last_ok());
        rig.run(2000);
        CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(4000.0f));
        CHECK(rig.plant.rpm <= 4100.0f);
        CHECK(rig.out.mode == Mode::Ready);
    }
}

TEST_CASE("R3.8 bench overrides: bench mode only, volatile, cleared with bench mode")
{
    SUBCASE("refused without bench mode, named")
    {
        harness::Rig rig;
        ParamUpdate u;
        add_number(u, "bench_cap_rpm", 2500.0);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_reason() == "bench_mode required: bench_cap_rpm");
        ParamUpdate v;
        add_number(v, "bench_underspeed_rpm", 300.0);
        rig.tick({set_params_cmd(&v)});
        CHECK(rig.last_reason() == "bench_mode required: bench_underspeed_rpm");
    }
    SUBCASE("ranges: underspeed below omega_low, cap between bias and cap")
    {
        Params p;
        p.bench_mode = true;
        p.bench_underspeed_rpm = 300.0f;
        p.bench_cap_rpm = 2500.0f;
        CHECK(validate_params(p).ok);
        p.bench_underspeed_rpm = 900.0f;  // above omega_low 800
        CHECK_FALSE(validate_params(p).ok);
        p.bench_underspeed_rpm = 300.0f;
        p.bench_cap_rpm = 4100.0f;  // would raise the cap
        CHECK_FALSE(validate_params(p).ok);
        p.bench_cap_rpm = 2300.0f;  // below the bias
        CHECK_FALSE(validate_params(p).ok);
    }
    SUBCASE("turning bench mode off clears them; naming one in the same update is refused")
    {
        harness::Rig rig;
        ParamUpdate on;
        add_bool(on, "bench_mode", true);
        rig.tick({set_params_cmd(&on)});
        ParamUpdate set;
        add_number(set, "bench_cap_rpm", 2500.0);
        add_number(set, "bench_underspeed_rpm", 300.0);
        rig.tick({set_params_cmd(&set)});
        REQUIRE(rig.last_ok());
        CHECK(effective_cap_rpm(rig.sup.params()) == doctest::Approx(2500.0f));
        CHECK(effective_underspeed_rpm(rig.sup.params()) == doctest::Approx(300.0f));
        ParamUpdate contradiction;
        add_bool(contradiction, "bench_mode", false);
        add_number(contradiction, "bench_cap_rpm", 2600.0);
        rig.tick({set_params_cmd(&contradiction)});
        CHECK(rig.last_reason() == "bench_mode required: bench_cap_rpm");
        ParamUpdate off;
        add_bool(off, "bench_mode", false);
        rig.tick({set_params_cmd(&off)});
        REQUIRE(rig.last_ok());
        CHECK(rig.sup.params().bench_cap_rpm == 0.0f);
        CHECK(rig.sup.params().bench_underspeed_rpm == 0.0f);
        CHECK(effective_cap_rpm(rig.sup.params()) == doctest::Approx(4000.0f));
    }
    SUBCASE("never survive a boot")
    {
        Params p;
        p.bench_mode = true;
        p.bench_cap_rpm = 2500.0f;
        p.bench_underspeed_rpm = 300.0f;
        Supervisor sup(p);
        CHECK(sup.params().bench_cap_rpm == 0.0f);
        CHECK(sup.params().bench_underspeed_rpm == 0.0f);
    }
}

TEST_CASE("R3.8 interlocks use the limits in force: bench cap trips OVERSPEED, bench underspeed moves UNDERSPEED")
{
    SUBCASE("OVERSPEED at bench cap + 100")
    {
        Params p;
        p.bench_mode = true;
        p.bench_cap_rpm = 2500.0f;
        InterlockMonitor m;
        FaultInputs f;
        f.vbat_avg_v = 10.4f;
        f.wheel_rpm_safe = 2590.0f;
        CHECK((m.evaluate(f, p) & interlock_bit(Interlock::Overspeed)) == 0);
        f.wheel_rpm_safe = 2601.0f;
        CHECK((m.evaluate(f, p) & interlock_bit(Interlock::Overspeed)) != 0);
    }
    SUBCASE("UNDERSPEED at the T-18 threshold, not at omega_low")
    {
        Params p;
        p.bench_mode = true;
        p.bench_underspeed_rpm = 300.0f;
        InterlockMonitor m;
        FaultInputs f;
        f.vbat_avg_v = 10.4f;
        f.underspeed_applies = true;
        f.wheel_rpm_safe = 500.0f;  // below omega_low 800, above the override
        InterlockSet seen = 0;
        for (uint32_t i = 0; i < ticks_from_ms(kUnderspeedMs) + 5; ++i) seen |= m.evaluate(f, p);
        CHECK((seen & interlock_bit(Interlock::Underspeed)) == 0);
        f.wheel_rpm_safe = 290.0f;
        for (uint32_t i = 0; i < ticks_from_ms(kUnderspeedMs) + 1; ++i) seen |= m.evaluate(f, p);
        CHECK((seen & interlock_bit(Interlock::Underspeed)) != 0);
    }
}

TEST_CASE("R3.8 MANUAL throttle back to READY is bumpless")
{
    CascadeRig rig(plant_params());
    rig.arm_to_ready();
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    REQUIRE(rig.out.mode == Mode::Manual);
    rig.run(500);  // MANUAL throttle holds the entry throttle open loop
    const int before = rig.out.throttle;
    const float speed = rig.plant.rpm;
    rig.tick({mode_cmd(Mode::Ready)});
    REQUIRE(rig.out.mode == Mode::Ready);
    CHECK(std::abs(rig.out.throttle - before) <= 3);
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(speed).epsilon(0.01));
}
