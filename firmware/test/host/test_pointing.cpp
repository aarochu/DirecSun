// Set 5 (R5.1 to R5.4): compass heading, MANUAL wheel and rate, respin, and
// SUN_POINT, on the host. Mixes unit checks of each piece with closed-loop runs
// of the real estimator, supervisor and cascade against the two-body plant.
//
// Mutants (tools/host-test.sh --selftest) each remove one guard and must fail
// exactly the named case: MAG_SNAP_ARMED, MAG_SIGN_FLIP, SUN_EXTRAPOLATE,
// NO_RESPIN_RANGE, NO_SUN_LOST.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "direcsun/core/angles.hpp"
#include "direcsun/core/control.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/core/protocol.hpp"
#include "direcsun/core/sun.hpp"
#include "doctest.h"
#include "plant_sim.hpp"
#include "supervisor_harness.hpp"

using namespace direcsun;
using harness::characterised_params;
using harness::cmd;
using harness::kOperator;
using harness::manual_cmd;
using harness::mode_cmd;
using harness::Rig;

namespace {

constexpr int kHz = static_cast<int>(kTickHz);
constexpr double kDt = 1.0 / static_cast<double>(kTickHz);
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// ---- compass helpers ---------------------------------------------------------

// The raw sensor vector a box at true heading `psi_deg` (counter-clockwise
// positive) reads, for a horizontal field of `b` counts that points along world
// bearing `theta0_deg` and a vertical component `bz`. In the box frame the field
// turns the opposite way to the box: its bearing there is theta0 - psi. The
// default axis map is box (X, Y, Z) = (+x, -y, -z), so sensor x = X, y = -Y,
// z = -Z.
void sensor_vector(double psi_deg, double theta0_deg, float b, float bz, float out[3])
{
    const double a = (theta0_deg - psi_deg) * kDegToRad;
    const float box_x = b * static_cast<float>(std::cos(a));
    const float box_y = b * static_cast<float>(std::sin(a));
    out[0] = box_x;
    out[1] = -box_y;
    out[2] = -bz;
}

MagCalibration calibrated()
{
    MagCalibration cal;
    cal.valid = true;
    return cal;
}

// theta0 is the field's world bearing; 0 makes the compass heading equal the
// true heading, which the tests below rely on.
void step_mag(Estimator &est, double psi_deg, bool safe, double theta0_deg = 0.0)
{
    float raw[3];
    sensor_vector(psi_deg, theta0_deg, 400.0f, 300.0f, raw);
    est.update_mag(raw, safe);
}

// A table of 5 points, d from -0.8 to 0.8 mapping to -40..40 deg
// counter-clockwise (internal).
Params sun_params()
{
    Params p = characterised_params();
    p.sun_cal_count = 5;
    const float d[5] = {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f};
    const float deg[5] = {-40.0f, -20.0f, 0.0f, 20.0f, 40.0f};
    for (int i = 0; i < 5; ++i) {
        p.sun_cal_d[i] = d[i];
        p.sun_cal_deg[i] = deg[i];
    }
    p.light_threshold_mv = 300.0f;
    return p;
}

}  // namespace

// ---- R5.1 compass ------------------------------------------------------------

TEST_CASE("R5.1 compass: a counter-clockwise turn increases the compass heading")
{
    Estimator est;
    est.set_mag_calibration(calibrated());
    step_mag(est, 30.0, true);
    REQUIRE(est.mag_heading_valid());
    const float first = est.mag_heading_deg();
    step_mag(est, 40.0, true);
    const float second = est.mag_heading_deg();
    CHECK(wrap_deg180(static_cast<double>(second) - static_cast<double>(first)) == doctest::Approx(10.0f).epsilon(0.01));

    // The first step in SAFE starts the heading from the compass.
    Estimator fresh;
    fresh.set_mag_calibration(calibrated());
    step_mag(fresh, 30.0, true, 25.0);  // the field is 25 deg off the box's zero: the compass reads 5
    CHECK(fresh.mag_heading_deg() == doctest::Approx(5.0f).epsilon(0.01));
    CHECK(fresh.heading_unwrapped_deg() == doctest::Approx(5.0).epsilon(0.01));
}

