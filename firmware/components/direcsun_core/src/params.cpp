#include "direcsun/core/params.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

// Mutation-test switch (host selftest builds only): drops the rule that keeps
// HALL_SILENT armed in SPINUP, so the HT-10 property test must notice.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_NO_HALL_EXPECT_RULE)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

namespace {

ParamsResult fail(const char *what, const char *key)
{
    ParamsResult r;
    r.ok = false;
    std::snprintf(r.reason, sizeof r.reason, "%s: %s", what, key);
    return r;
}

ParamsResult invalid(const char *key)
{
    return fail("invalid param", key);
}

bool finite_in(float v, float lo, float hi)
{
    return std::isfinite(v) && v >= lo && v <= hi;
}

bool is_number(const ParamValue &v)
{
    return v.type == ParamValue::Type::Number && std::isfinite(v.number);
}

bool is_integer(const ParamValue &v)
{
    return is_number(v) && std::floor(v.number) == v.number;
}

bool key_is(const ParamKeyValue &kv, const char *name)
{
    return std::strncmp(kv.key, name, ParamKeyValue::kMaxKey) == 0;
}

// Keys of FD section 16 that later sets implement (Set 5: heading and sun).
constexpr const char *kDeferredKeys[] = {"mag_axis_map", "mag_cal", "sun_cal", "light_threshold"};

}  // namespace

ParamsResult validate_params(const Params &p)
{
    if (!finite_in(p.k_psi, 0.0f, 10.0f)) return invalid("k_psi");
    if (!finite_in(p.k_omega, 0.0f, 0.05f)) return invalid("k_omega");
    if (!finite_in(p.k_i, 0.0f, 0.1f)) return invalid("k_i");
    if (!finite_in(p.wheel_kp, 0.0f, 10.0f)) return invalid("wheel_kp");
    if (!finite_in(p.wheel_ki, 0.0f, 10.0f)) return invalid("wheel_ki");

    if (p.ff_count != 0 && p.ff_count != kFfPoints) return invalid("ff_table");
    for (std::size_t i = 0; i < p.ff_count; ++i) {
        if (!finite_in(p.ff_rpm[i], 0.0f, 10000.0f)) return invalid("ff_table");
        if (!finite_in(p.ff_throttle[i], 48.0f, 2047.0f)) return invalid("ff_table");
        if (std::floor(p.ff_throttle[i]) != p.ff_throttle[i]) return invalid("ff_table");
        if (i > 0 && !(p.ff_rpm[i] > p.ff_rpm[i - 1])) return invalid("ff_table");
        if (i > 0 && p.ff_throttle[i] < p.ff_throttle[i - 1]) return invalid("ff_table");
    }

    if (!finite_in(p.omega_cap_rpm, 0.0f, kOmegaCapMaxRpm)) return invalid("omega_cap_rpm");
    if (!finite_in(p.omega_low_rpm, 500.0f, 1500.0f)) return invalid("omega_low_rpm");
    if (!finite_in(p.omega_bias_rpm, p.omega_low_rpm + 400.0f, p.omega_cap_rpm - 400.0f)) {
        return invalid("omega_bias_rpm");
    }

    if (p.s_w != 1 && p.s_w != -1) return invalid("s_w");
    if (p.s_gyro != 1 && p.s_gyro != -1) return invalid("s_gyro");
    if (p.dshot_rate != 150 && p.dshot_rate != 300) return invalid("dshot_rate");

    if (p.manual_throttle_min < 48 || p.manual_throttle_min > 2047) return invalid("manual_throttle_min");
    if (p.manual_throttle_max < 48 || p.manual_throttle_max > 2047) return invalid("manual_throttle_max");
    if (p.manual_throttle_min >= p.manual_throttle_max) return invalid("manual_throttle_min");
    if (p.spinup_throttle < 48 || p.spinup_throttle > 2047) return invalid("spinup_throttle");
    if (p.hall_expect_throttle < 48 || p.hall_expect_throttle > 2047) return invalid("hall_expect_throttle");
#ifndef DIRECSUN_MUTANT_NO_HALL_EXPECT_RULE
    // HALL_SILENT must arm in every SPINUP (finding V-64). SPINUP is exempt from
    // UNDERSPEED, so with the Hall fitted but silent HALL_SILENT is the only
    // stop (V-11), and it arms only above hall_expect_throttle while rung 1's
    // ramp tops out at spinup_throttle. The intended values agree: the threshold
    // is FF(omega_low), and spinup_throttle drives the wheel toward the bias, at
    // least 400 rpm faster.
    if (p.hall_expect_throttle >= p.spinup_throttle) return invalid("hall_expect_throttle");
#endif

    // The Hall bench bypass exists only inside bench mode.
    if (!p.hall_installed && !p.bench_mode) return fail("bench_mode required", "hall_installed");

    // Bench-only overrides (R3.8): 0 when unused; otherwise only in bench mode,
    // and each may only make its limit more conservative for its test.
    if (p.bench_underspeed_rpm != 0.0f) {
        if (!p.bench_mode) return fail("bench_mode required", "bench_underspeed_rpm");
        if (!finite_in(p.bench_underspeed_rpm, 100.0f, p.omega_low_rpm)) return invalid("bench_underspeed_rpm");
    }
    if (p.bench_cap_rpm != 0.0f) {
        if (!p.bench_mode) return fail("bench_mode required", "bench_cap_rpm");
        if (!finite_in(p.bench_cap_rpm, p.omega_bias_rpm, p.omega_cap_rpm)) return invalid("bench_cap_rpm");
    }

    return ParamsResult{};
}

