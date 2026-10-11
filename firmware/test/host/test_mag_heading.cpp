// R5.1: magnetometer heading (FD sections 10.2 and 11.3): the axis map and the
// hard- and soft-iron calibration, the complementary filter, the HEADING_GYRO
// flag, and the parameters that carry them.

#include <cmath>
#include <string>

#include "box_rig.hpp"
#include "cJSON.h"
#include "direcsun/core/angles.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/protocol.hpp"
#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace direcsun;
using namespace direcsun::protocol;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFieldCounts = 400.0;   // horizontal field magnitude in counts

// The raw sensor counts a magnetometer would read with the box at `psi_deg` (CCW positive),
// given the true calibration: box-frame field, then hard-iron offset and soft-iron distortion
// applied the way the sensor sees them, then the inverse of the axis map.
void raw_at(double psi_deg, const MagConfig &truth, const float distort[4], int16_t raw[3])
{
    const double psi = psi_deg * kPi / 180.0;
    const double bx = kFieldCounts * std::cos(psi);
    const double by = -kFieldCounts * std::sin(psi);
    // distortion D maps the true horizontal field to what is measured; the calibration is D^-1.
    const double mx = distort[0] * bx + distort[1] * by;
    const double my = distort[2] * bx + distort[3] * by;
    double box[3] = {mx + truth.offset[0], my + truth.offset[1], 120.0 + truth.offset[2]};
    for (int i = 0; i < 3; ++i) {
        // box[i] = sign[i] * raw[axis[i]]  =>  raw[axis[i]] = sign[i] * box[i]
        raw[truth.axis[i]] = static_cast<int16_t>(std::lround(truth.sign[i] * box[i]));
    }
}

MagConfig default_map()
{
    MagConfig c;
    c.calibrated = true;
    return c;
}

}  // namespace

TEST_CASE("R5.1 mag heading: a counter-clockwise turn raises it, through the default flipped-deck axis map")
{
    const MagConfig cfg = default_map();
    const float identity[4] = {1, 0, 0, 1};
    float previous = -179.0f;
    for (double psi = -170.0; psi <= 170.0; psi += 10.0) {
        int16_t raw[3];
        raw_at(psi, cfg, identity, raw);
        float h = 0.0f;
        REQUIRE(mag_heading_deg(cfg, raw, h));
        CAPTURE(psi);
        CHECK(std::fabs(h - psi) < 0.3);   // the sample is rounded to whole counts, 0.14 deg at 400 counts
        CHECK(h > previous);   // T-9: the sign is chosen so a counter-clockwise turn increases it
        previous = h;
    }
}

TEST_CASE("R5.1 mag heading: hard- and soft-iron correction recovers the heading to 0.5 deg")
{
    MagConfig truth = default_map();
    truth.offset[0] = 60.0f;
    truth.offset[1] = -35.0f;
    const float distort[4] = {1.15f, 0.12f, -0.05f, 0.85f};
    // The calibration the ground tool would fit is the offset and the inverse of the distortion.
    MagConfig cfg = truth;
    const double det = distort[0] * distort[3] - distort[1] * distort[2];
    cfg.soft[0] = static_cast<float>(distort[3] / det);
    cfg.soft[1] = static_cast<float>(-distort[1] / det);
    cfg.soft[2] = static_cast<float>(-distort[2] / det);
    cfg.soft[3] = static_cast<float>(distort[0] / det);
    double worst = 0.0;
    double worst_uncalibrated = 0.0;
    for (double psi = -175.0; psi <= 175.0; psi += 5.0) {
        int16_t raw[3];
        raw_at(psi, truth, distort, raw);
        float h = 0.0f;
        REQUIRE(mag_heading_deg(cfg, raw, h));
        worst = std::fmax(worst, std::fabs(wrap_deg180(h - psi)));
        MagConfig bare = default_map();   // no offset, no soft iron: the positive control
        float hb = 0.0f;
        REQUIRE(mag_heading_deg(bare, raw, hb));
        worst_uncalibrated = std::fmax(worst_uncalibrated, std::fabs(wrap_deg180(hb - psi)));
    }
    MESSAGE("calibrated worst error " << worst << " deg, uncalibrated " << worst_uncalibrated << " deg");
    CHECK(worst < 0.5);
    CHECK(worst_uncalibrated > 5.0);   // the distortion is real, so the test can see an uncorrected heading
    int16_t zero[3] = {0, 0, 0};
    float h = 0.0f;
    CHECK_FALSE(mag_heading_deg(MagConfig{}, zero, h));   // no horizontal field: no heading
}