TEST_CASE("R5.1 compass: the heading never snaps to the compass while armed")
{
    Estimator est;
    est.set_mag_calibration(calibrated());
    // Armed (not SAFE): the compass is noted but the gyro heading must not jump.
    step_mag(est, 120.0, false);
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(0.0));
    CHECK_FALSE(est.mag_fusing());
    step_mag(est, 120.0, false);
    CHECK(est.heading_unwrapped_deg() == doctest::Approx(0.0));
    // Back in SAFE it starts from the compass.
    step_mag(est, 120.0, true);
    CHECK(est.mag_fusing());
    CHECK(wrap_deg180(est.heading_unwrapped_deg() - 120.0) == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("R5.1 compass: each step pulls the heading by k = 0.02 s / 2 s of the wrapped error")
{
    Estimator est;
    est.set_mag_calibration(calibrated());
    step_mag(est, 0.0, true);
    const double start = est.heading_unwrapped_deg();
    // The box really turned 10 deg while the gyro (here: nothing fed) did not.
    step_mag(est, 10.0, true);
    CHECK(est.heading_unwrapped_deg() - start == doctest::Approx(0.01 * 10.0).epsilon(0.02));
    // After 3 time constants (150 steps) 95 % of the error is gone.
    for (int i = 0; i < 300; ++i) step_mag(est, 10.0, true);
    CHECK(est.heading_unwrapped_deg() - start == doctest::Approx(10.0).epsilon(0.1));
}

TEST_CASE("R5.1 compass: the pull takes the short way across the wrap")
{
    Estimator est;
    est.set_mag_calibration(calibrated());
    step_mag(est, 175.0, true);
    const double start = est.heading_unwrapped_deg();
    for (int i = 0; i < 10; ++i) step_mag(est, -175.0, true);  // 10 deg further counter-clockwise
    CHECK(est.heading_unwrapped_deg() > start);
    CHECK(est.heading_unwrapped_deg() < start + 10.0);
}

TEST_CASE("R5.1 compass: the gyro carries the heading between steps and the compass removes its drift")
{
    // True heading constant; a 0.5 deg/s gyro bias left uncorrected drifts the
    // gyro heading 1 deg per 2 s. The compass holds it within a fraction of a
    // degree (steady state error = bias * tau = 1 deg).
    Estimator est;
    est.set_mag_calibration(calibrated());
    step_mag(est, 0.0, true);
    EstimatorInputs in;
    in.gyro_z_dps = 0.5f;
    in.accel_g[2] = 1.0f;
    in.safe_mode = false;
    for (int tick = 1; tick <= 20 * kHz; ++tick) {
        est.update(in);
        if (tick % 10 == 0) step_mag(est, 0.0, false);
    }
    CHECK(est.mag_fusing());
    CHECK(std::fabs(est.heading_unwrapped_deg()) < 1.2);
    // Without the compass the same 20 s would have drifted 10 deg.
    Estimator gyro_only;
    for (int tick = 1; tick <= 20 * kHz; ++tick) gyro_only.update(in);
    CHECK(std::fabs(gyro_only.heading_unwrapped_deg()) > 9.0);
}

TEST_CASE("R5.1 compass: calibration, axis map, and staleness")
{
    SUBCASE("an uncalibrated magnetometer does nothing")
    {
        Estimator est;
        step_mag(est, 50.0, true);
        CHECK_FALSE(est.mag_heading_valid());
        CHECK_FALSE(est.mag_fusing());
        CHECK(est.heading_unwrapped_deg() == doctest::Approx(0.0));
    }
    SUBCASE("hard-iron offset is removed before the heading")
    {
        Estimator est;
        MagCalibration cal = calibrated();
        cal.offset[0] = 120.0f;
        cal.offset[1] = -80.0f;
        est.set_mag_calibration(cal);
        float raw[3];
        sensor_vector(30.0, 25.0, 400.0f, 300.0f, raw);
        raw[0] += 120.0f;
        raw[1] += -80.0f;
        est.update_mag(raw, true);
        float clean[3];
        sensor_vector(30.0, 25.0, 400.0f, 300.0f, clean);
        Estimator ref;
        ref.set_mag_calibration(calibrated());
        ref.update_mag(clean, true);
        CHECK(est.mag_heading_deg() == doctest::Approx(ref.mag_heading_deg()).epsilon(0.001));
    }
    SUBCASE("soft-iron matrix undoes a stretched axis")
    {
        Estimator est;
        MagCalibration cal = calibrated();
        cal.soft[0] = 0.5f;  // the sensor reads X twice as strong
        est.set_mag_calibration(cal);
        float raw[3];
        sensor_vector(60.0, 0.0, 400.0f, 0.0f, raw);
        raw[0] *= 2.0f;
        est.update_mag(raw, true);
        Estimator ref;
        ref.set_mag_calibration(calibrated());
        float clean[3];
        sensor_vector(60.0, 0.0, 400.0f, 0.0f, clean);
        ref.update_mag(clean, true);
        CHECK(est.mag_heading_deg() == doctest::Approx(ref.mag_heading_deg()).epsilon(0.001));
    }
    SUBCASE("a different axis map reads the same heading from a rotated sensor")
    {
        // Sensor mounted with box X = +y, box Y = +x, box Z = -z.
        Estimator est;
        MagCalibration cal = calibrated();
        cal.axis[0] = 2;
        cal.axis[1] = 1;
        cal.axis[2] = -3;
        est.set_mag_calibration(cal);
        float box_frame[3];
        {
            // the box-frame vector: X = b cos a, Y = b sin a
            const double a = (25.0 - 70.0) * kDegToRad;
            box_frame[0] = 400.0f * static_cast<float>(std::cos(a));
            box_frame[1] = 400.0f * static_cast<float>(std::sin(a));
            box_frame[2] = 0.0f;
        }
        const float raw[3] = {box_frame[1], box_frame[0], 0.0f};  // sensor x = Y, y = X
        est.update_mag(raw, true);
        Estimator ref;
        ref.set_mag_calibration(calibrated());
        float expect[3];
        sensor_vector(70.0, 25.0, 400.0f, 0.0f, expect);
        ref.update_mag(expect, true);
        CHECK(est.mag_heading_deg() == doctest::Approx(ref.mag_heading_deg()).epsilon(0.001));
    }
    SUBCASE("no horizontal field gives no heading")
    {
        Estimator est;
        est.set_mag_calibration(calibrated());
        const float raw[3] = {0.0f, 0.0f, 500.0f};
        est.update_mag(raw, true);
        CHECK_FALSE(est.mag_heading_valid());
    }
    SUBCASE("0.5 s without a step ends the fusion claim")
    {
        Estimator est;
        est.set_mag_calibration(calibrated());
        step_mag(est, 10.0, true);
        EstimatorInputs in;
        in.accel_g[2] = 1.0f;
        for (int i = 0; i < 249; ++i) est.update(in);
        CHECK(est.mag_fusing());
        CHECK(est.mag_heading_valid());
        for (int i = 0; i < 2; ++i) est.update(in);
        CHECK_FALSE(est.mag_fusing());
        CHECK_FALSE(est.mag_heading_valid());
    }
    SUBCASE("a new calibration restarts the fusion")
    {
        Estimator est;
        est.set_mag_calibration(calibrated());
        step_mag(est, 10.0, true);
        REQUIRE(est.mag_fusing());
        est.set_mag_calibration(calibrated());
        CHECK_FALSE(est.mag_fusing());
    }
}

TEST_CASE("R5.1 compass: the supervisor drops HEADING_GYRO only while the heading includes the compass")
{
    Rig rig;
    rig.in.heading_mag_fusing = false;
    rig.tick();
    CHECK((rig.out.flags & kFlagHeadingGyro) != 0);
    rig.in.heading_mag_fusing = true;
    rig.tick();
    CHECK((rig.out.flags & kFlagHeadingGyro) == 0);
}

// ---- R5.4 sun angle ----------------------------------------------------------

TEST_CASE("R5.4 sun angle: the table interpolates between its points")
{
    const Params p = sun_params();
    CHECK(sun_angle_from_d(0.0f, p).angle_deg == doctest::Approx(0.0f));
    CHECK(sun_angle_from_d(0.2f, p).angle_deg == doctest::Approx(10.0f));
    CHECK(sun_angle_from_d(-0.6f, p).angle_deg == doctest::Approx(-30.0f));
    CHECK(sun_angle_from_d(0.8f, p).valid);
    CHECK(sun_angle_from_d(-0.8f, p).angle_deg == doctest::Approx(-40.0f));
    // A decreasing table works too.
    Params q = sun_params();
    for (int i = 0; i < 5; ++i) q.sun_cal_deg[i] = -q.sun_cal_deg[i];
    CHECK(sun_angle_from_d(0.2f, q).angle_deg == doctest::Approx(-10.0f));
}

TEST_CASE("R5.4 sun angle: outside the calibrated range is invalid and not extrapolated")
{
    const Params p = sun_params();
    CHECK_FALSE(sun_angle_from_d(0.81f, p).valid);
    CHECK_FALSE(sun_angle_from_d(-0.81f, p).valid);
    CHECK_FALSE(sun_angle_from_d(1.0f, p).valid);
    CHECK(sun_angle_from_d(1.0f, p).angle_deg == 0.0f);
    CHECK_FALSE(sun_angle_from_d(NAN, p).valid);
}

TEST_CASE("R5.4 sun angle: no table, too dark, or a dark pair is invalid")
{
    Params none = characterised_params();
    CHECK_FALSE(sun_angle_from_d(0.0f, none).valid);

    SunSensor sensor;
    const Params p = sun_params();
    sensor.update(900.0f, 700.0f, 0.02f, p);
    CHECK(sensor.reading().valid);
    CHECK(sensor.reading().d == doctest::Approx(0.125f));
    CHECK(sensor.reading().angle_deg == doctest::Approx(6.25f));

    SunSensor dim;  // A + B = 280 mV, under the 300 mV threshold
    dim.update(150.0f, 130.0f, 0.02f, p);
    CHECK_FALSE(dim.reading().valid);

    SunSensor dark;
    Params no_threshold = p;
    no_threshold.light_threshold_mv = 0.0f;
    dark.update(0.0f, 0.0f, 0.02f, no_threshold);
    CHECK_FALSE(dark.reading().valid);

    SunSensor bad;
    bad.update(NAN, 100.0f, 0.02f, p);
    CHECK_FALSE(bad.reading().valid);
}

TEST_CASE("R5.4 sun angle: the filter keeps one noisy pair from flipping validity")
{
    SunSensor sensor;
    const Params p = sun_params();
    for (int i = 0; i < 50; ++i) sensor.update(900.0f, 700.0f, 0.02f, p);
    REQUIRE(sensor.reading().valid);
    sensor.update(0.0f, 0.0f, 0.02f, p);  // one dropout
    CHECK(sensor.reading().valid);
    for (int i = 0; i < 60; ++i) sensor.update(0.0f, 0.0f, 0.02f, p);
    CHECK_FALSE(sensor.reading().valid);
}

// ---- parameters and the wire -------------------------------------------------

TEST_CASE("R5.1/R5.4 parameters: calibrations are accepted, validated, persisted")
{
    using namespace harness;
    auto key = [](ParamUpdate &u, const char *name) -> ParamKeyValue & {
        ParamKeyValue &kv = u.items[u.count++];
        std::snprintf(kv.key, sizeof kv.key, "%s", name);
        return kv;
    };
    SUBCASE("mag_cal, mag_axis_map, sun_cal and light_threshold apply together")
    {
        ParamUpdate u;
        ParamKeyValue &cal = key(u, "mag_cal");
        cal.value.type = ParamValue::Type::Array;
        const float c[7] = {12.0f, -30.0f, 4.0f, 1.1f, 0.05f, 0.02f, 0.9f};
        std::memcpy(cal.value.array, c, sizeof c);
        cal.value.array_len = 7;
        ParamKeyValue &axis = key(u, "mag_axis_map");
        axis.value.type = ParamValue::Type::Array;
        const float a[3] = {2.0f, 1.0f, -3.0f};
        std::memcpy(axis.value.array, a, sizeof a);
        axis.value.array_len = 3;
        ParamKeyValue &sun = key(u, "sun_cal");
        sun.value.type = ParamValue::Type::Array;
        const float s[6] = {-0.5f, -30.0f, 0.0f, 0.0f, 0.5f, 30.0f};
        std::memcpy(sun.value.array, s, sizeof s);
        sun.value.array_len = 6;
        add_number(u, "light_threshold", 250.0);
        Params out;
        const ParamsResult r = apply_param_update(Params{}, u, out);
        CAPTURE(r.reason);
        REQUIRE(r.ok);
        CHECK(out.mag_cal_valid);
        CHECK(out.mag_offset[0] == 12.0f);
        CHECK(out.mag_soft[3] == doctest::Approx(0.9f));
        CHECK(out.mag_axis[0] == 2);
        CHECK(out.mag_axis[2] == -3);
        CHECK(out.sun_cal_count == 3);
        CHECK(out.sun_cal_deg[2] == 30.0f);
        CHECK(out.light_threshold_mv == 250.0f);

        // And they survive a save and a load.
        MemoryStore store;
        REQUIRE(save_params(store, out));
        Params loaded;
        const ParamsLoad load = load_params(store, loaded);
        CHECK_FALSE(load.reset);
        CHECK(loaded.mag_cal_valid);
        CHECK(loaded.mag_offset[1] == -30.0f);
        CHECK(loaded.mag_axis[1] == 1);
        CHECK(loaded.sun_cal_count == 3);
        CHECK(loaded.sun_cal_d[2] == 0.5f);
        CHECK(loaded.light_threshold_mv == 250.0f);
    }
    SUBCASE("a stored set from the previous layout is discarded, not misread")
    {
        MemoryStore store;
        store.has_version = true;
        store.version = kParamVersion - 1;
        store.has_blob = true;
        store.blob.assign(120, 0);
        Params loaded;
        const ParamsLoad load = load_params(store, loaded);
        CHECK(load.reset);
        CHECK(std::string(load.detail) == "version");
    }
    SUBCASE("bad axis maps, singular soft-iron, and non-monotonic tables are refused by key")
    {
        auto refuse = [&](const char *name, std::initializer_list<float> values, const char *reason) {
            ParamUpdate u;
            ParamKeyValue &kv = key(u, name);
            kv.value.type = ParamValue::Type::Array;
            kv.value.array_len = 0;
            for (float v : values) kv.value.array[kv.value.array_len++] = v;
            Params out;
            const ParamsResult r = apply_param_update(Params{}, u, out);
            CAPTURE(name);
            CHECK_FALSE(r.ok);
            CHECK(std::string(r.reason) == reason);
        };
        refuse("mag_axis_map", {1.0f, -1.0f, 3.0f}, "invalid param: mag_axis_map");  // x twice
        refuse("mag_cal", {0, 0, 0, 1.0f, 2.0f, 0.5f, 1.0f}, "invalid param: mag_cal");  // det 0
        refuse("mag_cal", {0, 0, 0, 1.0f, 0.0f, 0.0f}, "invalid param: mag_cal");        // 6 numbers
        refuse("sun_cal", {0.0f, 0.0f, 0.5f, 10.0f, 0.4f, 20.0f}, "invalid param: sun_cal");   // d decreases
        refuse("sun_cal", {-0.5f, -10.0f, 0.0f, 10.0f, 0.5f, 5.0f}, "invalid param: sun_cal");  // angle turns back
        refuse("sun_cal", {0.0f, 10.0f, 0.5f}, "invalid param: sun_cal");                       // odd length
    }
    SUBCASE("light_threshold is bounded")
    {
        ParamUpdate u;
        add_number(u, "light_threshold", 7000.0);
        Params out;
        CHECK_FALSE(apply_param_update(Params{}, u, out).ok);
    }
}

TEST_CASE("R5.1/R5.4 wire: set_params carries the calibrations, with the sun table's sign flipped to the box frame")
{
    static protocol::Inbound in;
    const char *frame =
        R"({"v":1,"type":"cmd","seq":5,"cmd":"set_params","params":{"mag_axis_map":["+y","+x","-z"],)"
        R"("mag_cal":{"offset":[10,20,30],"soft_iron":[[1,0.1],[0.2,0.9]]},)"
        R"("sun_cal":[{"d":-0.5,"deg":-30},{"d":0,"deg":0},{"d":0.5,"deg":30}],"light_threshold":250}})";
    protocol::parse_inbound(frame, std::strlen(frame), 7, in);
    REQUIRE(in.kind == protocol::InboundKind::Command);
    Params out;
    const ParamsResult r = apply_param_update(Params{}, in.params, out);
    CAPTURE(r.reason);
    REQUIRE(r.ok);
    CHECK(out.mag_axis[0] == 2);
    CHECK(out.mag_axis[1] == 1);
    CHECK(out.mag_axis[2] == -3);
    CHECK(out.mag_offset[2] == 30.0f);
    CHECK(out.mag_soft[1] == doctest::Approx(0.1f));
    CHECK(out.mag_soft[2] == doctest::Approx(0.2f));
    CHECK(out.sun_cal_count == 3);
    // The wire angle is clockwise positive; the box stores counter-clockwise.
    CHECK(out.sun_cal_deg[0] == 30.0f);
    CHECK(out.sun_cal_deg[2] == -30.0f);

    // Nine points do not fit: refused, not truncated.
    std::string many = R"({"v":1,"type":"cmd","seq":6,"cmd":"set_params","params":{"sun_cal":[)";
    for (int i = 0; i < 9; ++i) {
        if (i) many += ",";
        many += "{\"d\":" + std::to_string(-0.8 + 0.2 * i) + ",\"deg\":" + std::to_string(-40 + 10 * i) + "}";
    }
    many += "]}}";
    protocol::parse_inbound(many.data(), many.size(), 7, in);
    CHECK(in.kind == protocol::InboundKind::Rejected);
    CHECK(std::string(in.reason) == "invalid param: sun_cal");
}

TEST_CASE("R5.1/R5.4 wire: tlm carries mag_deg, sun_deg and sun_valid in the clockwise frame, null until calibrated")
{
    char buf[protocol::kMaxTlmBytes];
    TelemetrySnapshot s;
    s.mode = Mode::Ready;
    REQUIRE(protocol::encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"mag_deg\":null,\"sun_deg\":null,\"sun_valid\":false") != nullptr);

    s.mag_heading_valid = true;
    s.mag_deg = 30.0f;  // counter-clockwise internal
    s.sun_valid = true;
    s.sun_deg = -12.5f;
    REQUIRE(protocol::encode_tlm(s, 2, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"mag_deg\":-30.00") != nullptr);
    CHECK(std::strstr(buf, "\"sun_deg\":12.50,\"sun_valid\":true") != nullptr);

    // The longest frame still fits: every flag, bench fields, big numbers.
    s.flags = 0xFFFFFFFFu;
    s.bench_mode = true;
    s.latched = 0x1FF;
    s.imu_present = false;
    s.mag_present = false;
    REQUIRE(protocol::encode_tlm(s, 4000000000u, 4000000000u, buf, sizeof buf) > 0);
}