float effective_cap_rpm(const Params &params)
{
    return params.bench_cap_rpm > 0.0f && params.bench_cap_rpm < params.omega_cap_rpm ? params.bench_cap_rpm
                                                                                       : params.omega_cap_rpm;
}

float effective_underspeed_rpm(const Params &params)
{
    return params.bench_underspeed_rpm > 0.0f ? params.bench_underspeed_rpm : params.omega_low_rpm;
}

ParamsResult apply_param_update(const Params &current, const ParamUpdate &update, Params &out)
{
    if (update.count == 0 || update.count > ParamUpdate::kMaxKeys) {
        ParamsResult r;
        r.ok = false;
        std::snprintf(r.reason, sizeof r.reason, "no params");
        return r;
    }

    Params c = current;
    for (std::size_t i = 0; i < update.count; ++i) {
        const ParamKeyValue &kv = update.items[i];
        const ParamValue &v = kv.value;

        for (const char *deferred : kDeferredKeys) {
            if (key_is(kv, deferred)) return fail("not implemented", deferred);
        }
        if (key_is(kv, "omega_cap_rpm")) return fail("not settable", "omega_cap_rpm");

        auto set_float = [&](const char *name, float &field) -> bool {
            if (!key_is(kv, name)) return false;
            field = is_number(v) ? static_cast<float>(v.number) : NAN;
            return true;
        };
        auto set_u16 = [&](const char *name, uint16_t &field, bool &bad) -> bool {
            if (!key_is(kv, name)) return false;
            if (!is_integer(v) || v.number < 0.0 || v.number > 65535.0) {
                bad = true;
            } else {
                field = static_cast<uint16_t>(v.number);
            }
            return true;
        };
        auto set_sign = [&](const char *name, int8_t &field, bool &bad) -> bool {
            if (!key_is(kv, name)) return false;
            if (is_integer(v) && (v.number == 1.0 || v.number == -1.0)) {
                field = static_cast<int8_t>(v.number);
            } else {
                bad = true;
            }
            return true;
        };
        auto set_bool = [&](const char *name, bool &field, bool &bad) -> bool {
            if (!key_is(kv, name)) return false;
            if (v.type == ParamValue::Type::Bool) {
                field = v.flag;
            } else {
                bad = true;
            }
            return true;
        };

        bool bad = false;
        if (set_float("k_psi", c.k_psi) || set_float("k_omega", c.k_omega) || set_float("k_i", c.k_i) ||
            set_float("wheel_kp", c.wheel_kp) || set_float("wheel_ki", c.wheel_ki) ||
            set_float("omega_low_rpm", c.omega_low_rpm) || set_float("omega_bias_rpm", c.omega_bias_rpm) ||
            set_float("bench_underspeed_rpm", c.bench_underspeed_rpm) ||
            set_float("bench_cap_rpm", c.bench_cap_rpm)) {
            // Non-numbers became NaN and fail validation below with this key.
            if (!is_number(v)) return invalid(kv.key);
            continue;
        }
        if (set_u16("dshot_rate", c.dshot_rate, bad) ||
            set_u16("manual_throttle_min", c.manual_throttle_min, bad) ||
            set_u16("manual_throttle_max", c.manual_throttle_max, bad) ||
            set_u16("spinup_throttle", c.spinup_throttle, bad) ||
            set_u16("hall_expect_throttle", c.hall_expect_throttle, bad) ||
            set_sign("s_w", c.s_w, bad) || set_sign("s_gyro", c.s_gyro, bad)) {
            if (bad) return invalid(kv.key);
            continue;
        }
        if (set_bool("bench_mode", c.bench_mode, bad)) {
            if (bad) return invalid(kv.key);
            continue;
        }
        if (set_bool("hall_installed", c.hall_installed, bad)) {
            if (bad) return invalid(kv.key);
            // The bypass can only be switched on from bench mode that is already
            // on: two deliberate commands, never one [D].
            if (!c.hall_installed && !current.bench_mode) return fail("bench_mode required", "hall_installed");
            continue;
        }
        if (key_is(kv, "ff_table")) {
            if (v.type != ParamValue::Type::Array || v.array_len != 2 * kFfPoints) return invalid("ff_table");
            c.ff_count = static_cast<uint8_t>(kFfPoints);
            for (std::size_t k = 0; k < kFfPoints; ++k) {
                c.ff_rpm[k] = v.array[2 * k];
                c.ff_throttle[k] = v.array[2 * k + 1];
            }
            continue;
        }
        return fail("unknown param", kv.key);
    }

    // Turning bench mode off clears the bench-only overrides with it [D], unless
    // the same update also names one: that contradiction is refused below by
    // validation ("bench_mode required"), never silently dropped.
    if (current.bench_mode && !c.bench_mode) {
        bool names_underspeed = false;
        bool names_cap = false;
        for (std::size_t i = 0; i < update.count; ++i) {
            names_underspeed = names_underspeed || key_is(update.items[i], "bench_underspeed_rpm");
            names_cap = names_cap || key_is(update.items[i], "bench_cap_rpm");
        }
        if (!names_underspeed) c.bench_underspeed_rpm = 0.0f;
        if (!names_cap) c.bench_cap_rpm = 0.0f;
    }

    ParamsResult r = validate_params(c);
    if (r.ok) out = c;
    return r;
}

