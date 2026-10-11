// Parameters (FD section 16): validation, all-or-nothing updates, the cap
// ceiling, and persistence through the abstract store.

#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace harness;

TEST_CASE("params: the defaults validate")
{
    CHECK(validate_params(Params{}).ok);
}

TEST_CASE("params: one out-of-range key rejects the whole set and applies nothing")
{
    Rig rig;
    ParamUpdate u;
    add_number(u, "k_psi", 3.5);      // valid on its own
    add_number(u, "k_omega", 0.06);   // above 0.05
    rig.tick({set_params_cmd(&u)});
    CHECK(rig.last_reason() == "invalid param: k_omega");
    CHECK_FALSE(rig.out.params_changed);
    CHECK(rig.sup.params().k_psi == doctest::Approx(3.1f));
    CHECK(rig.sup.params().k_omega == doctest::Approx(0.0129f));
}

TEST_CASE("params: a valid set applies and asks to be persisted")
{
    Rig rig;
    ParamUpdate u;
    add_number(u, "k_psi", 3.5);
    add_number(u, "spinup_throttle", 250);
    rig.tick({set_params_cmd(&u)});
    CHECK(rig.last_ok());
    CHECK(rig.out.params_changed);
    CHECK(rig.sup.params().k_psi == doctest::Approx(3.5f));
    CHECK(rig.sup.params().spinup_throttle == 250);
}

TEST_CASE("params: cross-key, type, and unknown-key errors name the key")
{
    struct Bad {
        const char *key;
        double value;
        const char *reason;
    };
    const Bad bad[] = {
        {"omega_bias_rpm", 1000.0, "invalid param: omega_bias_rpm"},    // below low + 400
        {"omega_low_rpm", 400.0, "invalid param: omega_low_rpm"},
        {"manual_throttle_min", 500.0, "invalid param: manual_throttle_min"},  // not below max (400)
        {"manual_throttle_max", 30.0, "invalid param: manual_throttle_max"},
        {"dshot_rate", 600.0, "invalid param: dshot_rate"},
        {"s_w", 0.0, "invalid param: s_w"},
        {"spinup_throttle", 47.5, "invalid param: spinup_throttle"},
        {"no_such_key", 1.0, "unknown param: no_such_key"},
        {"mag_cal", 1.0, "not implemented: mag_cal"},
    };
    for (const Bad &b : bad) {
        CAPTURE(b.key);
        Rig rig;
        ParamUpdate u;
        add_number(u, b.key, b.value);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_reason() == b.reason);
    }
    Rig rig;
    ParamUpdate u;
    add_bool(u, "k_psi", true);
    rig.tick({set_params_cmd(&u)});
    CHECK(rig.last_reason() == "invalid param: k_psi");
}

TEST_CASE("params: hall_expect_throttle below spinup_throttle by every route (V-64)")
{
    // Direct validation: the review's 2047/2047 set, equality, and one below.
    Params p;
    p.spinup_throttle = 2047;
    p.hall_expect_throttle = 2047;
    p.manual_throttle_max = 2047;
    ParamsResult r = validate_params(p);
    CHECK_FALSE(r.ok);
    CHECK(std::string(r.reason) == "invalid param: hall_expect_throttle");
    p.spinup_throttle = 300;
    p.hall_expect_throttle = 300;
    CHECK_FALSE(validate_params(p).ok);
    p.hall_expect_throttle = 299;
    CHECK(validate_params(p).ok);

    // Over the wire: refused whole, nothing applied, nothing persisted.
    {
        Rig rig;
        ParamUpdate u;
        add_number(u, "spinup_throttle", 2047);
        add_number(u, "hall_expect_throttle", 2047);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_reason() == "invalid param: hall_expect_throttle");
        CHECK_FALSE(rig.out.params_changed);
        CHECK(rig.sup.params().spinup_throttle == 300);
        CHECK(rig.sup.params().hall_expect_throttle == 200);
    }
    {
        // Lowering the target alone to the stored threshold is refused too.
        Rig rig;
        ParamUpdate u;
        add_number(u, "spinup_throttle", 200);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_reason() == "invalid param: hall_expect_throttle");
        CHECK(rig.sup.params().spinup_throttle == 300);
    }
    {
        Rig rig;
        ParamUpdate u;
        add_number(u, "spinup_throttle", 2047);
        add_number(u, "hall_expect_throttle", 2046);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_ok());
        CHECK(rig.out.params_changed);
        CHECK(rig.sup.params().hall_expect_throttle == 2046);
    }

    // A stored set that breaks the rule loads the defaults, as any invalid set does.
    MemoryStore store;
    Params bad;
    bad.spinup_throttle = 2047;
    bad.hall_expect_throttle = 2047;
    REQUIRE(save_params(store, bad));
    Params loaded;
    const ParamsLoad load = load_params(store, loaded);
    CHECK(load.reset);
    CHECK(std::string(load.detail) == "invalid");
    CHECK(loaded.spinup_throttle == 300);
    CHECK(loaded.hall_expect_throttle == 200);
    // Positive control: the same store round-trips a set that keeps the rule.
    bad.hall_expect_throttle = 2046;
    REQUIRE(save_params(store, bad));
    CHECK_FALSE(load_params(store, loaded).reset);
    CHECK(loaded.hall_expect_throttle == 2046);

    // And a supervisor handed such a set runs on the defaults and says so.
    bad.hall_expect_throttle = 2047;
    Supervisor sup(bad);
    CHECK(sup.params().spinup_throttle == 300);
    CHECK(sup.params().hall_expect_throttle == 200);
    ScriptedController ctl;
    TickInputs in;
    in.vbat_avg_v = 10.4f;
    TickOutputs out;
    sup.step(in, ctl, out);
    REQUIRE(out.events.count == 1);
    CHECK(std::string(out.events.items[0].notice) == "PARAMS_RESET");
    CHECK(std::string(out.events.items[0].detail) == "invalid");
}