TEST_CASE("R5.2 wire flags: FF_TABLE_MISSING and RATE_GAIN_REDUCED")
{
    char buf[protocol::kMaxTlmBytes];
    TelemetrySnapshot s;
    s.flags = kFlagFfTableMissing | kFlagRateGainReduced;
    REQUIRE(protocol::encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"FF_TABLE_MISSING\"") != nullptr);
    CHECK(std::strstr(buf, "\"RATE_GAIN_REDUCED\"") != nullptr);
}

// ---- R5.2 MANUAL wheel and rate; flags ---------------------------------------

TEST_CASE("R5.2 MANUAL wheel and rate need the closed wheel loop; throttle does not")
{
    Rig bare;  // Hall fitted, no ff_table
    bare.arm_to_ready();
    bare.tick({mode_cmd(Mode::Manual, ManualInput::Wheel)});
    CHECK(bare.last_reason() == "input not available");
    bare.tick({mode_cmd(Mode::Manual, ManualInput::Rate)});
    CHECK(bare.last_reason() == "input not available");
    bare.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    CHECK(bare.last_ok());

    Rig ready(characterised_params());
    ready.arm_to_ready();
    ready.tick({mode_cmd(Mode::Manual, ManualInput::Wheel)});
    CHECK(ready.last_ok());
    CHECK(ready.out.manual_input == ManualInput::Wheel);
    // Entry is not a step: the wheel setpoint starts at the measured speed.
    ready.tick();
    CHECK(ready.ctl.last_demand.manual_wheel_rpm == doctest::Approx(ready.sup.params().omega_bias_rpm));
}

