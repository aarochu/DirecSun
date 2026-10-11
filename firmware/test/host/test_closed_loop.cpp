// R3.9: the rate loop closed through the box (FD section 19.1, HT-5 and HT-6),
// its anti-windup and integral bound, the stick-slip bleed, and the whole
// cascade (supervisor, rate loop, wheel PI) against the plant of plant_sim.hpp.
// R4.3: HOLD and SLEW through the same cascade, with the estimator's heading.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "direcsun/core/control.hpp"
#include "direcsun/core/controllers.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/interlocks.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"
#include "doctest.h"
#include "box_rig.hpp"
#include "plant_sim.hpp"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::BoxRig;
using harness::cascade_params;
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
    // +30 deg/s against 1 mN m of friction drains the wheel to its floor in
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

TEST_CASE("R4.4 DETUMBLE: the named mode brakes a one rev/s hand spin")
{
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    BoxRig rig(p);
    rig.box.friction_nm = 1e-3;
    rig.arm_to_ready();
    rig.tick({harness::mode_cmd(Mode::Detumble)});
    REQUIRE(rig.out.mode == Mode::Detumble);
    CHECK(rig.ctl.rate_active());
    rig.release(kOneRevPerSecondDps);
    double first_below_five_s = 1e9;
    for (int i = 0; i < static_cast<int>(5.0 / kDt); ++i) {
        rig.run(1);
        if (first_below_five_s > 1e8 && std::fabs(rig.box.rate_dps()) < 5.0) {
            first_below_five_s = (i + 1) * kDt;
        }
        REQUIRE(rig.out.mode == Mode::Detumble);
    }
    CHECK(first_below_five_s < 5.0);
    CHECK(std::fabs(rig.box.rate_dps()) < 5.0);
    rig.tick({harness::mode_cmd(Mode::Ready)});
    CHECK(rig.out.mode == Mode::Ready);
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

TEST_CASE("V-52 floor: a reference pinned on the floor does not trip UNDERSPEED")
{
    // A clockwise 3 rev/s spin is more momentum than the wheel can give up
    // between bias and the floor, so the rate loop drives omega_ref down until
    // it holds on the floor (MOMENTUM_SATURATED) with the box still turning.
    // The floor sits 100 rpm above omega_low, the UNDERSPEED threshold, so the
    // wheel settling onto it stays clear of the trip. The
    // DIRECSUN_MUTANT_FLOOR_AT_LOW build puts the floor back on omega_low and
    // must fail here.
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    BoxRig rig(p);
    rig.arm_to_ready();
    rig.release(-3.0 * kOneRevPerSecondDps);
    for (int i = 0; i < 1000 && !rig.saw_saturated_flag; ++i) rig.run(1);
    REQUIRE(rig.saw_saturated_flag);
    double min_wheel = rig.box.wheel_rpm;
    int saturated_ticks = 0;
    const int hold_ticks = static_cast<int>(5.0 / kDt);
    for (int i = 0; i < hold_ticks && rig.out.mode == Mode::Ready; ++i) {
        rig.run(1);
        min_wheel = std::fmin(min_wheel, rig.box.wheel_rpm);
        if (rig.out.flags & kFlagMomentumSaturated) ++saturated_ticks;
    }
    MESSAGE("floor hold: mode " << static_cast<int>(rig.out.mode) << ", wheel min " << min_wheel << " rpm, now "
                                << rig.box.wheel_rpm << " rpm, box " << rig.box.rate_dps() << " deg/s, saturated "
                                << saturated_ticks << " of " << hold_ticks << " ticks");
    CHECK(rig.out.mode == Mode::Ready);
    CHECK((rig.out.latched & interlock_bit(Interlock::Underspeed)) == 0u);
    CHECK(min_wheel > p.omega_low_rpm);
    CHECK(rig.ctl.wheel_ref_rpm() == doctest::Approx(p.omega_low_rpm + 100.0f));
    CHECK(saturated_ticks == hold_ticks);  // pinned the whole time, and reported so
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

namespace {

struct AttitudeRun {
    double settle_s = 1e9;      // after which the true heading stays inside the band
    double worst_after_s = 0.0; // the largest error after the deadline
    double peak_error_deg = 0.0;
    double end_error_deg = 0.0; // estimator heading against the target at the end
    double complete_s = -1.0;   // slew_complete, or -1
    double timeout_s = -1.0;    // SLEW_TIMEOUT, or -1
    double ref_min = 1e9;       // omega_ref while HOLD or SLEW ran the closed loop
    double ref_max = -1e9;
    bool left_safe_modes = false;  // SAFE seen: an interlock or the heartbeat ended the run
};

// Armed, pinned through SPINUP, released at rest in READY, zeroed. Gyro noise
// from `seed`.
BoxRig &ready_and_zeroed(BoxRig &rig, double friction_nm, unsigned seed)
{
    rig.gyro.rng.seed(seed);
    rig.box.friction_nm = friction_nm;
    rig.arm_to_ready();
    rig.release(0.0);
    rig.run(250);
    rig.tick({cmd(CommandKind::ZeroHeading)});
    REQUIRE(rig.out.mode == Mode::Ready);
    return rig;
}

void track_ref(BoxRig &rig, AttitudeRun &r)
{
    if ((rig.out.mode == Mode::Hold || rig.out.mode == Mode::Slew) && rig.ctl.closed_loop()) {
        r.ref_min = std::fmin(r.ref_min, rig.ctl.wheel_ref_rpm());
        r.ref_max = std::fmax(r.ref_max, rig.ctl.wheel_ref_rpm());
    }
    if (rig.out.mode == Mode::Safe) r.left_safe_modes = true;
}

// A SLEW to `target_deg` from rest, run for `seconds`. The error is the true
// heading's, measured from where the zero was taken; `band_deg` sets settle_s.
// `ib_scale` and `iw_scale` move the plant's inertias off the controller's
// constants (R4.5 robustness).
AttitudeRun slew_run(double friction_nm, unsigned seed, double target_deg, double seconds, double band_deg,
                     double deadline_s, double ib_scale = 1.0, double iw_scale = 1.0)
{
    Params p = cascade_params();
    p.rate_gain_scale = 1.0f;
    BoxRig rig(p);
    rig.box.i_b *= ib_scale;
    rig.box.i_w *= iw_scale;
    ready_and_zeroed(rig, friction_nm, seed);
    const double zero_true = rig.box.heading_deg;
    Command c = harness::mode_cmd(Mode::Slew);
    c.psi_ref_deg = static_cast<float>(target_deg);
    rig.tick({c});
    REQUIRE(rig.out.mode == Mode::Slew);
    AttitudeRun r;
    const int n = static_cast<int>(seconds / kDt);
    for (int i = 0; i < n; ++i) {
        rig.run(1);
        const double t = (i + 1) * kDt;
        const double error = std::fabs(rig.box.heading_deg - zero_true - target_deg);
        r.peak_error_deg = std::fmax(r.peak_error_deg, error);
        if (error >= band_deg) r.settle_s = 1e9;
        else if (r.settle_s > 1e8) r.settle_s = t;
        if (t >= deadline_s) r.worst_after_s = std::fmax(r.worst_after_s, error);
        if (r.complete_s < 0.0 && rig.has_event_cause("slew_complete")) r.complete_s = t;
        if (r.timeout_s < 0.0 && rig.has_event_cause("SLEW_TIMEOUT")) r.timeout_s = t;
        track_ref(rig, r);
    }
    r.end_error_deg = rig.out.heading_deg - target_deg;
    return r;
}

}  // namespace

TEST_CASE("HT-5 sim: a 90 deg SLEW settles within 3 deg in under 3 s (PR-2)")
{
    // The whole cascade with the estimator's heading, for the planning range
    // of friction and ten seeds of gyro noise, both directions of the judge
    // sequence. The DIRECSUN_MUTANT_FLIP_KPSI build must fail here
    // (tools/host-test.sh --selftest): its slew runs away from the target.
    double slowest = 0.0;
    double worst = 0.0;
    double divergence = 0.0;
    for (double friction : {0.0, 1e-3, 3e-3}) {
        for (double target : {90.0, -90.0}) {
            for (unsigned seed = 1; seed <= 10; ++seed) {
                CAPTURE(friction);
                CAPTURE(target);
                CAPTURE(seed);
                const AttitudeRun r = slew_run(friction, seed, target, 5.0, 3.0, 3.0);
                slowest = std::fmax(slowest, r.settle_s);
                worst = std::fmax(worst, r.worst_after_s);
                divergence = std::fmax(divergence, r.peak_error_deg - std::fabs(target));
                if (seed == 1) {
                    MESSAGE("friction " << friction * 1e3 << " mN m, target " << target << ": settled at "
                                        << r.settle_s << " s, worst after 3 s " << r.worst_after_s
                                        << " deg, slew_complete at " << r.complete_s << " s, omega_ref "
                                        << r.ref_min << ".." << r.ref_max << " rpm");
                }
                CHECK(r.settle_s < 3.0);
                CHECK(r.worst_after_s < 3.0);
                CHECK(r.complete_s > 0.0);
                CHECK(r.complete_s < 3.5);
                CHECK(r.timeout_s < 0.0);
                CHECK(std::fabs(r.end_error_deg) < 3.0);
                CHECK(r.ref_min >= 800.0);
                CHECK(r.ref_max <= 4000.0);
                CHECK_FALSE(r.left_safe_modes);
            }
        }
    }
    MESSAGE("60 slews: slowest settle " << slowest << " s, worst error after 3 s " << worst
                                        << " deg, largest move past the start away from the target "
                                        << divergence << " deg");
}

TEST_CASE("HT-5 sim: a HOLD poked with 10 mN m for 100 ms returns within 2 deg in under 2 s")
{
    // Then the box must stay put without the wheel creeping it: at 3 mN m a
    // creep spends about 440 rpm of wheel speed per second, which drained the
    // wheel into UNDERSPEED within 4 s before the hold deadband (V-51). The
    // DIRECSUN_MUTANT_NO_HOLD_DEADBAND build must fail here.
    double slowest = 0.0;
    double biggest = 0.0;
    double walk = 0.0;
    for (double friction : {0.0, 1e-3, 3e-3}) {
        for (double poke : {10e-3, -10e-3}) {
            for (unsigned seed = 1; seed <= 10; ++seed) {
                CAPTURE(friction);
                CAPTURE(poke);
                CAPTURE(seed);
                Params p = cascade_params();
                p.rate_gain_scale = 1.0f;
                BoxRig rig(p);
                ready_and_zeroed(rig, friction, seed);
                rig.tick({harness::mode_cmd(Mode::Hold)});
                REQUIRE(rig.out.mode == Mode::Hold);
                rig.run(500);
                const double held_true = rig.box.heading_deg;
                AttitudeRun r;
                double back_s = 1e9;
                double ref_at_3s = 0.0;
                for (int i = 0; i < static_cast<int>(4.0 / kDt); ++i) {
                    const double t = i * kDt;
                    rig.box.disturbance_nm = t < 0.1 ? poke : 0.0;
                    if (i == static_cast<int>(3.0 / kDt)) ref_at_3s = rig.ctl.wheel_ref_rpm();
                    rig.run(1);
                    const double error = std::fabs(rig.box.heading_deg - held_true);
                    r.peak_error_deg = std::fmax(r.peak_error_deg, error);
                    if (error >= 2.0) back_s = 1e9;
                    else if (back_s > 1e8) back_s = t + kDt;
                    track_ref(rig, r);
                }
                const double walked = std::fabs(rig.ctl.wheel_ref_rpm() - ref_at_3s);
                slowest = std::fmax(slowest, back_s);
                biggest = std::fmax(biggest, r.peak_error_deg);
                walk = std::fmax(walk, walked);
                if (seed == 1) {
                    MESSAGE("friction " << friction * 1e3 << " mN m, poke " << poke * 1e3 << " mN m: peak "
                                        << r.peak_error_deg << " deg, back within 2 deg at " << back_s
                                        << " s, omega_ref " << r.ref_min << ".." << r.ref_max
                                        << " rpm, walked " << walked << " rpm from 3 to 4 s");
                }
                CHECK(r.peak_error_deg > 2.0);  // the poke really moved it
                CHECK(back_s < 2.0);
                CHECK(walked < 25.0);
                CHECK(rig.out.mode == Mode::Hold);
                CHECK(r.ref_min >= 800.0);
                CHECK(r.ref_max <= 4000.0);
                CHECK_FALSE(r.left_safe_modes);
            }
        }
    }
    MESSAGE("60 pokes: largest excursion " << biggest << " deg, slowest return " << slowest
                                           << " s, largest wheel walk from 3 to 4 s " << walk << " rpm");
}

TEST_CASE("R4.3 timeout path (sim): at 30 mN m of friction the SLEW times out at 6.0 s, then READY")
{
    // Far above the planning range (1 to 3 mN m): the rate loop's torque
    // cannot break the box free, so the slew can only end by its timeout.
    const AttitudeRun r = slew_run(30e-3, 1, -90.0, 6.5, 3.0, 6.0);
    MESSAGE("timeout at " << r.timeout_s << " s, peak error " << r.peak_error_deg << " deg, omega_ref " << r.ref_min
                          << ".." << r.ref_max << " rpm");
    CHECK(r.timeout_s >= 6.0 - kDt);
    CHECK(r.timeout_s <= 6.0 + kDt);
    CHECK(r.complete_s < 0.0);
    CHECK_FALSE(r.left_safe_modes);
    CHECK(r.ref_min >= 800.0);
    CHECK(r.ref_max <= 4000.0);
}

TEST_CASE("R4.4 DETUMBLE meets PR-1 both ways at every friction in the planning range")
{
    // A 1 rev/s hand spin in DETUMBLE is under 5 deg/s within 5 s and stays
    // there for 5 s more, with no interlock on the way. The wheel lags its
    // reference, so the reference can touch the cap or the floor for a moment
    // early on (MOMENTUM_SATURATED, reported below); it must be off the clamp
    // by the end.
    for (double friction : {0.0, 1e-3, 3e-3}) {
        for (double dir : {1.0, -1.0}) {
            CAPTURE(friction);
            CAPTURE(dir);
            Params p = cascade_params();
            p.rate_gain_scale = 1.0f;
            BoxRig rig(p);
            rig.box.friction_nm = friction;
            rig.arm_to_ready();
            rig.tick({harness::mode_cmd(Mode::Detumble)});
            REQUIRE(rig.out.mode == Mode::Detumble);
            REQUIRE_FALSE(rig.saw_saturated_flag);
            rig.release(dir * kOneRevPerSecondDps);
            double after = 0.0;
            float ref_min = 1e9f;
            float ref_max = -1e9f;
            double first_sat = -1.0;
            double last_sat = -1.0;
            for (int i = 0; i < static_cast<int>(10.0 / kDt); ++i) {
                rig.run(1);
                const double t = (i + 1) * kDt;
                if (t >= 5.0) after = std::fmax(after, std::fabs(rig.box.rate_dps()));
                ref_min = std::fmin(ref_min, rig.ctl.wheel_ref_rpm());
                ref_max = std::fmax(ref_max, rig.ctl.wheel_ref_rpm());
                if (rig.out.flags & kFlagMomentumSaturated) {
                    if (first_sat < 0.0) first_sat = t;
                    last_sat = t;
                }
                REQUIRE(rig.out.mode == Mode::Detumble);  // no interlock on the way
            }
            MESSAGE("friction " << friction * 1e3 << " mN m, direction " << dir << ": max |rate| from 5 to 10 s "
                                << after << " deg/s; wheel reference " << ref_min << " to " << ref_max
                                << " rpm; MOMENTUM_SATURATED from " << first_sat << " to " << last_sat
                                << " s after release (-1: never)");
            CHECK(after < 5.0);
            CHECK(last_sat < 5.0);
            CHECK((rig.out.flags & kFlagMomentumSaturated) == 0u);
        }
    }
}

TEST_CASE("R4.4 DETUMBLE past the absorbable spin saturates the wheel without tripping an interlock")
{
    // 2.2 rev/s is more than the wheel can absorb (about 1.5 rev/s each way):
    // the reference sits on a clamp and MOMENTUM_SATURATED is set, but nothing trips.
    for (double dir : {1.0, -1.0}) {
        CAPTURE(dir);
        Params p = cascade_params();
        p.rate_gain_scale = 1.0f;
        BoxRig rig(p);
        rig.arm_to_ready();
        rig.tick({harness::mode_cmd(Mode::Detumble)});
        REQUIRE(rig.out.mode == Mode::Detumble);
        rig.release(dir * 2.2 * kOneRevPerSecondDps);
        float ref_min = 1e9f;
        float ref_max = -1e9f;
        for (int i = 0; i < static_cast<int>(8.0 / kDt); ++i) {
            rig.run(1);
            REQUIRE(rig.out.mode == Mode::Detumble);
            ref_min = std::fmin(ref_min, rig.ctl.wheel_ref_rpm());
            ref_max = std::fmax(ref_max, rig.ctl.wheel_ref_rpm());
        }
        MESSAGE("direction " << dir << ": wheel reference " << ref_min << " to " << ref_max << " rpm");
        CHECK(ref_min >= omega_ref_floor_rpm(p) - 0.5f);
        CHECK(ref_max <= p.omega_cap_rpm + 0.5f);
        CHECK(rig.saw_saturated_flag);
        CHECK(rig.out.latched == 0u);
    }
}

TEST_CASE("R4.4 DETUMBLE and READY run the same controller: identical wheel reference trajectories")
{
    // The same hand spin, once in READY and once in DETUMBLE, tick for tick.
    // A DETUMBLE with a gain, limit, or bleed of its own makes the two differ.
    auto trajectory = [](bool detumble) {
        Params p = cascade_params();
        p.rate_gain_scale = 1.0f;
        BoxRig rig(p);
        rig.box.friction_nm = 1e-3;
        rig.arm_to_ready();
        if (detumble) {
            rig.tick({harness::mode_cmd(Mode::Detumble)});
        } else {
            rig.tick();
        }
        REQUIRE(rig.out.mode == (detumble ? Mode::Detumble : Mode::Ready));
        rig.release(kOneRevPerSecondDps);
        std::vector<float> ref;
        for (int i = 0; i < static_cast<int>(3.0 / kDt); ++i) {
            rig.run(1);
            ref.push_back(rig.ctl.wheel_ref_rpm());
        }
        return ref;
    };
    const std::vector<float> ready = trajectory(false);
    const std::vector<float> detumble = trajectory(true);
    REQUIRE(ready.size() == detumble.size());
    float worst = 0.0f;
    float span = 0.0f;
    for (std::size_t i = 0; i < ready.size(); ++i) {
        worst = std::fmax(worst, std::fabs(ready[i] - detumble[i]));
        span = std::fmax(span, std::fabs(ready[i] - ready[0]));
    }
    MESSAGE("largest difference " << worst << " rpm over a reference that moved " << span << " rpm");
    CHECK(span > 100.0f);  // the spin really moved the wheel reference
    CHECK(worst == 0.0f);
}

TEST_CASE("R4.5 robustness: the controller built on the TD inertias meets PR-1 and PR-2 on a plant 30 % off")
{
    // T-3 passes within 30 % of 1.03e-3, so the gains must tolerate that band,
    // and a wheel 20 % off, without retuning. Friction at both ends of the
    // planning range, ten gyro-noise seeds.
    double worst_settle = 0.0;
    double worst_detumble = 0.0;
    for (double ib : {0.7, 1.3}) {
        for (double iw : {0.8, 1.2}) {
            for (double friction : {0.0, 3e-3}) {
                CAPTURE(ib);
                CAPTURE(iw);
                CAPTURE(friction);
                for (unsigned seed = 1; seed <= 10; ++seed) {
                    CAPTURE(seed);
                    const AttitudeRun r = slew_run(friction, seed, 90.0, 5.0, 3.0, 3.0, ib, iw);
                    worst_settle = std::fmax(worst_settle, r.settle_s);
                    CHECK(r.settle_s < 3.0);
                    CHECK(r.timeout_s < 0.0);
                    CHECK(std::fabs(r.end_error_deg) < 3.0);
                    CHECK_FALSE(r.left_safe_modes);

                    Params p = cascade_params();
                    p.rate_gain_scale = 1.0f;
                    BoxRig rig(p);
                    rig.box.i_b *= ib;
                    rig.box.i_w *= iw;
                    rig.box.friction_nm = friction;
                    rig.gyro.rng.seed(seed);
                    rig.arm_to_ready();
                    rig.release(kOneRevPerSecondDps);
                    double after = 0.0;
                    for (int i = 0; i < static_cast<int>(8.0 / kDt); ++i) {
                        rig.run(1);
                        if ((i + 1) * kDt >= 5.0) after = std::fmax(after, std::fabs(rig.box.rate_dps()));
                    }
                    worst_detumble = std::fmax(worst_detumble, after);
                    CHECK(after < 5.0);
                    CHECK(rig.out.mode == Mode::Ready);
                }
            }
        }
    }
    MESSAGE("worst 90 deg slew settle " << worst_settle << " s; worst |rate| 5 to 8 s after a 1 rev/s spin "
                                       << worst_detumble << " deg/s");
}
