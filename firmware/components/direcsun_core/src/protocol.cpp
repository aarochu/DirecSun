#include "direcsun/core/protocol.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "cJSON.h"
#include "direcsun/core/interlocks.hpp"
#include "direcsun/core/modes.hpp"

// Mutation-test switch: removes the wire sign conversion so the host selftest
// can prove the sign test notices. Host selftest builds only.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_NO_WIRE_SIGN)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun::protocol {

// ---------------------------------------------------------------------------
// Wire frame: the only sign conversion in the firmware (CONTEXT.md, 2026-10-10).
// ---------------------------------------------------------------------------

namespace {

float negate_for_wire(float v)
{
#ifdef DIRECSUN_MUTANT_NO_WIRE_SIGN
    return v;
#else
    return -v;
#endif
}

// Rounds to 0.01 and turns -0 into 0, so the printed text is the value checked.
double round_centi(double v)
{
    const double r = std::round(v * 100.0) / 100.0;
    return r == 0.0 ? 0.0 : r;
}

}  // namespace

float angle_to_wire(float internal_deg)
{
    if (!std::isfinite(internal_deg)) return 0.0f;
    double w = round_centi(negate_for_wire(internal_deg));
    while (w <= -180.0) w += 360.0;
    while (w > 180.0) w -= 360.0;
    return static_cast<float>(round_centi(w));
}

float signed_to_wire(float internal)
{
    if (!std::isfinite(internal)) return 0.0f;
    const float w = negate_for_wire(internal);
    return w == 0.0f ? 0.0f : w;
}

float angle_from_wire(float wire_deg)
{
    float internal = negate_for_wire(wire_deg);
    while (internal <= -180.0f) internal += 360.0f;
    while (internal > 180.0f) internal -= 360.0f;
    return internal == 0.0f ? 0.0f : internal;
}

float signed_from_wire(float wire)
{
    const float internal = negate_for_wire(wire);
    return internal == 0.0f ? 0.0f : internal;
}

// ---------------------------------------------------------------------------
// Inbound
// ---------------------------------------------------------------------------

namespace {

constexpr double kSeqMax = 4294967295.0;

struct JsonDoc {
    cJSON *root = nullptr;
    ~JsonDoc() { cJSON_Delete(root); }
};

void reset(Inbound &out, uint32_t client)
{
    out.kind = InboundKind::Rejected;
    out.command = Command{};
    out.command.client = client;
    out.t_client_ms = 0.0;
    out.params.count = 0;
    out.params_json[0] = '\0';
    out.reject_seq = 0;
    out.reason[0] = '\0';
}

void reject(Inbound &out, uint32_t seq, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void reject(Inbound &out, uint32_t seq, const char *fmt, ...)
{
    out.kind = InboundKind::Rejected;
    out.reject_seq = seq;
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(out.reason, sizeof out.reason, fmt, args);
    va_end(args);
}

bool is_finite_number(const cJSON *item)
{
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble);
}

bool is_integer(const cJSON *item)
{
    return is_finite_number(item) && std::floor(item->valuedouble) == item->valuedouble;
}

bool in_range(const cJSON *item, double lo, double hi)
{
    return is_finite_number(item) && item->valuedouble >= lo && item->valuedouble <= hi;
}

// Name of the first member of `object` that is not in `allowed`, or nullptr.
const char *unexpected_member(const cJSON *object, std::initializer_list<const char *> allowed)
{
    for (const cJSON *member = object->child; member != nullptr; member = member->next) {
        bool known = false;
        for (const char *name : allowed) {
            if (member->string != nullptr && std::strcmp(member->string, name) == 0) {
                known = true;
                break;
            }
        }
        if (!known) return member->string != nullptr ? member->string : "?";
    }
    return nullptr;
}

int member_count(const cJSON *object)
{
    int n = 0;
    for (const cJSON *member = object->child; member != nullptr; member = member->next) ++n;
    return n;
}

const cJSON *member(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool string_is(const cJSON *item, const char *value)
{
    return cJSON_IsString(item) && item->valuestring != nullptr && std::strcmp(item->valuestring, value) == 0;
}

bool mode_from_wire(const char *name, Mode &mode)
{
    struct Entry {
        const char *name;
        Mode mode;
    };
    // commandable_mode in the schema: SAFE and SPINUP are reached by other commands.
    static const Entry kModes[] = {
        {"READY", Mode::Ready}, {"DETUMBLE", Mode::Detumble}, {"HOLD", Mode::Hold},
        {"SLEW", Mode::Slew},   {"SUN_POINT", Mode::SunPoint}, {"MANUAL", Mode::Manual},
    };
    for (const Entry &e : kModes) {
        if (std::strcmp(name, e.name) == 0) {
            mode = e.mode;
            return true;
        }
    }
    return false;
}

bool manual_input_from_wire(const cJSON *item, ManualInput &input)
{
    if (string_is(item, "throttle")) input = ManualInput::Throttle;
    else if (string_is(item, "wheel")) input = ManualInput::Wheel;
    else if (string_is(item, "rate")) input = ManualInput::Rate;
    else return false;
    return true;
}

// ---- set_params --------------------------------------------------------------

bool valid_ff_table(const cJSON *item, ParamValue &value)
{
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) != 8) return false;
    value.type = ParamValue::Type::Array;
    value.array_len = 0;
    for (const cJSON *point = item->child; point != nullptr; point = point->next) {
        if (!cJSON_IsObject(point) || unexpected_member(point, {"rpm", "throttle"}) != nullptr) return false;
        const cJSON *rpm = member(point, "rpm");
        const cJSON *throttle = member(point, "throttle");
        if (!is_finite_number(rpm) || rpm->valuedouble < 0.0) return false;
        if (!is_integer(throttle) || !in_range(throttle, 48.0, 2047.0)) return false;
        value.array[value.array_len++] = static_cast<float>(rpm->valuedouble);
        value.array[value.array_len++] = static_cast<float>(throttle->valuedouble);
    }
    return true;
}

