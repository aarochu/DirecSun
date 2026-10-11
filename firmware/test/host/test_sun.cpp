// R5.4: the sun angle (FD section 11.4), its parameters, and SUN_POINT.

#include <cmath>
#include <string>

#include "box_rig.hpp"
#include "cJSON.h"
#include "direcsun/core/protocol.hpp"
#include "direcsun/core/sun.hpp"
#include "doctest.h"

using namespace direcsun;
using namespace direcsun::protocol;
using harness::BoxRig;
using harness::cascade_params;
using harness::cmd;
using harness::mode_cmd;

namespace {

constexpr double kDt = 1.0 / static_cast<double>(kTickHz);

Params table_params()
{
    Params p;
    p.sun_cal_count = 3;
    p.sun_cal_d[0] = -0.6f; p.sun_cal_deg[0] = 60.0f;    // internal frame: counter-clockwise positive
    p.sun_cal_d[1] = 0.0f;  p.sun_cal_deg[1] = 0.0f;
    p.sun_cal_d[2] = 0.6f;  p.sun_cal_deg[2] = -60.0f;
    p.light_threshold_mv = 200.0f;
    return p;
}

// A and B for a sun at `angle_deg` (internal frame) under the identity table, total light `sum_mv`.
void lamp(BoxRig &rig, double lamp_world_deg, double sum_mv = 1000.0)
{
    double angle = lamp_world_deg - rig.box.heading_deg;   // CCW angle the box must turn
    angle = std::fmax(-89.0, std::fmin(89.0, angle));
    const double d = angle / kSunIdentityDegPerD;
    rig.in.sun_a_mv = static_cast<float>(0.5 * sum_mv * (1.0 + d));
    rig.in.sun_b_mv = static_cast<float>(0.5 * sum_mv * (1.0 - d));
}

}  // namespace

TEST_CASE("R5.4 sun angle: identity table, threshold, and no light")
{
    const Params p;
    SunReading r = evaluate_sun(1000.0f, 1000.0f, p);   // d = 0
    CHECK(r.valid);
    CHECK(r.angle_deg == doctest::Approx(0.0f).epsilon(1e-3));
    r = evaluate_sun(1500.0f, 500.0f, p);   // d = 0.5 -> -45 internal (45 deg clockwise)
    CHECK(r.valid);
    CHECK(r.angle_deg == doctest::Approx(-45.0f));
    CHECK_FALSE(evaluate_sun(0.0f, 0.0f, p).valid);   // no light: the sum must exceed zero
    Params dim = p;
    dim.light_threshold_mv = 400.0f;
    CHECK_FALSE(evaluate_sun(150.0f, 200.0f, dim).valid);
    CHECK(evaluate_sun(150.0f, 300.0f, dim).valid);
    CHECK_FALSE(evaluate_sun(-5.0f, 300.0f, p).valid);
    CHECK_FALSE(evaluate_sun(NAN, 300.0f, p).valid);
}

TEST_CASE("R5.4 sun angle: the T-10 table interpolates and is invalid outside its range")
{
    const Params p = table_params();
    SunReading r = evaluate_sun(650.0f, 350.0f, p);          // d = 0.3 -> -30
    CHECK(r.valid);
    CHECK(r.angle_deg == doctest::Approx(-30.0f));
    r = evaluate_sun(300.0f, 700.0f, p);                      // d = -0.4 -> +40
    CHECK(r.valid);
    CHECK(r.angle_deg == doctest::Approx(40.0f));
    CHECK_FALSE(evaluate_sun(900.0f, 100.0f, p).valid);      // d = 0.8, past the table
    CHECK_FALSE(evaluate_sun(50.0f, 950.0f, p).valid);       // d = -0.9
}

TEST_CASE("R5.4 sun parameters: validation, update, and persistence")
{
    CHECK(validate_params(Params{}).ok);
    Params p = table_params();
    CHECK(validate_params(p).ok);
    Params bad = p;
    bad.sun_cal_d[1] = -0.7f;   // d not increasing
    CHECK_FALSE(validate_params(bad).ok);
    bad = p;
    bad.sun_cal_deg[1] = 90.0f;   // angle not monotonic
    CHECK_FALSE(validate_params(bad).ok);
    bad = p;
    bad.sun_cal_count = 1;
    CHECK_FALSE(validate_params(bad).ok);
    bad = p;
    bad.light_threshold_mv = -1.0f;
    CHECK_FALSE(validate_params(bad).ok);

    // A decreasing table is as good as an increasing one: only monotonicity matters.
    Params rising = p;
    for (int i = 0; i < 3; ++i) rising.sun_cal_deg[i] = -p.sun_cal_deg[i];
    CHECK(validate_params(rising).ok);

    // Persistence round trip, and the kParamVersion 3 layout.
    harness::MemoryStore store;
    REQUIRE(save_params(store, p));
    Params loaded;
    const ParamsLoad load = load_params(store, loaded);
    CHECK_FALSE(load.reset);
    CHECK(loaded.sun_cal_count == 3);
    CHECK(loaded.sun_cal_deg[0] == doctest::Approx(60.0f));
    CHECK(loaded.light_threshold_mv == doctest::Approx(200.0f));
}