// ---------------------------------------------------------------------------
// Persistence: a fixed little-endian layout of the non-volatile fields.
// ---------------------------------------------------------------------------

namespace {

constexpr const char *kKeyVersion = "param_version";
constexpr const char *kKeyBlob = "params";
constexpr std::size_t kBlobSize = 5 * 4 + 1 + 2 * kFfPoints * 4 + 2 * 4 + 2 * 1 + 5 * 2;

class Writer {
public:
    explicit Writer(uint8_t *buf) : buf_(buf) {}
    void u8(uint8_t v) { buf_[n_++] = v; }
    void u16(uint16_t v)
    {
        u8(static_cast<uint8_t>(v & 0xFF));
        u8(static_cast<uint8_t>(v >> 8));
    }
    void f32(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>(bits >> (8 * i)));
    }
    std::size_t size() const { return n_; }

private:
    uint8_t *buf_;
    std::size_t n_ = 0;
};

class Reader {
public:
    Reader(const uint8_t *buf, std::size_t len) : buf_(buf), len_(len) {}
    bool u8(uint8_t &v)
    {
        if (n_ >= len_) return false;
        v = buf_[n_++];
        return true;
    }
    bool u16(uint16_t &v)
    {
        uint8_t lo, hi;
        if (!u8(lo) || !u8(hi)) return false;
        v = static_cast<uint16_t>(lo | (hi << 8));
        return true;
    }
    bool f32(float &v)
    {
        uint32_t bits = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t b;
            if (!u8(b)) return false;
            bits |= static_cast<uint32_t>(b) << (8 * i);
        }
        std::memcpy(&v, &bits, sizeof v);
        return true;
    }
    bool at_end() const { return n_ == len_; }