bool valid_mag_axis_map(const cJSON *item)
{
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) != 3) return false;
    for (const cJSON *axis = item->child; axis != nullptr; axis = axis->next) {
        bool ok = false;
        for (const char *name : {"+x", "-x", "+y", "-y", "+z", "-z"}) ok = ok || string_is(axis, name);
        if (!ok) return false;
    }
    return true;
}

bool numeric_array(const cJSON *item, int size)
{
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) != size) return false;
    for (const cJSON *v = item->child; v != nullptr; v = v->next) {
        if (!is_finite_number(v)) return false;
    }
    return true;
}

bool valid_mag_cal(const cJSON *item)
{
    if (!cJSON_IsObject(item) || unexpected_member(item, {"offset", "soft_iron"}) != nullptr) return false;
    const cJSON *offset = member(item, "offset");
    const cJSON *soft = member(item, "soft_iron");
    if (!numeric_array(offset, 3)) return false;
    if (!cJSON_IsArray(soft) || cJSON_GetArraySize(soft) != 2) return false;
    for (const cJSON *row = soft->child; row != nullptr; row = row->next) {
        if (!numeric_array(row, 2)) return false;
    }
    return true;
}

bool valid_sun_cal(const cJSON *item)
{
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) < 2) return false;
    for (const cJSON *point = item->child; point != nullptr; point = point->next) {
        if (!cJSON_IsObject(point) || unexpected_member(point, {"d", "deg"}) != nullptr) return false;
        if (!in_range(member(point, "d"), -1.0, 1.0) || !in_range(member(point, "deg"), -90.0, 90.0)) return false;
    }
    return true;
}

