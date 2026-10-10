// R3.9: the rate loop closed through the box (FD section 19.1, HT-5 and HT-6),
// its anti-windup and integral bound, the stick-slip bleed, and the whole
// cascade (supervisor, rate loop, wheel PI) against the plant of plant_sim.hpp.

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "direcsun/core/control.hpp"
#include "direcsun/core/controllers.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"
#include "doctest.h"
#include "plant_sim.hpp"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::cmd;

namespace {

constexpr double kDt = 1.0 / static_cast<double>(kTickHz);
constexpr double kOneRevPerSecondDps = 360.0;

struct RateRun {
    std::vector<double> t;
    std::vector<double> rate_dps;
    std::vector<double> ref_rpm;
    std::vector<double> integral;
    std::vector<bool> momentum_saturated;
    double ref_min = 1e9;
    double ref_max = -1e9;
};

Params rate_params(float scale, int s_w = 1)
{
    Params p;
    p.rate_gain_scale = scale;
    p.s_w = static_cast<int8_t>(s_w);
    return p;
}

// The rate loop alone, with the wheel loop modelled as its 2 Hz lag. The
// command goes in through the loop's omega_cmd input, as the attitude loop's
// will (Set 4).
RateRun run_rate(const Params &p, double friction_nm, double w0_dps, const std::function<double(double)> &command_dps,
                 double seconds, int plant_s_w = 1)
{
    plant::Box box;
    box.s_w = plant_s_w;
    box.friction_nm = friction_nm;
    box.omega = w0_dps * plant::kTwoPi / 360.0;
    box.wheel_rpm = 2400.0;
    plant::WheelLag lag;
    plant::Gyro gyro;
    RateLoop loop;
    double ref = 2400.0;
    RateRun r;
    const int n = static_cast<int>(seconds / kDt);
    for (int k = 0; k < n; ++k) {
        const double t = k * kDt;
        const double measured = gyro.read(box.rate_dps());
        const RateLoopStep s = loop.step(static_cast<float>(command_dps(t)), static_cast<float>(measured),
                                         static_cast<float>(ref), static_cast<float>(kDt), p, 4000.0f);
        ref = s.omega_ref_rpm;
        box.step(lag.follow(ref, box.wheel_rpm, kDt), kDt);
        r.t.push_back(t + kDt);
        r.rate_dps.push_back(box.rate_dps());
        r.ref_rpm.push_back(ref);
        r.integral.push_back(loop.integral());
        r.momentum_saturated.push_back(s.momentum_saturated);
        if (ref < r.ref_min) r.ref_min = ref;
        if (ref > r.ref_max) r.ref_max = ref;
    }
    return r;
}

double first_time_below(const RateRun &r, double limit_dps)
{
    for (std::size_t i = 0; i < r.t.size(); ++i) {
        if (std::fabs(r.rate_dps[i]) < limit_dps) return r.t[i];
    }
    return 1e9;
}

double max_abs_after(const RateRun &r, double from_s)
{
    double m = 0.0;
    for (std::size_t i = 0; i < r.t.size(); ++i) {
        if (r.t[i] >= from_s) m = std::fmax(m, std::fabs(r.rate_dps[i]));
    }
    return m;
}

const auto zero_command = [](double) { return 0.0; };

// Feed-forward and wheel gains for the throttle-driven wheel of the cascade
// tests: steady rpm = 2.5 (throttle - 48), tau 0.8 s, as in test_controllers.cpp.
Params cascade_params()
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

// The real supervisor and cascade driving the throttle-driven wheel inside the
// two-body box. The box is pinned (the lock pin) until release().
struct BoxRig {
    Supervisor sup;
    CascadeController ctl;
    plant::Box box;
    plant::Gyro gyro;
    TickInputs in;
    TickOutputs out;
    uint32_t now = 1;
    bool pinned = true;
    double peak_tau_nm = 0.0;
    bool saw_saturated_flag = false;

    explicit BoxRig(const Params &p) : sup(p)
    {
        box.wheel_rpm = 0.0;
        in.hall_edge_age_ms = 5;
        in.vbat_avg_v = 10.4f;
        in.built_inputs = kRungOneBuiltInputs;
    }