TEST_CASE("R5.2 MANUAL rate: the operator's rate becomes the rate loop's command, zero once stale")
{
    Rig rig(characterised_params());
    rig.enter_manual(ManualInput::Rate);
    rig.tick({manual_cmd(ManualInput::Rate, 25.0f)});
    CHECK(rig.ctl.last_demand.omega_cmd_dps == doctest::Approx(25.0f));
    rig.run(ticks_from_ms(kManualStaleMs) + 2);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);
    CHECK((rig.out.flags & kFlagManualStale) != 0);
}

TEST_CASE("R5.2 status flags: FF_TABLE_MISSING and RATE_GAIN_REDUCED explain a refusal and a gentle loop")
{
    Rig bare;
    bare.tick();
    CHECK((bare.out.flags & kFlagFfTableMissing) != 0);
    CHECK((bare.out.flags & kFlagRateGainReduced) != 0);  // default rate_gain_scale 0.2

    Params p = characterised_params();
    p.rate_gain_scale = 1.0f;
    Rig full(p);
    full.tick();
    CHECK((full.out.flags & kFlagFfTableMissing) == 0);
    CHECK((full.out.flags & kFlagRateGainReduced) == 0);

    // With the Hall bypassed there is no wheel loop to be missing a table for.
    Params bench;
    bench.bench_mode = true;
    Rig bypass(bench);
    ParamUpdate u;
    harness::add_bool(u, "bench_mode", true);
    bypass.tick({harness::set_params_cmd(&u)});
    ParamUpdate v;
    harness::add_bool(v, "hall_installed", false);
    bypass.tick({harness::set_params_cmd(&v)});
    REQUIRE(bypass.last_ok());
    bypass.tick();
    CHECK((bypass.out.flags & kFlagFfTableMissing) == 0);
}