// Checks one params member against the schema and converts it. Returns false
// with `unknown` set for a key the schema does not have.
bool param_from_wire(const cJSON *item, ParamValue &value, bool &unknown)
{
    unknown = false;
    const char *key = item->string;
    auto number = [&](double lo, double hi) {
        if (!in_range(item, lo, hi)) return false;
        value.type = ParamValue::Type::Number;
        value.number = item->valuedouble;
        return true;
    };
    auto integer = [&](double lo, double hi) { return is_integer(item) && number(lo, hi); };
    auto one_of = [&](std::initializer_list<double> allowed) {
        if (!is_finite_number(item)) return false;
        for (double a : allowed) {
            if (item->valuedouble == a) return number(a, a);
        }
        return false;
    };
    // Keys the supervisor does not implement yet (Set 5) still get the full schema
    // check here; the supervisor then answers "not implemented: <key>".
    auto deferred = [&](bool schema_ok) {
        if (!schema_ok) return false;
        value.type = ParamValue::Type::Number;
        value.number = 0.0;
        return true;
    };
    constexpr double kInf = HUGE_VAL;

    if (std::strcmp(key, "k_psi") == 0) return number(0.0, 10.0);
    if (std::strcmp(key, "k_omega") == 0) return number(0.0, 0.05);
    if (std::strcmp(key, "k_i") == 0) return number(0.0, 0.1);
    if (std::strcmp(key, "rate_gain_scale") == 0) return number(0.0, 1.0);
    if (std::strcmp(key, "wheel_kp") == 0) return number(0.0, kInf);
    if (std::strcmp(key, "wheel_ki") == 0) return number(0.0, kInf);
    if (std::strcmp(key, "ff_table") == 0) return valid_ff_table(item, value);
    if (std::strcmp(key, "omega_low_rpm") == 0) return number(500.0, 1500.0);
    if (std::strcmp(key, "omega_bias_rpm") == 0) return number(0.0, kInf);
    // Bench-only overrides (R3.8); 0 clears them. The box checks the ranges that
    // depend on other parameters (below omega_low; between bias and cap).
    if (std::strcmp(key, "bench_underspeed_rpm") == 0) return number(0.0, 1500.0);
    if (std::strcmp(key, "bench_cap_rpm") == 0) return number(0.0, 4000.0);
    if (std::strcmp(key, "s_w") == 0 || std::strcmp(key, "s_gyro") == 0) return one_of({-1.0, 1.0});
    if (std::strcmp(key, "dshot_rate") == 0) return one_of({150.0, 300.0});
    if (std::strcmp(key, "bench_mode") == 0 || std::strcmp(key, "hall_installed") == 0) {
        if (!cJSON_IsBool(item)) return false;
        value.type = ParamValue::Type::Bool;
        value.flag = cJSON_IsTrue(item);
        return true;
    }
    if (std::strcmp(key, "manual_throttle_min") == 0 || std::strcmp(key, "manual_throttle_max") == 0 ||
        std::strcmp(key, "spinup_throttle") == 0 || std::strcmp(key, "hall_expect_throttle") == 0) {
        return integer(48.0, 2047.0);
    }
    if (std::strcmp(key, "mag_axis_map") == 0) return deferred(valid_mag_axis_map(item));
    if (std::strcmp(key, "mag_cal") == 0) return deferred(valid_mag_cal(item));
    if (std::strcmp(key, "sun_cal") == 0) return deferred(valid_sun_cal(item));
    if (std::strcmp(key, "light_threshold") == 0) return deferred(in_range(item, 0.0, kInf));
    unknown = true;
    return false;
}

void parse_set_params(const cJSON *root, Inbound &out)
{
    const cJSON *params = member(root, "params");
    if (params == nullptr) return reject(out, out.command.seq, "missing field: params");
    if (!cJSON_IsObject(params) || member_count(params) == 0) {
        return reject(out, out.command.seq, "invalid field: params");
    }
    for (const cJSON *item = params->child; item != nullptr; item = item->next) {
        if (item->string == nullptr) return reject(out, out.command.seq, "invalid field: params");
        if (out.params.count >= ParamUpdate::kMaxKeys) return reject(out, out.command.seq, "too many params");
        ParamKeyValue &kv = out.params.items[out.params.count];
        kv = ParamKeyValue{};
        bool unknown = false;
        if (!param_from_wire(item, kv.value, unknown)) {
            return reject(out, out.command.seq, unknown ? "unknown param: %.40s" : "invalid param: %.40s",
                          item->string);
        }
        std::snprintf(kv.key, sizeof kv.key, "%s", item->string);
        ++out.params.count;
    }
    // Echoed as ack.applied on success; cJSON asks for 5 spare bytes.
    if (!cJSON_PrintPreallocated(const_cast<cJSON *>(params), out.params_json,
                                 static_cast<int>(sizeof out.params_json) - 5, false)) {
        out.params_json[0] = '\0';
    }
    out.command.kind = CommandKind::SetParams;
    out.command.params = nullptr;
    out.kind = InboundKind::Command;
}

// ---- cmd -----------------------------------------------------------------------