    void tick(std::vector<Command> cmds = {})
    {
        ++now;
        in.tick = now;
        in.rate_dps = static_cast<float>(gyro.read(box.rate_dps()));
        in.wheel_rpm_safe = static_cast<float>(box.wheel_rpm);
        in.wheel_rpm_fast = static_cast<float>(box.wheel_rpm);
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        const double steady = out.throttle <= 48 ? 0.0 : 2.5 * (out.throttle - 48);
        box.step(box.wheel_rpm + (steady - box.wheel_rpm) / 0.8 * kDt, kDt);
        if (pinned) box.omega = 0.0;
        peak_tau_nm = std::fmax(peak_tau_nm, std::fabs(ctl.tau_cmd_nm()));
        if (out.flags & kFlagMomentumSaturated) saw_saturated_flag = true;
    }

    // Quiet ticks with an operator ping every 100 ms (the heartbeat).
    void run(int n)
    {
        for (int i = 0; i < n; ++i) tick(i % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
    }

    void arm_to_ready()
    {
        tick({cmd(CommandKind::Arm)});
        for (int i = 0; i < 20 && out.mode != Mode::Ready; ++i) run(500);
        REQUIRE(out.mode == Mode::Ready);
        run(500);
    }

    void release(double rate_dps)
    {
        pinned = false;
        box.omega = rate_dps * plant::kTwoPi / 360.0;
    }
};

}  // namespace

TEST_CASE("HT-5 sim: READY detumbles a 1 rev/s spin within 5 s at the design gains")
{
    // PR-1 at rate_gain_scale 1 (the design gains, FD section 12.2), for the
    // planning range of bearing friction. The DIRECSUN_MUTANT_RATE_SIGN_FLIP
    // build must fail here (tools/host-test.sh --selftest).
    for (double friction : {0.0, 1e-3, 3e-3}) {
        CAPTURE(friction);
        const RateRun r = run_rate(rate_params(1.0f), friction, kOneRevPerSecondDps, zero_command, 8.0);
        const double first = first_time_below(r, 5.0);
        const double after = max_abs_after(r, 5.0);
        MESSAGE("friction " << friction * 1e3 << " mN m: below 5 deg/s at " << first << " s, max after 5 s "
                            << after << " deg/s, omega_ref " << r.ref_min << ".." << r.ref_max << " rpm");
        CHECK(first < 5.0);
        CHECK(after < 5.0);
        CHECK(r.ref_min >= 800.0);
        CHECK(r.ref_max <= 4000.0);
    }
}

TEST_CASE("HT-5 sim: a +-30 deg/s rate command is tracked within 10 % after 1 s")
{
    for (double friction : {0.0, 1e-3, 3e-3}) {
        for (double target : {30.0, -30.0}) {
            CAPTURE(friction);
            CAPTURE(target);
            const RateRun r = run_rate(rate_params(1.0f), friction, 0.0, [target](double) { return target; }, 3.0);
            double worst = 0.0;
            for (std::size_t i = 0; i < r.t.size(); ++i) {
                if (r.t[i] >= 1.0) worst = std::fmax(worst, std::fabs(r.rate_dps[i] - target));
            }
            MESSAGE("friction " << friction * 1e3 << " mN m, command " << target << ": worst error after 1 s "
                                << worst << " deg/s");
            CHECK(worst < 0.1 * std::fabs(target));
            CHECK(r.ref_min >= 800.0);
            CHECK(r.ref_max <= 4000.0);
        }
    }
}

TEST_CASE("HT-5 sim: the sign-check gain (scale 0.2) damps a push and never amplifies it")
{
    // FD section 12.5 runs the first unpinned test at rate_gain_scale 0.2: the
    // operator pushes the box and watches it slow down. It need not meet PR-1.
    for (double friction : {0.0, 1e-3, 3e-3}) {
        CAPTURE(friction);
        const RateRun r = run_rate(rate_params(0.2f), friction, 180.0, zero_command, 8.0);
        double peak = 0.0;
        for (double v : r.rate_dps) peak = std::fmax(peak, std::fabs(v));
        MESSAGE("friction " << friction * 1e3 << " mN m: peak " << peak << " deg/s, max after 5 s "
                            << max_abs_after(r, 5.0) << " deg/s");
        CHECK(peak <= 180.5);
        CHECK(max_abs_after(r, 5.0) < 20.0);
        CHECK(r.ref_min >= 800.0);
        CHECK(r.ref_max <= 4000.0);
    }
}

TEST_CASE("HT-6 positive control: with s_w wrong, the same detumble diverges past 2 rev/s")
{
    // The controller believes s_w = -1; the wheel really spins with s_w = +1.
    // Per FD section 21 rule 5, the suite counts only if this divergence shows.
    for (float scale : {1.0f, 0.2f}) {
        CAPTURE(scale);
        const RateRun r = run_rate(rate_params(scale, -1), 1e-3, kOneRevPerSecondDps, zero_command, 8.0, 1);
        double peak = 0.0;
        for (double v : r.rate_dps) peak = std::fmax(peak, std::fabs(v));
        MESSAGE("scale " << scale << ": peak " << peak << " deg/s");
        CHECK(peak > 2.0 * kOneRevPerSecondDps);
    }
}

TEST_CASE("R3.9 anti-windup: a command held into MOMENTUM_SATURATED does not wind up")
{
    // +30 deg/s against 1 mN m of friction drains the wheel to omega_low in
    // about 10 s; the command is held to 20 s, then released. The integral
    // needs about 0.06 rad to carry the friction, well inside its 3 mN m bound
    // (0.18 rad at the design gains), so a wind-up has room to show: the
    // DIRECSUN_MUTANT_NO_RATE_ANTIWINDUP build runs it to the bound and must
    // fail here. With conditional integration it is frozen while saturated.
    const RateRun r = run_rate(rate_params(1.0f), 1e-3, 0.0, [](double t) { return t < 20.0 ? 30.0 : 0.0; }, 30.0);
    std::size_t onset = r.t.size();
    for (std::size_t i = 0; i < r.t.size(); ++i) {
        if (r.momentum_saturated[i]) {
            onset = i;
            break;
        }
    }
    REQUIRE(onset < r.t.size());  // otherwise the scenario never saturated and proves nothing
    REQUIRE(r.t[onset] < 18.0);
    const std::size_t release = static_cast<std::size_t>(20.0 / kDt) - 1;
    const double growth = std::fabs(r.integral[release] - r.integral[onset]);
    double lurch = 0.0;
    for (std::size_t i = release; i < r.t.size(); ++i) lurch = std::fmax(lurch, -r.rate_dps[i]);
    MESSAGE("saturated at " << r.t[onset] << " s; integral " << r.integral[onset] << " rad, grew by " << growth
                            << " rad until release; lurch " << lurch << " deg/s");
    CHECK(growth < 0.01);
    CHECK(lurch < 20.0);
    CHECK(max_abs_after(r, 25.0) < 5.0);
}

TEST_CASE("R3.9 rate integral: its torque stays inside 3 mN m (FD section 12.2)")
{
    // A steady 10 deg/s error with omega_ref held mid-range would integrate to
    // 1.75 rad in 10 s: 28 mN m at the design k_i, 5.7 mN m at scale 0.2. The
    // bound holds both at 3.
    for (float scale : {1.0f, 0.2f}) {
        CAPTURE(scale);
        const Params p = rate_params(scale);
        RateLoop loop;
        RateLoopStep s;
        for (int k = 0; k < static_cast<int>(10.0 / kDt); ++k) {
            s = loop.step(10.0f, 0.0f, 2400.0f, static_cast<float>(kDt), p, 4000.0f);
        }
        const float k_i = p.k_i * scale;
        CHECK(k_i * loop.integral() == doctest::Approx(kRateIntegralLimitNm));
        const float k_omega = p.k_omega * scale;
        CHECK(s.tau_cmd_nm == doctest::Approx(k_omega * 10.0f * 3.14159265f / 180.0f + kRateIntegralLimitNm));
    }
    Params none = rate_params(1.0f);
    none.k_i = 0.0f;
    RateLoop loop;
    for (int k = 0; k < 100; ++k) loop.step(10.0f, 0.0f, 2400.0f, static_cast<float>(kDt), none, 4000.0f);
    CHECK(loop.integral() == 0.0f);
}

TEST_CASE("R3.9 stick-slip bleed: a stopped box does not walk the wheel to a limit")
{
    // After a detumble, friction holds the box still with some integral left.
    // Below breakaway that integral does nothing but ramp omega_ref; the bleed
    // removes it. The DIRECSUN_MUTANT_NO_RATE_BLEED build must fail here.
    const RateRun r = run_rate(rate_params(1.0f), 1e-3, kOneRevPerSecondDps, zero_command, 20.0);
    const double at5 = r.ref_rpm[static_cast<std::size_t>(5.0 / kDt)];
    const double at20 = r.ref_rpm.back();
    MESSAGE("omega_ref " << at5 << " rpm at 5 s, " << at20 << " rpm at 20 s");
    CHECK(std::fabs(at20 - at5) < 50.0);
    CHECK(max_abs_after(r, 5.0) < 1.0);
}

TEST_CASE("R3.9 rate loop sign: omega_ref moves so the reaction opposes the box's turn")
{
    // FD section 12.5's check in numbers: the box turning counter-clockwise
    // (positive) needs a clockwise reaction, so a wheel spinning +z when
    // forward (s_w = +1) must speed up; with s_w = -1 it must slow down.
    for (int s_w : {1, -1}) {
        for (float rate : {40.0f, -40.0f}) {
            CAPTURE(s_w);
            CAPTURE(rate);
            RateLoop loop;
            const RateLoopStep s = loop.step(0.0f, rate, 2400.0f, static_cast<float>(kDt), rate_params(1.0f, s_w), 4000.0f);
            CHECK(s.tau_cmd_nm * rate < 0.0f);  // torque opposes the turn
            CHECK((s.omega_ref_rpm - 2400.0f) * static_cast<float>(s_w) * rate > 0.0f);
        }
    }
}

TEST_CASE("R3.9 cascade in READY: supervisor, rate loop, and wheel PI detumble a hand spin")
{
    // The whole controller through the real supervisor, against a throttle-
    // driven wheel whose steady map is the feed-forward table and the two-body
    // box. Pinned through SPINUP, then spun at 1 rev/s in READY.
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    BoxRig rig(p);
    rig.box.friction_nm = 1e-3;
    rig.arm_to_ready();
    CHECK(rig.ctl.rate_active());
    rig.release(kOneRevPerSecondDps);
    double first = 1e9;
    double after = 0.0;
    for (int i = 0; i < static_cast<int>(8.0 / kDt); ++i) {
        rig.run(1);
        const double t = (i + 1) * kDt;
        if (first > 1e8 && std::fabs(rig.box.rate_dps()) < 5.0) first = t;
        if (t >= 5.0) after = std::fmax(after, std::fabs(rig.box.rate_dps()));
        REQUIRE(rig.out.mode == Mode::Ready);  // no interlock tripped along the way
    }
    MESSAGE("cascade: below 5 deg/s at " << first << " s, max after 5 s " << after << " deg/s, wheel "
                                         << rig.box.wheel_rpm << " rpm, peak tau " << rig.peak_tau_nm * 1e3 << " mN m");
    CHECK(first < 5.0);
    CHECK(after < 5.0);
    CHECK(rig.peak_tau_nm > 0.0);  // the rate loop really ran
}

TEST_CASE("R3.9 cascade: MOMENTUM_SATURATED is reported while the wheel sits on a limit")
{
    // A 3 rev/s spin is more momentum than the wheel can absorb between bias
    // and the cap, so omega_ref reaches 4000 rpm and holds there.
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    BoxRig rig(p);
    rig.arm_to_ready();
    CHECK_FALSE(rig.saw_saturated_flag);
    rig.release(3.0 * kOneRevPerSecondDps);
    for (int i = 0; i < 1000 && !rig.saw_saturated_flag; ++i) rig.run(1);
    REQUIRE(rig.saw_saturated_flag);
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(4000.0f));
    CHECK(rig.out.mode == Mode::Ready);
    rig.tick({cmd(CommandKind::Disarm)});
    CHECK((rig.out.flags & kFlagMomentumSaturated) == 0u);
    CHECK(rig.ctl.tau_cmd_nm() == 0.0f);
}

TEST_CASE("R3.9 cascade: a wheel-step hold suspends the rate loop")
{
    CascadeController ctl;
    const Params p = cascade_params();
    ControlDemand d;
    d.mode = Mode::Ready;
    d.previous_throttle = 1008;
    TickInputs in;
    in.wheel_rpm_safe = 2400.0f;
    in.wheel_rpm_fast = 2400.0f;
    in.rate_dps = 40.0f;  // the box is turning: an active rate loop would move omega_ref
    ctl.throttle(d, in, p);
    CHECK(ctl.rate_active());
    CHECK(ctl.tau_cmd_nm() != 0.0f);
    d.wheel_hold = true;
    d.wheel_step_rpm = 300.0f;
    ctl.throttle(d, in, p);
    const float stepped = ctl.wheel_ref_rpm();
    d.wheel_step_rpm = 0.0f;
    for (int i = 0; i < 100; ++i) ctl.throttle(d, in, p);
    CHECK(ctl.wheel_ref_rpm() == doctest::Approx(stepped));
    CHECK_FALSE(ctl.rate_active());
    CHECK(ctl.tau_cmd_nm() == 0.0f);
    CHECK(ctl.flags() == 0u);
}