// ---- R5.3 respin -------------------------------------------------------------

TEST_CASE("R5.3 respin: READY to SPINUP with a new start speed, then back to READY")
{
    Rig rig(characterised_params());
    rig.arm_to_ready();
    Command respin = cmd(CommandKind::Respin);
    respin.has_wheel_rpm = true;
    respin.wheel_rpm = 3000.0f;
    rig.tick({respin});
    REQUIRE(rig.last_ok());
    CHECK(rig.out.mode == Mode::Spinup);
    CHECK(rig.has_event_cause("respin"));
    rig.tick();
    CHECK(rig.ctl.last_demand.mode == Mode::Spinup);
    CHECK(rig.ctl.last_demand.omega_target_rpm == doctest::Approx(3000.0f));
    // Not READY until the wheel sits within 5 % of the target for 0.5 s.
    rig.in.wheel_rpm_safe = 2400.0f;
    rig.run(ticks_from_ms(kSpinupDwellMs) + 5);
    CHECK(rig.out.mode == Mode::Spinup);
    rig.in.wheel_rpm_safe = 3000.0f;
    rig.run(ticks_from_ms(kSpinupDwellMs) + 5);
    CHECK(rig.out.mode == Mode::Ready);

    // Without a speed it goes to the bias.
    rig.tick({cmd(CommandKind::Respin)});
    REQUIRE(rig.last_ok());
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_target_rpm == doctest::Approx(rig.sup.params().omega_bias_rpm));
}