void parse_cmd(const cJSON *root, Inbound &out)
{
    const uint32_t seq = out.command.seq;
    const cJSON *cmd = member(root, "cmd");
    if (cmd == nullptr) return reject(out, seq, "missing field: cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == nullptr) return reject(out, seq, "invalid field: cmd");
    const char *name = cmd->valuestring;

    auto only = [&](std::initializer_list<const char *> allowed) {
        const char *extra = unexpected_member(root, allowed);
        if (extra != nullptr) {
            reject(out, seq, "unexpected field: %.40s", extra);
            return false;
        }
        return true;
    };
    auto accept = [&](CommandKind kind) {
        out.command.kind = kind;
        out.kind = InboundKind::Command;
    };

    if (std::strcmp(name, "arm") == 0 || std::strcmp(name, "disarm") == 0 || std::strcmp(name, "estop") == 0 ||
        std::strcmp(name, "zero_heading") == 0) {
        if (!only({"v", "type", "seq", "cmd"})) return;
        if (std::strcmp(name, "arm") == 0) return accept(CommandKind::Arm);
        if (std::strcmp(name, "disarm") == 0) return accept(CommandKind::Disarm);
        if (std::strcmp(name, "estop") == 0) return accept(CommandKind::Estop);
        return accept(CommandKind::ZeroHeading);
    }

    if (std::strcmp(name, "mode") == 0) {
        if (!only({"v", "type", "seq", "cmd", "mode", "psi_ref_deg", "input"})) return;
        const cJSON *mode = member(root, "mode");
        if (mode == nullptr) return reject(out, seq, "missing field: mode");
        Mode target = Mode::Ready;
        if (!cJSON_IsString(mode) || mode->valuestring == nullptr || !mode_from_wire(mode->valuestring, target)) {
            return reject(out, seq, "invalid field: mode");
        }
        const cJSON *psi_ref = member(root, "psi_ref_deg");
        const cJSON *input = member(root, "input");
        if (target == Mode::Slew) {
            if (psi_ref == nullptr) return reject(out, seq, "missing field: psi_ref_deg");
            if (!is_finite_number(psi_ref) || !(psi_ref->valuedouble > -180.0 && psi_ref->valuedouble <= 180.0)) {
                return reject(out, seq, "invalid field: psi_ref_deg");
            }
            out.command.psi_ref_deg = angle_from_wire(static_cast<float>(psi_ref->valuedouble));
        } else if (psi_ref != nullptr) {
            return reject(out, seq, "unexpected field: psi_ref_deg");
        }
        if (target == Mode::Manual) {
            if (input == nullptr) return reject(out, seq, "missing field: input");
            if (!manual_input_from_wire(input, out.command.manual_input)) {
                return reject(out, seq, "invalid field: input");
            }
        } else if (input != nullptr) {
            return reject(out, seq, "unexpected field: input");
        }
        out.command.target_mode = target;
        return accept(CommandKind::Mode);
    }

    if (std::strcmp(name, "manual") == 0) {
        if (!only({"v", "type", "seq", "cmd", "throttle", "wheel_rpm", "rate_dps"})) return;
        const cJSON *throttle = member(root, "throttle");
        const cJSON *wheel = member(root, "wheel_rpm");
        const cJSON *rate = member(root, "rate_dps");
        const int present = (throttle != nullptr) + (wheel != nullptr) + (rate != nullptr);
        if (present == 0) return reject(out, seq, "missing setpoint");
        if (present > 1) return reject(out, seq, "two setpoints");
        if (throttle != nullptr) {
            if (!is_integer(throttle) || !in_range(throttle, 0.0, 2047.0)) {
                return reject(out, seq, "invalid field: throttle");
            }
            out.command.setpoint_input = ManualInput::Throttle;
            out.command.setpoint = static_cast<float>(throttle->valuedouble);
        } else if (wheel != nullptr) {
            if (!is_finite_number(wheel) || wheel->valuedouble < 0.0) return reject(out, seq, "invalid field: wheel_rpm");
            out.command.setpoint_input = ManualInput::Wheel;
            out.command.setpoint = static_cast<float>(wheel->valuedouble);
        } else {
            if (!is_finite_number(rate)) return reject(out, seq, "invalid field: rate_dps");
            out.command.setpoint_input = ManualInput::Rate;
            out.command.setpoint = signed_from_wire(static_cast<float>(rate->valuedouble));
        }
        return accept(CommandKind::Manual);
    }

    if (std::strcmp(name, "respin") == 0) {
        if (!only({"v", "type", "seq", "cmd", "wheel_rpm"})) return;
        const cJSON *wheel = member(root, "wheel_rpm");
        if (wheel != nullptr) {
            if (!is_finite_number(wheel) || wheel->valuedouble < 0.0) return reject(out, seq, "invalid field: wheel_rpm");
            out.command.has_wheel_rpm = true;
            out.command.wheel_rpm = static_cast<float>(wheel->valuedouble);
        }
        return accept(CommandKind::Respin);
    }

    if (std::strcmp(name, "set_params") == 0) {
        if (!only({"v", "type", "seq", "cmd", "params"})) return;
        return parse_set_params(root, out);
    }

    if (std::strcmp(name, "wheel_step") == 0) {
        if (!only({"v", "type", "seq", "cmd", "delta_rpm"})) return;
        const cJSON *delta = member(root, "delta_rpm");
        if (delta == nullptr) return reject(out, seq, "missing field: delta_rpm");
        if (!in_range(delta, -500.0, 500.0)) return reject(out, seq, "invalid field: delta_rpm");
        out.command.delta_rpm = static_cast<float>(delta->valuedouble);
        return accept(CommandKind::WheelStep);
    }

    return reject(out, seq, "unknown cmd");
}

bool only_whitespace(const char *p, const char *end)
{
    for (; p < end; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return false;
    }
    return true;
}

}  // namespace