TEST_CASE("R5.4 set_params takes sun_cal and light_threshold, in the wire frame")
{
    static Inbound in;
    const std::string text = R"({"v":1,"type":"cmd","seq":3,"cmd":"set_params","params":{"sun_cal":[{"d":-0.6,"deg":-60},{"d":0,"deg":0},{"d":0.6,"deg":60}],"light_threshold":250}})";
    parse_inbound(text.data(), text.size(), 7, in);
    REQUIRE(in.kind == InboundKind::Command);
    REQUIRE(in.command.kind == CommandKind::SetParams);
    const ParamUpdate &update = in.params;
    Params next;
    const ParamsResult r = apply_param_update(Params{}, update, next);
    REQUIRE(r.ok);
    CHECK(next.sun_cal_count == 3);
    // The wire is clockwise positive: wire -60 at d = -0.6 is +60 counter-clockwise inside.
    CHECK(next.sun_cal_deg[0] == doctest::Approx(60.0f));
    CHECK(next.sun_cal_deg[2] == doctest::Approx(-60.0f));
    CHECK(next.light_threshold_mv == doctest::Approx(250.0f));
    // The schema's bounds still hold at the decoder.
    const std::string bad = R"({"v":1,"type":"cmd","seq":4,"cmd":"set_params","params":{"sun_cal":[{"d":0,"deg":0}]}})";
    parse_inbound(bad.data(), bad.size(), 7, in);
    CHECK(in.kind == InboundKind::Rejected);
}

TEST_CASE("R5.4 tlm sun_deg is clockwise on the wire and null without a valid sun")
{
    char buf[2048];
    TelemetrySnapshot snap;
    snap.mode = Mode::SunPoint;
    snap.sun_valid = true;
    snap.sun_deg = 30.0f;   // internal: the sun is 30 deg counter-clockwise of the face
    REQUIRE(encode_tlm(snap, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::string(buf).find("\"sun_deg\":-30") != std::string::npos);
    CHECK(std::string(buf).find("\"sun_valid\":true") != std::string::npos);
    snap.sun_valid = false;
    REQUIRE(encode_tlm(snap, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::string(buf).find("\"sun_deg\":null") != std::string::npos);
    CHECK(std::string(buf).find("\"sun_valid\":false") != std::string::npos);
}

TEST_CASE("R5.4 SUN_POINT entry guards")
{
    SUBCASE("needs READY, the wheel loop, and a valid sun")
    {
        BoxRig rig(cascade_params());
        rig.tick({mode_cmd(Mode::SunPoint)});
        CHECK(std::string(rig.out.acks.items[0].reason) == "not in READY");
        rig.arm_to_ready();
        rig.in.sun_a_mv = 0.0f;
        rig.in.sun_b_mv = 0.0f;
        rig.tick({mode_cmd(Mode::SunPoint)});
        CHECK(std::string(rig.out.acks.items[0].reason) == "sun not valid");
        CHECK(rig.out.mode == Mode::Ready);
        rig.in.sun_a_mv = 600.0f;
        rig.in.sun_b_mv = 400.0f;
        rig.tick({mode_cmd(Mode::SunPoint)});
        REQUIRE(rig.out.acks.items[0].ok);
        CHECK(rig.out.mode == Mode::SunPoint);
        CHECK(rig.has_event_cause("mode"));
        CHECK_FALSE(rig.out.psi_ref_valid);
        CHECK(rig.out.sun_valid);
    }
    SUBCASE("no wheel loop")
    {
        harness::Rig rig;
        rig.arm_to_ready();
        rig.in.sun_a_mv = 600.0f;
        rig.in.sun_b_mv = 400.0f;
        rig.tick({mode_cmd(Mode::SunPoint)});
        CHECK(rig.last_reason() == "wheel loop not available");
    }
}

TEST_CASE("R5.4 SUN_POINT returns to READY after 0.5 s of invalid sun, and not at 490 ms")
{
    BoxRig rig(cascade_params());
    rig.arm_to_ready();
    rig.in.sun_a_mv = 600.0f;
    rig.in.sun_b_mv = 400.0f;
    rig.tick({mode_cmd(Mode::SunPoint)});
    REQUIRE(rig.out.mode == Mode::SunPoint);
    rig.in.sun_a_mv = 0.0f;
    rig.in.sun_b_mv = 0.0f;
    rig.run(static_cast<int>(0.49 / kDt));
    CHECK(rig.out.mode == Mode::SunPoint);
    // A valid reading in between restarts the timer.
    rig.in.sun_a_mv = 600.0f;
    rig.in.sun_b_mv = 400.0f;
    rig.run(5);
    rig.in.sun_a_mv = 0.0f;
    rig.in.sun_b_mv = 0.0f;
    rig.run(static_cast<int>(0.49 / kDt));
    CHECK(rig.out.mode == Mode::SunPoint);
    bool saw_event = false;
    for (int i = 0; i < static_cast<int>(0.05 / kDt); ++i) {
        rig.run(1);
        saw_event = saw_event || rig.has_event_cause("sun_invalid");
    }
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(saw_event);
}

TEST_CASE("R5.4 SUN_POINT turns the box toward a lamp, either side, and holds there")
{
    for (double lamp_deg : {40.0, -40.0}) {
        for (double friction : {0.0, 2e-3}) {
            CAPTURE(lamp_deg);
            CAPTURE(friction);
            Params p = cascade_params();
            p.rate_gain_scale = 1.0f;
            BoxRig rig(p);
            rig.box.friction_nm = friction;
            rig.arm_to_ready();
            rig.release(0.0);
            lamp(rig, lamp_deg);
            rig.tick({mode_cmd(Mode::SunPoint)});
            REQUIRE(rig.out.mode == Mode::SunPoint);
            double worst_late = 0.0;
            for (int i = 0; i < static_cast<int>(8.0 / kDt); ++i) {
                lamp(rig, lamp_deg);
                rig.run(1);
                REQUIRE(rig.out.mode == Mode::SunPoint);
                if ((i + 1) * kDt > 5.0) worst_late = std::fmax(worst_late, std::fabs(rig.box.heading_deg - lamp_deg));
            }
            CHECK(worst_late < 2.0);
            CHECK(std::fabs(rig.box.rate_dps()) < 3.0);
            CHECK(rig.out.latched == 0);
        }
    }
}