TEST_CASE("R5.3 respin: start speeds outside the guarded range are refused")
{
    Rig rig(characterised_params());
    rig.arm_to_ready();
    const float low = rig.sup.params().omega_low_rpm + kRespinMarginRpm;       // 1000
    const float high = rig.sup.params().omega_cap_rpm - kRespinMarginRpm;      // 3800
    for (float rpm : {low - 1.0f, high + 1.0f, 0.0f, 100000.0f}) {
        CAPTURE(rpm);
        Command respin = cmd(CommandKind::Respin);
        respin.has_wheel_rpm = true;
        respin.wheel_rpm = rpm;
        rig.tick({respin});
        CHECK(rig.last_reason() == "invalid setpoint");
        CHECK(rig.out.mode == Mode::Ready);
    }
    // The ends are allowed (TD 3.6's 1000 and 3800 rpm spin demos).
    for (float rpm : {low, high}) {
        CAPTURE(rpm);
        Rig edge(characterised_params());
        edge.arm_to_ready();
        Command respin = cmd(CommandKind::Respin);
        respin.has_wheel_rpm = true;
        respin.wheel_rpm = rpm;
        edge.tick({respin});
        CHECK(edge.last_ok());
    }
}

TEST_CASE("R5.3 respin: guard order and refusal reasons")
{
    Rig safe(characterised_params());
    safe.tick({cmd(CommandKind::Respin)});
    CHECK(safe.last_reason() == "not in READY");

    Rig moving(characterised_params());
    moving.arm_to_ready();
    moving.in.rate_dps = 8.0f;
    moving.tick({cmd(CommandKind::Respin)});
    CHECK(moving.last_reason() == "box not still");
    CHECK(moving.out.mode == Mode::Ready);

    Rig viewer(characterised_params());
    viewer.arm_to_ready();
    viewer.tick({cmd(CommandKind::Respin, harness::kViewer)});
    CHECK(viewer.last_reason() == "not controlling client");
}

// ---- R5.4 SUN_POINT ----------------------------------------------------------