void parse_inbound(const char *data, std::size_t len, uint32_t client, Inbound &out)
{
    reset(out, client);
    if (data == nullptr) return reject(out, 0, "malformed JSON");
    if (len > kMaxInboundBytes) return reject(out, 0, "too large");

    JsonDoc doc;
    const char *end = nullptr;
    doc.root = cJSON_ParseWithLengthOpts(data, len, &end, false);
    if (doc.root == nullptr || !cJSON_IsObject(doc.root) || end == nullptr || !only_whitespace(end, data + len)) {
        return reject(out, 0, "malformed JSON");
    }
    const cJSON *root = doc.root;

    // seq first, so every later refusal can echo it (FD finding V-14).
    const cJSON *seq = member(root, "seq");
    if (seq == nullptr) return reject(out, 0, "missing field: seq");
    if (!is_integer(seq) || !in_range(seq, 0.0, kSeqMax)) return reject(out, 0, "invalid seq");
    out.command.seq = static_cast<uint32_t>(seq->valuedouble);

    const cJSON *v = member(root, "v");
    if (v == nullptr) return reject(out, out.command.seq, "missing v");
    if (!is_finite_number(v) || v->valuedouble != 1.0) return reject(out, out.command.seq, "unsupported v");

    const cJSON *type = member(root, "type");
    if (type == nullptr) return reject(out, out.command.seq, "missing field: type");
    if (!cJSON_IsString(type) || type->valuestring == nullptr) return reject(out, out.command.seq, "invalid field: type");

    if (string_is(type, "ping")) {
        const char *extra = unexpected_member(root, {"v", "type", "seq", "t_client_ms"});
        if (extra != nullptr) return reject(out, out.command.seq, "unexpected field: %.40s", extra);
        const cJSON *t = member(root, "t_client_ms");
        if (t == nullptr) return reject(out, out.command.seq, "missing field: t_client_ms");
        if (!is_finite_number(t) || t->valuedouble < 0.0) {
            return reject(out, out.command.seq, "invalid field: t_client_ms");
        }
        out.t_client_ms = t->valuedouble;
        out.command.kind = CommandKind::Ping;
        out.kind = InboundKind::Ping;
        return;
    }
    if (string_is(type, "cmd")) return parse_cmd(root, out);
    for (const char *box_type : {"tlm", "ack", "pong", "event"}) {
        if (string_is(type, box_type)) return reject(out, out.command.seq, "unexpected type %s", box_type);
    }
    return reject(out, out.command.seq, "unknown type");
}

// ---------------------------------------------------------------------------
// Outbound
// ---------------------------------------------------------------------------

namespace {

class Writer {
public:
    Writer(char *buf, std::size_t cap) : buf_(buf), cap_(cap)
    {
        if (cap_ > 0) buf_[0] = '\0';
        ok_ = cap_ > 0;
    }

    void raw(const char *s) { append(s, std::strlen(s)); }

    void fmt(const char *format, ...) __attribute__((format(printf, 2, 3)))
    {
        if (!ok_) return;
        va_list args;
        va_start(args, format);
        const int n = std::vsnprintf(buf_ + len_, cap_ - len_, format, args);
        va_end(args);
        if (n < 0 || static_cast<std::size_t>(n) >= cap_ - len_) {
            ok_ = false;
            return;
        }
        len_ += static_cast<std::size_t>(n);
    }