TEST_CASE("R5.1 filter: the compass cancels gyro drift, and without it the heading walks away")
{
    for (bool fused : {true, false}) {
        CAPTURE(fused);
        MagConfig cfg = default_map();
        cfg.calibrated = fused;
        Estimator est;
        est.set_mag_config(cfg);
        const float identity[4] = {1, 0, 0, 1};
        const MagConfig truth = default_map();
        double psi_true = -25.0;   // the gyro-only heading starts at 0, so its error only grows
        // 0.5 deg/s of gyro bias the estimator does not know about, for 60 s, box still.
        double worst_late = 0.0;
        for (int tick = 0; tick < 30000; ++tick) {
            EstimatorInputs in;
            in.gyro_z_dps = 0.5f + 0.0f;
            in.safe_mode = false;   // bias frozen, as in flight
            in.accel_g[2] = 1.0f;
            if (tick % 2 == 0) {   // a 250 Hz stand-in for the 200 Hz sample stream
                in.mag_new = true;
                raw_at(psi_true, truth, identity, in.mag_raw);
            }
            est.update(in);
            if (tick > 15000) worst_late = std::fmax(worst_late, std::fabs(wrap_deg180(est.heading_unwrapped_deg() - psi_true)));
        }
        MESSAGE((fused ? "fused" : "gyro only") << ": worst error after 30 s " << worst_late << " deg");
        if (fused) {
            // Steady state is the bias times tau: 0.5 deg/s x 2 s = 1 deg.
            CHECK(worst_late < 1.5);
            CHECK(est.heading_absolute());
        } else {
            CHECK(worst_late > 10.0);
            CHECK_FALSE(est.heading_absolute());
        }
    }
}

TEST_CASE("R5.1 filter: the first fix sets the absolute heading, and 0.5 s without samples drops it")
{
    Estimator est;
    est.set_mag_config(default_map());
    const float identity[4] = {1, 0, 0, 1};
    const MagConfig truth = default_map();
    EstimatorInputs in;
    in.accel_g[2] = 1.0f;
    CHECK_FALSE(est.heading_absolute());
    for (int tick = 0; tick < 40; ++tick) {
        in.mag_new = tick % 2 == 0;
        raw_at(-70.0, truth, identity, in.mag_raw);
        est.update(in);
    }
    CHECK(est.heading_absolute());
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(-70.0).epsilon(0.01));
    CHECK(est.mag_heading_deg() == doctest::Approx(-70.0f).epsilon(0.01));
    in.mag_new = false;
    for (int tick = 0; tick < 240; ++tick) est.update(in);
    CHECK(est.heading_absolute());
    for (int tick = 0; tick < 20; ++tick) est.update(in);
    CHECK_FALSE(est.heading_absolute());   // after 250 ticks (0.5 s) with no step
}

TEST_CASE("R5.1 HEADING_GYRO is set until the compass is fused")
{
    harness::Rig rig;
    rig.tick();
    CHECK((rig.out.flags & kFlagHeadingGyro) != 0);
    rig.in.heading_absolute = true;
    rig.tick();
    CHECK((rig.out.flags & kFlagHeadingGyro) == 0);
}