TEST_CASE("R5.4 SUN_POINT: guards, in the order READY, wheel loop, sun")
{
    Rig safe(sun_params());
    safe.in.sun_valid = true;
    safe.tick({mode_cmd(Mode::SunPoint)});
    CHECK(safe.last_reason() == "not in READY");

    Rig bare;
    bare.arm_to_ready();
    bare.in.sun_valid = true;
    bare.tick({mode_cmd(Mode::SunPoint)});
    CHECK(bare.last_reason() == "wheel loop not available");

    Rig dark(sun_params());
    dark.arm_to_ready();
    dark.in.sun_valid = false;
    dark.tick({mode_cmd(Mode::SunPoint)});
    CHECK(dark.last_reason() == "sun not valid");
    CHECK(dark.out.mode == Mode::Ready);

    Rig ok(sun_params());
    ok.arm_to_ready();
    ok.in.sun_valid = true;
    ok.tick({mode_cmd(Mode::SunPoint)});
    CHECK(ok.last_ok());
    CHECK(ok.out.mode == Mode::SunPoint);
    CHECK_FALSE(ok.out.psi_ref_valid);
    ok.tick({mode_cmd(Mode::Ready)});
    CHECK(ok.out.mode == Mode::Ready);
}

TEST_CASE("R5.4 SUN_POINT: the rate command turns the box toward the sun, with a hold deadband")
{
    Rig rig(sun_params());
    rig.arm_to_ready();
    rig.in.sun_valid = true;
    rig.in.sun_deg = 20.0f;  // the sun is 20 deg counter-clockwise of the pointing axis
    rig.tick({mode_cmd(Mode::SunPoint)});
    rig.tick();
    CHECK(rig.ctl.last_demand.mode == Mode::SunPoint);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == doctest::Approx(rig.sup.params().k_psi * 20.0f));
    rig.in.sun_deg = -20.0f;
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps < 0.0f);
    rig.in.sun_deg = 0.5f;  // inside the 1 deg deadband
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);
    rig.in.sun_deg = 80.0f;  // limited like the attitude loop
    rig.tick();
    CHECK(rig.ctl.last_demand.omega_cmd_dps == doctest::Approx(kAttitudeRateLimitDps));
}

TEST_CASE("R5.4 SUN_POINT: 0.5 s of invalid sun drops to READY")
{
    Rig rig(sun_params());
    rig.arm_to_ready();
    rig.in.sun_valid = true;
    rig.in.sun_deg = 10.0f;
    rig.tick({mode_cmd(Mode::SunPoint)});
    REQUIRE(rig.out.mode == Mode::SunPoint);

    const uint32_t lost = ticks_from_ms(kSunLostMs);
    rig.in.sun_valid = false;
    rig.run(lost - 2);
    CHECK(rig.out.mode == Mode::SunPoint);
    CHECK(rig.ctl.last_demand.omega_cmd_dps == 0.0f);  // holds rate, does not chase a stale angle
    // A blink shorter than the dwell restarts the count.
    rig.in.sun_valid = true;
    rig.tick();
    rig.in.sun_valid = false;
    rig.run(lost - 2);
    CHECK(rig.out.mode == Mode::SunPoint);
    bool saw_event = false;
    for (int i = 0; i < 6 && rig.out.mode == Mode::SunPoint; ++i) {
        rig.tick();
        saw_event = saw_event || rig.has_event_cause("sun_invalid");
    }
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(saw_event);
}