    // A JSON string, escaped.
    void str(const char *s)
    {
        raw("\"");
        for (const char *p = s; p != nullptr && *p != '\0'; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '"') raw("\\\"");
            else if (c == '\\') raw("\\\\");
            else if (c < 0x20) fmt("\\u%04x", c);
            else append(p, 1);
        }
        raw("\"");
    }

    // A number with `decimals` places; non-finite becomes 0 and -0 becomes 0, so
    // the output is always valid JSON and never reads "-0.00".
    void num(double v, int decimals)
    {
        if (!std::isfinite(v)) v = 0.0;
        const double scale = std::pow(10.0, decimals);
        double r = std::round(v * scale) / scale;
        if (r == 0.0) r = 0.0;
        fmt("%.*f", decimals, r);
    }

    std::size_t finish() const { return ok_ ? len_ : 0; }

private:
    void append(const char *s, std::size_t n)
    {
        if (!ok_) return;
        if (n >= cap_ - len_) {
            ok_ = false;
            return;
        }
        std::memcpy(buf_ + len_, s, n);
        len_ += n;
        buf_[len_] = '\0';
    }

    char *buf_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool ok_ = true;
};

int legal_throttle(int throttle)
{
    if (throttle > kDshotMaxThrottle) return kDshotMaxThrottle;
    if (throttle < kDshotMinThrottle) return 0;
    return throttle;
}

}  // namespace

std::size_t encode_tlm(const TelemetrySnapshot &snap, uint32_t seq, uint32_t tlm_dropped, char *buf,
                       std::size_t cap)
{
    Writer w(buf, cap);
    const int64_t t_ms = snap.t_us > 0 ? snap.t_us / 1000 : 0;
    w.fmt("{\"v\":1,\"type\":\"tlm\",\"seq\":%lu,\"t_ms\":%lld,\"mode\":", static_cast<unsigned long>(seq),
          static_cast<long long>(t_ms));
    w.str(mode_wire_name(snap.mode));
    w.raw(",\"manual_input\":");
    const char *input = snap.mode == Mode::Manual ? manual_input_wire_name(snap.manual_input) : nullptr;
    if (input != nullptr) w.str(input);
    else w.raw("null");

    w.raw(",\"psi_deg\":");
    w.num(angle_to_wire(snap.psi_deg), 2);
    // A heading reference exists only in HOLD and SLEW (R4.3), and there is no
    // calibrated magnetometer heading or sun angle until Set 5: null, never a
    // fake number.
    w.raw(",\"psi_ref_deg\":");
    if (snap.psi_ref_valid) w.num(angle_to_wire(snap.psi_ref_deg), 2);
    else w.raw("null");
    w.raw(",\"rate_dps\":");
    w.num(signed_to_wire(snap.rate_dps), 2);
    w.raw(",\"mag_deg\":null,\"sun_deg\":null,\"sun_valid\":false,\"wheel_rpm\":");
    w.num(snap.wheel_rpm > 0.0f ? snap.wheel_rpm : 0.0f, 1);
    w.raw(",\"wheel_ref_rpm\":");
    w.num(snap.wheel_ref_rpm > 0.0f ? snap.wheel_ref_rpm : 0.0f, 1);
    w.fmt(",\"throttle\":%d,\"throttle_band\":", legal_throttle(snap.throttle));
    // null until the control task has filled the band from the parameters.
    if (snap.throttle_band_lo >= kDshotMinThrottle && snap.throttle_band_hi >= snap.throttle_band_lo &&
        snap.throttle_band_hi <= kDshotMaxThrottle) {
        w.fmt("[%d,%d]", snap.throttle_band_lo, snap.throttle_band_hi);
    } else {
        w.raw("null");
    }
    w.raw(",\"tau_cmd_mNm\":");
    w.num(signed_to_wire(snap.tau_cmd_mNm), 3);
    w.raw(",\"vbat\":");
    w.num(snap.vbat_v > 0.0f ? snap.vbat_v : 0.0f, 2);
    w.fmt(",\"loop_us\":%lu,\"flags\":[", static_cast<unsigned long>(snap.loop_us));

    bool first = true;
    auto flag = [&](const char *name) {
        if (!first) w.raw(",");
        first = false;
        w.str(name);
    };
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        if ((snap.flags & bit) != 0) {
            const char *name = flag_wire_name(bit);
            if (name != nullptr) flag(name);
        }
    }
    if (!snap.imu_present) flag("IMU_MISSING");
    if (!snap.mag_present) flag("MAG_MISSING");
    for (unsigned i = 0; i < static_cast<unsigned>(Interlock::Count); ++i) {
        const Interlock il = static_cast<Interlock>(i);
        if ((snap.latched & interlock_bit(il)) != 0) flag(interlock_wire_name(il));
    }
    w.raw("],\"t_c\":");
    if (snap.baro_valid) w.num(snap.baro_t_c, 2);
    else w.raw("null");
    w.raw(",\"p_pa\":");
    if (snap.baro_valid) w.num(snap.baro_p_pa, 1);
    else w.raw("null");
    w.fmt(",\"tlm_dropped\":%lu,\"dropped_events\":%lu", static_cast<unsigned long>(tlm_dropped),
          static_cast<unsigned long>(snap.dropped_events));

    if (snap.bench_mode) {
        // Raw counts in the sensors' own axes, as the schema says: not converted.
        w.fmt(",\"gyro_raw\":[%d,%d,%d],\"mag_raw\":[%d,%d,%d],\"sun_a_mv\":%d,\"sun_b_mv\":%d", snap.gyro_raw[0],
              snap.gyro_raw[1], snap.gyro_raw[2], snap.mag_raw[0], snap.mag_raw[1], snap.mag_raw[2],
              snap.sun_a_mv > 0 ? snap.sun_a_mv : 0, snap.sun_b_mv > 0 ? snap.sun_b_mv : 0);
        w.raw(",\"accel_rms_g\":");
        w.num(snap.accel_rms_g > 0.0f ? snap.accel_rms_g : 0.0f, 4);
    }
    w.raw("}");
    return w.finish();
}