TEST_CASE("params: the feed-forward table must be monotonic")
{
    ParamUpdate u;
    ParamKeyValue &kv = u.items[u.count++];
    std::snprintf(kv.key, sizeof kv.key, "ff_table");
    kv.value.type = ParamValue::Type::Array;
    kv.value.array_len = 16;
    for (int i = 0; i < 8; ++i) {
        kv.value.array[2 * i] = 500.0f + 500.0f * static_cast<float>(i);
        kv.value.array[2 * i + 1] = 100.0f + 50.0f * static_cast<float>(i);
    }
    Params out;
    CHECK(apply_param_update(Params{}, u, out).ok);
    CHECK(out.ff_count == 8);
    kv.value.array[6] = kv.value.array[4];  // repeated rpm
    CHECK_FALSE(apply_param_update(Params{}, u, out).ok);
}

TEST_CASE("params: omega_cap_rpm = 4001 is rejected by every route")
{
    // Over the wire: the cap is not settable at all, lower or higher.
    for (double value : {4001.0, 4000.0, 3000.0}) {
        Rig rig;
        ParamUpdate u;
        add_number(u, "omega_cap_rpm", value);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_reason() == "not settable: omega_cap_rpm");
        CHECK(rig.sup.params().omega_cap_rpm == doctest::Approx(4000.0f));
    }
    // Direct validation.
    Params p;
    p.omega_cap_rpm = 4001.0f;
    CHECK_FALSE(validate_params(p).ok);
    // A supervisor handed such a set falls back to the defaults and says so.
    Supervisor sup(p);
    CHECK(sup.params().omega_cap_rpm == doctest::Approx(4000.0f));
    ScriptedController ctl;
    TickInputs in;
    TickOutputs out;
    sup.step(in, ctl, out);
    REQUIRE(out.events.count >= 1);
    CHECK(std::string(out.events.items[0].notice) == "PARAMS_RESET");
    // Persistence: the cap is not stored, so a load always yields the ceiling.
    MemoryStore store;
    Params saved;
    saved.k_psi = 2.0f;
    REQUIRE(save_params(store, saved));
    Params loaded;
    load_params(store, loaded);
    CHECK(loaded.omega_cap_rpm == doctest::Approx(kOmegaCapMaxRpm));
}

TEST_CASE("params: a param_version mismatch loads the defaults")
{
    MemoryStore store;
    Params saved;
    saved.k_psi = 2.0f;
    saved.manual_throttle_max = 380;
    REQUIRE(save_params(store, saved));

    Params loaded;
    ParamsLoad r = load_params(store, loaded);
    CHECK_FALSE(r.reset);
    CHECK(loaded.k_psi == doctest::Approx(2.0f));  // round trip, so the store does work
    CHECK(loaded.manual_throttle_max == 380);

    store.version = kParamVersion + 1;
    r = load_params(store, loaded);
    CHECK(r.reset);
    CHECK(std::string(r.detail) == "version");
    CHECK(loaded.k_psi == doctest::Approx(3.1f));
    CHECK(loaded.manual_throttle_max == 400);
}

TEST_CASE("params: missing, short, and out-of-range stored sets load the defaults")
{
    Params loaded;
    MemoryStore empty;
    ParamsLoad r = load_params(empty, loaded);
    CHECK(r.reset);
    CHECK(std::string(r.detail) == "missing");

    MemoryStore store;
    REQUIRE(save_params(store, Params{}));
    store.blob.pop_back();
    r = load_params(store, loaded);
    CHECK(r.reset);
    CHECK(std::string(r.detail) == "invalid");

    MemoryStore bad;
    Params p;
    p.k_omega = 0.04f;
    REQUIRE(save_params(bad, p));
    // Corrupt the stored k_omega (bytes 4..7) to 1.0f, which is out of range.
    const float one = 1.0f;
    std::memcpy(&bad.blob[4], &one, sizeof one);
    r = load_params(bad, loaded);
    CHECK(r.reset);
    CHECK(loaded.k_omega == doctest::Approx(0.0129f));
}

TEST_CASE("params: the volatile fields never persist")
{
    MemoryStore store;
    Params p;
    p.bench_mode = true;
    p.hall_installed = false;
    REQUIRE(save_params(store, p));
    Params loaded;
    load_params(store, loaded);
    CHECK_FALSE(loaded.bench_mode);
    CHECK(loaded.hall_installed);
}

TEST_CASE("params: PARAMS_RESET is posted once, on the first tick")
{
    Params p;
    Supervisor sup(p, true, "version");
    ScriptedController ctl;
    TickInputs in;
    in.vbat_avg_v = 10.4f;  // a charged battery, so no BATTERY_LOW event joins the notice
    TickOutputs out;
    sup.step(in, ctl, out);
    REQUIRE(out.events.count == 1);
    CHECK(out.events.items[0].kind == Event::Kind::Notice);
    CHECK(std::string(out.events.items[0].notice) == "PARAMS_RESET");
    CHECK(std::string(out.events.items[0].detail) == "version");
    in.tick = 1;
    sup.step(in, ctl, out);
    CHECK(out.events.count == 0);
}