private:
    const uint8_t *buf_;
    std::size_t len_;
    std::size_t n_ = 0;
};

void encode(const Params &p, Writer &w)
{
    w.f32(p.k_psi);
    w.f32(p.k_omega);
    w.f32(p.k_i);
    w.f32(p.wheel_kp);
    w.f32(p.wheel_ki);
    w.u8(p.ff_count);
    for (float v : p.ff_rpm) w.f32(v);
    for (float v : p.ff_throttle) w.f32(v);
    w.f32(p.omega_low_rpm);
    w.f32(p.omega_bias_rpm);
    w.u8(static_cast<uint8_t>(p.s_w));
    w.u8(static_cast<uint8_t>(p.s_gyro));
    w.u16(p.dshot_rate);
    w.u16(p.manual_throttle_min);
    w.u16(p.manual_throttle_max);
    w.u16(p.spinup_throttle);
    w.u16(p.hall_expect_throttle);
}

bool decode(Reader &r, Params &p)
{
    uint8_t sw = 0, sg = 0;
    bool ok = r.f32(p.k_psi) && r.f32(p.k_omega) && r.f32(p.k_i) && r.f32(p.wheel_kp) && r.f32(p.wheel_ki) &&
              r.u8(p.ff_count);
    for (float &v : p.ff_rpm) ok = ok && r.f32(v);
    for (float &v : p.ff_throttle) ok = ok && r.f32(v);
    ok = ok && r.f32(p.omega_low_rpm) && r.f32(p.omega_bias_rpm) && r.u8(sw) && r.u8(sg) && r.u16(p.dshot_rate) &&
         r.u16(p.manual_throttle_min) && r.u16(p.manual_throttle_max) && r.u16(p.spinup_throttle) &&
         r.u16(p.hall_expect_throttle);
    p.s_w = static_cast<int8_t>(sw);
    p.s_gyro = static_cast<int8_t>(sg);
    return ok && r.at_end();
}

}  // namespace

ParamsLoad load_params(KvStore &store, Params &out)
{
    const Params defaults{};
    ParamsLoad result;

    uint32_t version = 0;
    if (!store.read_u32(kKeyVersion, version)) {
        out = defaults;
        result.reset = true;
        result.detail = "missing";
        return result;
    }
    if (version != kParamVersion) {
        out = defaults;
        result.reset = true;
        result.detail = "version";
        return result;
    }

    uint8_t buf[kBlobSize + 1] = {};
    std::size_t len = sizeof buf;
    Params loaded = defaults;  // volatile fields stay at their defaults
    if (!store.read_blob(kKeyBlob, buf, len) || len != kBlobSize) {
        out = defaults;
        result.reset = true;
        result.detail = "invalid";
        return result;
    }
    Reader reader(buf, len);
    if (!decode(reader, loaded) || !validate_params(loaded).ok) {
        out = defaults;
        result.reset = true;
        result.detail = "invalid";
        return result;
    }
    out = loaded;
    return result;
}

bool save_params(KvStore &store, const Params &params)
{
    uint8_t buf[kBlobSize];
    Writer w(buf);
    encode(params, w);
    if (w.size() != kBlobSize) return false;
    return store.write_blob(kKeyBlob, buf, kBlobSize) && store.write_u32(kKeyVersion, kParamVersion) &&
           store.commit();
}

}  // namespace direcsun
