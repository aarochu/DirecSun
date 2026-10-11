// HT-9 (FD section 19.1) and the wire sign conversion (R3.5): the real parser
// against the schema fixtures, the refusal reasons, and every encoder. The
// encoded messages are written to DIRECSUN_PROTOCOL_CAPTURE_DIR, where
// tools/check-protocol.sh validates them against protocol/schema-v1.json.

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cJSON.h"
#include "direcsun/core/interlocks.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/protocol.hpp"
#include "doctest.h"

namespace fs = std::filesystem;
using namespace direcsun;
using namespace direcsun::protocol;

namespace {

std::string read_file(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Client-to-box fixtures are the ones the parser must judge: cmd-* and ping*.
std::vector<fs::path> client_fixtures(const char *dir)
{
    std::vector<fs::path> out;
    for (const auto &entry : fs::directory_iterator(fs::path(DIRECSUN_PROTOCOL_DIR) / "examples" / dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("cmd", 0) == 0 || name.rfind("ping", 0) == 0) out.push_back(entry.path());
    }
    return out;
}

Inbound &scratch()
{
    static Inbound inbound;  // about 3 kB: keep it off the test stack
    return inbound;
}

void parse(const std::string &text, uint32_t client = 7)
{
    parse_inbound(text.data(), text.size(), client, scratch());
}

struct Json {
    cJSON *root = nullptr;
    explicit Json(const char *text) : root(cJSON_Parse(text)) {}
    ~Json() { cJSON_Delete(root); }
    double number(const char *key) const
    {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
        REQUIRE(cJSON_IsNumber(item));
        return item->valuedouble;
    }
};

TelemetrySnapshot ready_snapshot()
{
    TelemetrySnapshot s;
    s.t_us = 83120000;
    s.mode = Mode::Ready;
    s.psi_deg = 47.2f;
    s.rate_dps = 0.3f;
    s.wheel_rpm = 2504.0f;
    s.wheel_ref_rpm = 2510.0f;
    s.throttle = 412;
    s.throttle_band_lo = 150;
    s.throttle_band_hi = 400;
    s.vbat_v = 10.1f;
    s.loop_us = 740;
    s.flags = kFlagMomentumOk;
    s.imu_present = true;
    s.mag_present = true;
    return s;
}

}  // namespace

TEST_CASE("HT-9: every valid client fixture parses")
{
    const auto files = client_fixtures("valid");
    REQUIRE(files.size() >= 13);  // the instrument found the fixtures
    for (const auto &path : files) {
        CAPTURE(path.filename().string());
        const std::string text = read_file(path);
        parse(text);
        CHECK(scratch().kind != InboundKind::Rejected);
        CHECK(scratch().reason[0] == '\0');
        if (path.filename().string().rfind("cmd-set-params", 0) == 0) {
            Json doc(text.c_str());
            const cJSON *params = cJSON_GetObjectItemCaseSensitive(doc.root, "params");
            CHECK(scratch().params.count == static_cast<std::size_t>(cJSON_GetArraySize(params)));
            CHECK(scratch().command.kind == CommandKind::SetParams);
            CHECK(std::strlen(scratch().params_json) > 2);
        }
    }
}

TEST_CASE("HT-9: every invalid client fixture is refused with a reason")
{
    const auto files = client_fixtures("invalid");
    REQUIRE(files.size() >= 13);
    for (const auto &path : files) {
        CAPTURE(path.filename().string());
        parse(read_file(path));
        CHECK(scratch().kind == InboundKind::Rejected);
        CHECK(scratch().reason[0] != '\0');
    }
}

TEST_CASE("HT-9: the named refusals carry their FD reasons")
{
    parse(std::string(kMaxInboundBytes + 1, ' '));
    CHECK(std::string(scratch().reason) == "too large");
    CHECK(scratch().reject_seq == 0);

    parse(R"({"v":2,"type":"ping","seq":79,"t_client_ms":1})");
    CHECK(std::string(scratch().reason) == "unsupported v");
    CHECK(scratch().reject_seq == 79);

    parse(R"({"type":"ping","seq":80,"t_client_ms":1})");
    CHECK(std::string(scratch().reason) == "missing v");

    parse(R"({"v":1,"type":"hello","seq":81})");
    CHECK(std::string(scratch().reason) == "unknown type");

    parse(R"({"v":1,"type":"tlm","seq":82})");
    CHECK(std::string(scratch().reason) == "unexpected type tlm");

    parse(R"({"v":1,"type":"cmd","seq":83,"cmd":"launch"})");
    CHECK(std::string(scratch().reason) == "unknown cmd");

    parse(R"({"v":1,"type":"cmd","seq":84,"cmd":"manual","throttle":300,"rate_dps":5})");
    CHECK(std::string(scratch().reason) == "two setpoints");

    parse(R"({"v":1,"type":"cmd","seq":85,"cmd":"manual"})");
    CHECK(std::string(scratch().reason) == "missing setpoint");

    parse("not json at all");
    CHECK(std::string(scratch().reason) == "malformed JSON");

    parse(R"({"v":1,"type":"ping","seq":86,"t_client_ms":1} trailing)");
    CHECK(std::string(scratch().reason) == "malformed JSON");

    parse(R"({"v":1,"type":"cmd","seq":87,"cmd":"arm","extra":1})");
    CHECK(std::string(scratch().reason) == "unexpected field: extra");

    parse(R"({"v":1,"type":"cmd","seq":88,"cmd":"set_params","params":{"omega_cap_rpm":3000}})");
    CHECK(std::string(scratch().reason) == "unknown param: omega_cap_rpm");

    parse(R"({"v":1,"type":"cmd","seq":89,"cmd":"set_params","params":{"spinup_throttle":47}})");
    CHECK(std::string(scratch().reason) == "invalid param: spinup_throttle");

    // Nesting deeper than CJSON_NESTING_LIMIT (8) is refused, not recursed into.
    parse(R"({"v":1,"type":"cmd","seq":90,"cmd":"set_params","params":{"k_psi":[[[[[[[[[1]]]]]]]]]}})");
    CHECK(std::string(scratch().reason) == "malformed JSON");

    // Positive control for the reason checks: an accepted message has no reason.
    parse(R"({"v":1,"type":"cmd","seq":91,"cmd":"arm"})");
    CHECK(scratch().kind == InboundKind::Command);
    CHECK(scratch().reason[0] == '\0');
}

TEST_CASE("HT-9: commands carry the client and seq, and the ack echoes the seq")
{
    parse(R"({"v":1,"type":"cmd","seq":77,"cmd":"mode","mode":"MANUAL","input":"rate"})", 12);
    REQUIRE(scratch().kind == InboundKind::Command);
    CHECK(scratch().command.kind == CommandKind::Mode);
    CHECK(scratch().command.client == 12);
    CHECK(scratch().command.seq == 77);
    CHECK(scratch().command.target_mode == Mode::Manual);
    CHECK(scratch().command.manual_input == ManualInput::Rate);

    Ack ack;
    ack.client = 12;
    ack.seq = scratch().command.seq;
    ack.ok = false;
    std::snprintf(ack.reason, sizeof ack.reason, "%s", "wheel not at bias");
    char buf[kMaxAckBytes];
    REQUIRE(encode_ack(ack, nullptr, buf, sizeof buf) > 0);
    Json doc(buf);
    CHECK(doc.number("seq") == 77);

    parse(R"({"v":1,"type":"ping","seq":5,"t_client_ms":512330.25})", 3);
    REQUIRE(scratch().kind == InboundKind::Ping);
    CHECK(scratch().command.kind == CommandKind::Ping);
    CHECK(scratch().command.client == 3);
    CHECK(scratch().t_client_ms == doctest::Approx(512330.25));
}

TEST_CASE("sign conversion: internal CCW encodes as wire CW and decodes back")
{
    // Internal frame (FD 2.3): turning counter-clockwise at +10 deg/s at +30 deg.
    TelemetrySnapshot s = ready_snapshot();
    s.psi_deg = 30.0f;
    s.rate_dps = 10.0f;
    s.tau_cmd_mNm = 1.5f;
    char buf[kMaxTlmBytes];
    REQUIRE(encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    Json doc(buf);
    CHECK(doc.number("psi_deg") == doctest::Approx(-30.0));
    CHECK(doc.number("rate_dps") == doctest::Approx(-10.0));
    CHECK(doc.number("tau_cmd_mNm") == doctest::Approx(-1.5));

    // Wire to internal: "rotate right" (+90 on the wire) is clockwise, -90 inside.
    parse(R"({"v":1,"type":"cmd","seq":1,"cmd":"mode","mode":"SLEW","psi_ref_deg":90})");
    REQUIRE(scratch().kind == InboundKind::Command);
    CHECK(scratch().command.psi_ref_deg == doctest::Approx(-90.0f));
    parse(R"({"v":1,"type":"cmd","seq":2,"cmd":"manual","rate_dps":20})");
    REQUIRE(scratch().kind == InboundKind::Command);
    CHECK(scratch().command.setpoint == doctest::Approx(-20.0f));
    // Unsigned quantities are not converted.
    parse(R"({"v":1,"type":"cmd","seq":3,"cmd":"manual","wheel_rpm":2600})");
    CHECK(scratch().command.setpoint == doctest::Approx(2600.0f));
}

TEST_CASE("wire angles stay inside (-180, 180] after rounding")
{
    for (float internal : {180.0f, -180.0f, 179.996f, -179.996f, 0.004f, -0.004f, 90.0f, -90.0f, 359.0f}) {
        CAPTURE(internal);
        const float w = angle_to_wire(internal);
        CHECK(w > -180.0f);
        CHECK(w <= 180.0f);
        // The wire value is the internal angle negated, modulo a whole turn.
        const double turns = (static_cast<double>(w) + internal) / 360.0;
        CHECK(std::fabs(turns - std::round(turns)) < 1e-4);
    }
    CHECK(angle_to_wire(NAN) == 0.0f);
    CHECK(signed_to_wire(INFINITY) == 0.0f);
    CHECK(!std::signbit(signed_to_wire(0.0f)));
}

TEST_CASE("encoders refuse a buffer that is too small instead of truncating")
{
    char tiny[16];
    CHECK(encode_tlm(ready_snapshot(), 1, 0, tiny, sizeof tiny) == 0);
    Ack ack;
    ack.ok = true;
    CHECK(encode_ack(ack, nullptr, tiny, sizeof tiny) == 0);
    char buf[kMaxPongBytes];
    CHECK(encode_pong(1, 2, 3.0, 4, buf, sizeof buf) > 0);
}

TEST_CASE("HT-9: every encoded message kind is written for the schema check")
{
    fs::create_directories(DIRECSUN_PROTOCOL_CAPTURE_DIR);
    std::ofstream encoded(fs::path(DIRECSUN_PROTOCOL_CAPTURE_DIR) / "encoded.jsonl", std::ios::trunc);
    std::ofstream malformed(fs::path(DIRECSUN_PROTOCOL_CAPTURE_DIR) / "malformed.jsonl", std::ios::trunc);
    REQUIRE(encoded.good());
    REQUIRE(malformed.good());

    int written = 0;
    char buf[kMaxTlmBytes > kMaxAckBytes ? kMaxTlmBytes : kMaxAckBytes];
    auto emit = [&](std::size_t n) {
        REQUIRE(n > 0);
        REQUIRE(n == std::strlen(buf));
        Json doc(buf);
        REQUIRE(cJSON_IsObject(doc.root));  // the encoder's own output re-parses
        encoded << buf << '\n';
        ++written;
    };

    // tlm: SAFE with no barometer and a missing magnetometer.
    TelemetrySnapshot safe;
    safe.t_us = 1000;
    safe.imu_present = true;
    emit(encode_tlm(safe, 1, 0, buf, sizeof buf));

    // tlm: READY with momentum flags, the Hall bypass flag, and a latched fault.
    TelemetrySnapshot ready = ready_snapshot();
    ready.flags = kFlagMomentumNearLimit | kFlagHallBypass | kFlagHeadingGyro;  // HEADING_GYRO: every tlm until Set 5 (R4.2)
    ready.latched = interlock_bit(Interlock::Overspeed) | interlock_bit(Interlock::Estop);
    emit(encode_tlm(ready, 2, 3, buf, sizeof buf));

    // tlm: MANUAL throttle in bench mode, barometer present, stale setpoint.
    TelemetrySnapshot manual = ready_snapshot();
    manual.mode = Mode::Manual;
    manual.manual_input = ManualInput::Throttle;
    manual.flags = kFlagMomentumOk | kFlagManualStale;
    manual.bench_mode = true;
    manual.gyro_raw[0] = -32768;
    manual.gyro_raw[2] = 32767;
    manual.mag_raw[1] = -1234;
    manual.sun_a_mv = 812;
    manual.baro_valid = true;
    manual.baro_t_c = 23.51f;
    manual.baro_p_pa = 101325.4f;
    manual.psi_deg = -179.999f;
    emit(encode_tlm(manual, 3, 0, buf, sizeof buf));
    const std::string manual_tlm = buf;

    // tlm: READY during a bench wheel step (R3.8): wheel reference and WHEEL_STEP.
    TelemetrySnapshot stepped = ready_snapshot();
    stepped.wheel_ref_rpm = 2700.0f;
    stepped.flags = kFlagMomentumOk | kFlagWheelStep;
    stepped.bench_mode = true;
    emit(encode_tlm(stepped, 4, 0, buf, sizeof buf));

    // tlm: READY with the rate loop holding the wheel on a limit (R3.9).
    TelemetrySnapshot saturated = ready_snapshot();
    saturated.wheel_ref_rpm = 800.0f;
    saturated.tau_cmd_mNm = -31.0f;
    saturated.flags = kFlagMomentumNearLimit | kFlagMomentumSaturated;
    emit(encode_tlm(saturated, 5, 0, buf, sizeof buf));

    // tlm: SLEW and HOLD with their heading reference (R4.3).
    TelemetrySnapshot slewing = ready_snapshot();
    slewing.mode = Mode::Slew;
    slewing.psi_ref_valid = true;
    slewing.psi_ref_deg = -63.5f;
    slewing.psi_deg = -48.25f;
    slewing.rate_dps = -55.0f;
    slewing.flags = kFlagMomentumOk | kFlagHeadingGyro;
    emit(encode_tlm(slewing, 6, 0, buf, sizeof buf));
    TelemetrySnapshot holding = slewing;
    holding.mode = Mode::Hold;
    holding.psi_ref_deg = 180.0f;
    holding.psi_deg = 179.5f;
    holding.rate_dps = 0.2f;
    emit(encode_tlm(holding, 7, 0, buf, sizeof buf));

    // ack: ok, refused, and set_params with the applied set.
    Ack ack;
    ack.seq = 77;
    ack.ok = true;
    emit(encode_ack(ack, nullptr, buf, sizeof buf));
    ack.ok = false;
    std::snprintf(ack.reason, sizeof ack.reason, "%s", "latched fault: \"OVERSPEED\"");
    emit(encode_ack(ack, nullptr, buf, sizeof buf));
    parse(R"({"v":1,"type":"cmd","seq":78,"cmd":"set_params","params":{"bench_mode":true,"spinup_throttle":320,"ff_table":[{"rpm":800,"throttle":120},{"rpm":1200,"throttle":150},{"rpm":1600,"throttle":180},{"rpm":2000,"throttle":210},{"rpm":2400,"throttle":240},{"rpm":2800,"throttle":270},{"rpm":3200,"throttle":300},{"rpm":3600,"throttle":330}]}})");
    REQUIRE(scratch().kind == InboundKind::Command);
    ack.seq = 78;
    ack.ok = true;
    ack.reason[0] = '\0';
    emit(encode_ack(ack, scratch().params_json, buf, sizeof buf));

    // event: mode changes with an interlock, a command, and a transition cause.
    Event ev;
    ev.kind = Event::Kind::Mode;
    ev.mode = Mode::Safe;
    ev.cause = "HEARTBEAT_LOST";
    emit(encode_event(ev, 10, 83500, buf, sizeof buf));
    ev.mode = Mode::Spinup;
    ev.cause = "arm";
    emit(encode_event(ev, 11, 83600, buf, sizeof buf));
    ev.mode = Mode::Ready;
    ev.cause = "spinup_complete";
    emit(encode_event(ev, 12, 85600, buf, sizeof buf));
    ev.cause = "slew_complete";  // R4.3
    emit(encode_event(ev, 12, 88600, buf, sizeof buf));
    ev.cause = "SLEW_TIMEOUT";
    emit(encode_event(ev, 12, 91600, buf, sizeof buf));

    // event: notices, PARAMS_RESET and the Hall bypass switching on and off.
    Event notice;
    notice.kind = Event::Kind::Notice;
    notice.notice = "PARAMS_RESET";
    notice.detail = "version";
    emit(encode_event(notice, 13, 2000, buf, sizeof buf));
    notice.notice = "HALL_BYPASS";
    notice.detail = "on";
    emit(encode_event(notice, 14, 3000, buf, sizeof buf));
    notice.detail = "off";
    emit(encode_event(notice, 15, 4000, buf, sizeof buf));

    // pong
    emit(encode_pong(16, 79, 512330.25, 83120, buf, sizeof buf));

    CHECK(written == 19);

    // Positive control for the schema check: the MANUAL tlm with psi_deg turned
    // into a string must be rejected by tools/check-protocol.sh.
    std::string bad = manual_tlm;
    const std::size_t at = bad.find("\"psi_deg\":");
    REQUIRE(at != std::string::npos);
    const std::size_t value = at + std::strlen("\"psi_deg\":");
    const std::size_t comma = bad.find(',', value);
    bad = bad.substr(0, value) + "\"" + bad.substr(value, comma - value) + "\"" + bad.substr(comma);
    malformed << bad << '\n';
}

TEST_CASE("R3.8 tlm carries the wheel reference and the WHEEL_STEP flag")
{
    TelemetrySnapshot s = ready_snapshot();
    s.wheel_ref_rpm = 2700.0f;
    s.flags = kFlagMomentumOk | kFlagWheelStep;
    char buf[kMaxTlmBytes];
    REQUIRE(encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    Json doc(buf);
    CHECK(doc.number("wheel_ref_rpm") == doctest::Approx(2700.0));
    CHECK(std::strstr(buf, "\"WHEEL_STEP\"") != nullptr);
    CHECK(std::strstr(buf, "\"MOMENTUM_OK\"") != nullptr);

    parse(R"({"v":1,"type":"cmd","seq":9,"cmd":"set_params","params":{"bench_underspeed_rpm":300,"bench_cap_rpm":2500}})");
    REQUIRE(scratch().kind == InboundKind::Command);
    CHECK(scratch().params.count == 2);
}

TEST_CASE("R3.9 tlm carries the rate loop torque clockwise positive and MOMENTUM_SATURATED")
{
    TelemetrySnapshot s = ready_snapshot();
    s.tau_cmd_mNm = 12.5f;  // internal frame: counter-clockwise on the box
    s.flags = kFlagMomentumNearLimit | kFlagMomentumSaturated;
    char buf[kMaxTlmBytes];
    REQUIRE(encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    Json doc(buf);
    CHECK(doc.number("tau_cmd_mNm") == doctest::Approx(-12.5));
    CHECK(std::strstr(buf, "\"MOMENTUM_SATURATED\"") != nullptr);

    parse(R"({"v":1,"type":"cmd","seq":9,"cmd":"set_params","params":{"rate_gain_scale":0.2}})");
    REQUIRE(scratch().kind == InboundKind::Command);
    CHECK(scratch().params.count == 1);
}

TEST_CASE("R4.3 tlm carries psi_ref clockwise positive in HOLD and SLEW, null otherwise")
{
    TelemetrySnapshot s = ready_snapshot();
    s.psi_ref_valid = false;
    s.psi_ref_deg = 12.0f;  // never sent while not valid
    char buf[kMaxTlmBytes];
    REQUIRE(encode_tlm(s, 1, 0, buf, sizeof buf) > 0);
    CHECK(std::strstr(buf, "\"psi_ref_deg\":null") != nullptr);

    s.mode = Mode::Slew;
    s.psi_ref_valid = true;
    s.psi_ref_deg = 30.0f;  // internal frame: counter-clockwise
    REQUIRE(encode_tlm(s, 2, 0, buf, sizeof buf) > 0);
    {
        Json doc(buf);
        CHECK(doc.number("psi_ref_deg") == doctest::Approx(-30.0));
    }
    s.mode = Mode::Hold;
    s.psi_ref_deg = -180.0f;  // a half-turn: on the wire it is +180, never -180
    REQUIRE(encode_tlm(s, 3, 0, buf, sizeof buf) > 0);
    Json doc(buf);
    CHECK(doc.number("psi_ref_deg") == doctest::Approx(180.0));
}