std::size_t encode_ack(const Ack &ack, const char *applied_json, char *buf, std::size_t cap)
{
    Writer w(buf, cap);
    w.fmt("{\"v\":1,\"type\":\"ack\",\"seq\":%lu,\"ok\":%s", static_cast<unsigned long>(ack.seq),
          ack.ok ? "true" : "false");
    if (!ack.ok) {
        w.raw(",\"reason\":");
        w.str(ack.reason[0] != '\0' ? ack.reason : "refused");
    } else if (applied_json != nullptr && applied_json[0] != '\0') {
        w.raw(",\"applied\":");
        w.raw(applied_json);
    }
    w.raw("}");
    return w.finish();
}

std::size_t encode_event(const Event &event, uint32_t seq, int64_t t_ms, char *buf, std::size_t cap)
{
    Writer w(buf, cap);
    w.fmt("{\"v\":1,\"type\":\"event\",\"seq\":%lu,\"t_ms\":%lld,", static_cast<unsigned long>(seq),
          static_cast<long long>(t_ms > 0 ? t_ms : 0));
    if (event.kind == Event::Kind::Mode) {
        w.raw("\"event\":\"mode\",\"mode\":");
        w.str(mode_wire_name(event.mode));
        w.raw(",\"cause\":");
        w.str(event.cause != nullptr && event.cause[0] != '\0' ? event.cause : "mode");
    } else {
        w.raw("\"event\":\"notice\",\"name\":");
        w.str(event.notice != nullptr ? event.notice : "");
        if (event.detail != nullptr && event.detail[0] != '\0') {
            w.raw(",\"detail\":");
            w.str(event.detail);
        }
    }
    w.raw("}");
    return w.finish();
}

std::size_t encode_pong(uint32_t seq, uint32_t ping_seq, double t_client_ms, int64_t t_ms, char *buf,
                        std::size_t cap)
{
    Writer w(buf, cap);
    if (!std::isfinite(t_client_ms) || t_client_ms < 0.0) t_client_ms = 0.0;
    w.fmt("{\"v\":1,\"type\":\"pong\",\"seq\":%lu,\"ping_seq\":%lu,\"t_client_ms\":%.15g,\"t_ms\":%lld}",
          static_cast<unsigned long>(seq), static_cast<unsigned long>(ping_seq), t_client_ms,
          static_cast<long long>(t_ms > 0 ? t_ms : 0));
    return w.finish();
}

}  // namespace direcsun::protocol