TEST_CASE("R5.4 SUN_POINT: closed loop, the box turns to the lamp and stays")
{
    // The real estimator, supervisor and cascade against the two-body plant, the
    // lamp 60 deg counter-clockwise of the box's pointing axis.
    Params params = sun_params();
    params.rate_gain_scale = 1.0f;
    {
        const WheelPiGains g = wheel_pi_gains(2.5f, 0.8f, kWheelPiBandwidthHz);
        params.wheel_kp = g.kp;
        params.wheel_ki = g.ki;
    }
    Supervisor sup(params);
    CascadeController ctl;
    Estimator est;
    plant::Box box;
    plant::Gyro gyro;
    box.wheel_rpm = 0.0;
    box.friction_nm = 1e-3;
    TickInputs in;
    TickOutputs out;
    in.hall_edge_age_ms = 5;
    in.vbat_avg_v = 10.4f;
    in.built_inputs = kAllBuiltInputs;
    uint32_t now = 1;
    const double lamp_deg = 60.0;
    bool pinned = true;

    auto tick = [&](std::vector<Command> cmds) {
        ++now;
        in.tick = now;
        EstimatorInputs e;
        e.gyro_z_dps = static_cast<float>(gyro.read(box.rate_dps()));
        e.accel_g[2] = 1.0f;
        e.safe_mode = out.mode == Mode::Safe;
        e.wheel_known = true;
        e.wheel_rpm = static_cast<float>(box.wheel_rpm);
        est.update(e);
        in.rate_dps = est.rate_dps();
        in.heading_deg = est.heading_unwrapped_deg();
        in.wheel_rpm_safe = static_cast<float>(box.wheel_rpm);
        in.wheel_rpm_fast = static_cast<float>(box.wheel_rpm);
        // The lamp is fixed in the world; the sun angle is its bearing from the box.
        const double sun = lamp_deg - box.heading_deg;
        in.sun_valid = std::fabs(sun) < 80.0;
        in.sun_deg = static_cast<float>(sun);
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        const double steady = out.throttle <= 48 ? 0.0 : 2.5 * (out.throttle - 48);
        box.step(box.wheel_rpm + (steady - box.wheel_rpm) / 0.8 * kDt, kDt);
        if (pinned) box.omega = 0.0;
    };
    auto run = [&](int n) {
        for (int i = 0; i < n; ++i) {
            tick(i % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
        }
    };

    tick({cmd(CommandKind::Arm)});
    for (int i = 0; i < 20 && out.mode != Mode::Ready; ++i) run(500);
    REQUIRE(out.mode == Mode::Ready);
    run(500);
    pinned = false;
    tick({mode_cmd(Mode::SunPoint)});
    REQUIRE(out.mode == Mode::SunPoint);
    run(8 * kHz);
    CAPTURE(box.heading_deg);
    CHECK(out.mode == Mode::SunPoint);
    CHECK(std::fabs(lamp_deg - box.heading_deg) < 3.0);
    CHECK(std::fabs(box.rate_dps()) < 2.0);
}

// ---- closed loop on the compass ---------------------------------------------

TEST_CASE("R5.1 HOLD on the compass: gyro bias no longer walks the heading off")
{
    // HOLD against the compass resists a gyro bias the relative (gyro-only) HOLD
    // would slowly follow. The gyro reads 0.4 deg/s high; the box does not move.
    Params params = characterised_params();
    params.rate_gain_scale = 1.0f;
    {
        const WheelPiGains g = wheel_pi_gains(2.5f, 0.8f, kWheelPiBandwidthHz);
        params.wheel_kp = g.kp;
        params.wheel_ki = g.ki;
    }
    for (bool use_compass : {false, true}) {
        CAPTURE(use_compass);
        Supervisor sup(params);
        CascadeController ctl;
        Estimator est;
        if (use_compass) est.set_mag_calibration(calibrated());
        plant::Box box;
        box.wheel_rpm = 0.0;
        box.friction_nm = 1e-3;
        TickInputs in;
        TickOutputs out;
        in.hall_edge_age_ms = 5;
        in.vbat_avg_v = 10.4f;
        in.built_inputs = kAllBuiltInputs;
        uint32_t now = 1;
        plant::Gyro gyro;
        gyro.noise_dps = 0.0;
        const double bias_dps = 0.4;
        int tick_count = 0;
        auto tick = [&](std::vector<Command> cmds) {
            ++now;
            ++tick_count;
            in.tick = now;
            EstimatorInputs e;
            e.gyro_z_dps = static_cast<float>(gyro.read(box.rate_dps()) + bias_dps);
            e.accel_g[2] = 1.0f;
            e.safe_mode = out.mode == Mode::Safe;
            e.wheel_known = true;
            e.wheel_rpm = static_cast<float>(box.wheel_rpm);
            est.update(e);
            if (use_compass && tick_count % 10 == 0) {
                float raw[3];
                sensor_vector(box.heading_deg, 25.0, 400.0f, 300.0f, raw);
                est.update_mag(raw, out.mode == Mode::Safe);
            }
            in.rate_dps = est.rate_dps();
            in.heading_deg = est.heading_unwrapped_deg();
            in.heading_mag_fusing = est.mag_fusing();
            in.wheel_rpm_safe = static_cast<float>(box.wheel_rpm);
            in.wheel_rpm_fast = static_cast<float>(box.wheel_rpm);
            in.commands = cmds.empty() ? nullptr : cmds.data();
            in.command_count = cmds.size();
            sup.step(in, ctl, out);
            in.commands = nullptr;
            in.command_count = 0;
            const double steady = out.throttle <= 48 ? 0.0 : 2.5 * (out.throttle - 48);
            box.step(box.wheel_rpm + (steady - box.wheel_rpm) / 0.8 * kDt, kDt);
        };
        auto run = [&](int n) {
            for (int i = 0; i < n; ++i) {
                tick(i % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
            }
        };
        // The compass starts the heading in SAFE.
        for (int i = 0; i < 100; ++i) tick({});
        tick({cmd(CommandKind::Arm)});
        box.omega = 0.0;
        for (int i = 0; i < 20 && out.mode != Mode::Ready; ++i) run(500);
        REQUIRE(out.mode == Mode::Ready);
        run(500);
        tick({mode_cmd(Mode::Hold)});
        REQUIRE(out.mode == Mode::Hold);
        const double start = box.heading_deg;
        run(60 * kHz);
        const double drift = std::fabs(box.heading_deg - start);
        MESSAGE((use_compass ? "compass" : "gyro only") << ": the box drifted " << drift << " deg in 60 s");
        if (use_compass) {
            CHECK(out.mode == Mode::Hold);
            CHECK((out.flags & kFlagHeadingGyro) == 0);
            CHECK(drift < 2.0);
        } else {
            CHECK((out.flags & kFlagHeadingGyro) != 0);
            CHECK(drift > 5.0);  // the gyro-only reference walks with the bias
        }
    }
}