TEST_CASE("R5.1 mag parameters: wire decode, validation, and persistence")
{
    static Inbound in;
    const std::string text = R"({"v":1,"type":"cmd","seq":3,"cmd":"set_params","params":{"mag_axis_map":["+y","+x","-z"],"mag_cal":{"offset":[10,-20,5],"soft_iron":[[1.1,0.02],[0.0,0.9]]}}})";
    parse_inbound(text.data(), text.size(), 7, in);
    REQUIRE(in.kind == InboundKind::Command);
    Params next;
    const ParamsResult r = apply_param_update(Params{}, in.params, next);
    REQUIRE(r.ok);
    CHECK(next.mag_axis[0] == 1);
    CHECK(next.mag_sign[0] == 1);
    CHECK(next.mag_axis[1] == 0);
    CHECK(next.mag_axis[2] == 2);
    CHECK(next.mag_sign[2] == -1);
    CHECK(next.mag_cal_set);
    CHECK(next.mag_offset[1] == doctest::Approx(-20.0f));
    CHECK(next.mag_soft[0] == doctest::Approx(1.1f));
    CHECK(next.mag_soft[3] == doctest::Approx(0.9f));

    Params bad = next;
    bad.mag_axis[1] = 1;   // two box axes from one sensor axis
    CHECK_FALSE(validate_params(bad).ok);
    bad = next;
    bad.mag_soft[0] = 0.0f;
    bad.mag_soft[3] = 0.0f;   // singular
    CHECK_FALSE(validate_params(bad).ok);
    bad = next;
    bad.mag_sign[0] = 0;
    CHECK_FALSE(validate_params(bad).ok);

    harness::MemoryStore store;
    REQUIRE(save_params(store, next));
    Params loaded;
    CHECK_FALSE(load_params(store, loaded).reset);
    CHECK(loaded.mag_cal_set);
    CHECK(loaded.mag_axis[0] == 1);
    CHECK(loaded.mag_offset[2] == doctest::Approx(5.0f));
    MagConfig cfg;
    mag_config_from_params(loaded, cfg);
    CHECK(cfg.calibrated);
    CHECK(cfg.sign[2] == -1);

    const std::string wrong = R"({"v":1,"type":"cmd","seq":4,"cmd":"set_params","params":{"mag_axis_map":["+x","+y"]}})";
    parse_inbound(wrong.data(), wrong.size(), 7, in);
    CHECK(in.kind == InboundKind::Rejected);
}

TEST_CASE("R5.1 tlm mag_deg is clockwise on the wire, and null without a fused compass")
{
    char buf[2048];
    TelemetrySnapshot snap;
    snap.mode = Mode::Ready;
    snap.mag_active = true;
    snap.mag_deg = 30.0f;
    REQUIRE(encode_tlm(snap, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::string(buf).find("\"mag_deg\":-30") != std::string::npos);
    snap.mag_active = false;
    REQUIRE(encode_tlm(snap, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::string(buf).find("\"mag_deg\":null") != std::string::npos);
}

TEST_CASE("R5.1 PR-3: a HOLD against a gyro the estimator mis-biased stays within 2 deg only with the compass fused")
{
    // 0.3 deg/s of gyro offset the bias learning did not catch, a 60 s hold, then three
    // 10 mN m pokes. Gyro-only, the hold keeps its estimate and the box walks away;
    // fused, the heading is absolute and the box stays (T-17).
    for (bool fused : {true, false}) {
        CAPTURE(fused);
        Params p = harness::cascade_params();
        p.rate_gain_scale = 1.0f;
        harness::BoxRig rig(p);
        rig.gyro.bias_dps = 0.3;
        if (fused) rig.enable_compass();
        rig.arm_to_ready();
        rig.release(0.0);
        rig.run(500);
        rig.tick({harness::cmd(CommandKind::ZeroHeading)});
        rig.tick({harness::mode_cmd(Mode::Hold)});
        REQUIRE(rig.out.mode == Mode::Hold);
        const double held_true = rig.box.heading_deg;
        constexpr double dt = 1.0 / static_cast<double>(kTickHz);
        double worst_outside_pokes = 0.0;
        double poke_end = -10.0;
        for (int i = 0; i < static_cast<int>(60.0 / dt); ++i) {
            const double t = i * dt;
            const bool poking = (t >= 20.0 && t < 20.1) || (t >= 35.0 && t < 35.1) || (t >= 50.0 && t < 50.1);
            rig.box.disturbance_nm = poking ? 10e-3 : 0.0;
            if (poking) poke_end = t + 0.1;
            rig.run(1);
            REQUIRE(rig.out.mode == Mode::Hold);
            if (t > poke_end + 2.0 && t > 15.0) {
                worst_outside_pokes = std::fmax(worst_outside_pokes, std::fabs(rig.box.heading_deg - held_true));
            }
        }
        MESSAGE((fused ? "fused" : "gyro only") << ": worst true error outside the poke windows " << worst_outside_pokes
                                                << " deg");
        if (fused) CHECK(worst_outside_pokes < 2.0);
        else CHECK(worst_outside_pokes > 5.0);   // the positive control: without the compass the test sees the drift
    }
}
