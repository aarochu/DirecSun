#pragma once

// Test harness for the supervisor: a scripted controller, a healthy-sensor
// baseline, and helpers that build commands and step ticks. Shared by
// test_modes.cpp, test_interlocks.cpp, test_params.cpp, and test_bypass.cpp.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/interlocks.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"

namespace harness {

using namespace direcsun;

// Returns `throttle` every call; counts calls and resets.
struct ScriptedController : ThrottleController {
    int throttle_value = 0;
    int calls = 0;
    int resets = 0;
    ControlDemand last_demand;

    int throttle(const ControlDemand &demand, const TickInputs &, const Params &) override
    {
        ++calls;
        last_demand = demand;
        return throttle_value;
    }
    void reset() override { ++resets; }
};

// A box whose T-5 feed-forward table is set, so the closed wheel loop is
// available: HOLD, SLEW, SUN_POINT, respin and MANUAL wheel/rate need it.
inline Params characterised_params()
{
    Params p;
    p.ff_count = static_cast<uint8_t>(kFfPoints);
    for (std::size_t i = 0; i < kFfPoints; ++i) {
        p.ff_rpm[i] = 500.0f * static_cast<float>(i + 1);
        p.ff_throttle[i] = 48.0f + p.ff_rpm[i] / 2.5f;
    }
    return p;
}

constexpr uint32_t kOperator = 7;   // the client that arms
constexpr uint32_t kViewer = 9;     // a second client

inline Command cmd(CommandKind kind, uint32_t client = kOperator, uint32_t seq = 1)
{
    Command c;
    c.kind = kind;
    c.client = client;
    c.seq = seq;
    return c;
}

inline Command mode_cmd(Mode target, ManualInput input = ManualInput::None, uint32_t client = kOperator,
                        uint32_t seq = 1)
{
    Command c = cmd(CommandKind::Mode, client, seq);
    c.target_mode = target;
    c.manual_input = input;
    return c;
}

inline Command manual_cmd(ManualInput input, float value, uint32_t client = kOperator, uint32_t seq = 1)
{
    Command c = cmd(CommandKind::Manual, client, seq);
    c.setpoint_input = input;
    c.setpoint = value;
    return c;
}

inline void add_number(ParamUpdate &u, const char *key, double value)
{
    ParamKeyValue &kv = u.items[u.count++];
    std::snprintf(kv.key, sizeof kv.key, "%s", key);
    kv.value.type = ParamValue::Type::Number;
    kv.value.number = value;
}

inline void add_bool(ParamUpdate &u, const char *key, bool value)
{
    ParamKeyValue &kv = u.items[u.count++];
    std::snprintf(kv.key, sizeof kv.key, "%s", key);
    kv.value.type = ParamValue::Type::Bool;
    kv.value.flag = value;
}

inline Command set_params_cmd(const ParamUpdate *update, uint32_t client = kOperator, uint32_t seq = 1)
{
    Command c = cmd(CommandKind::SetParams, client, seq);
    c.params = update;
    return c;
}

// A supervisor plus a healthy baseline of inputs. Each tick() advances the
// tick counter by one and runs one step with the given commands.
struct Rig {
    Supervisor sup;
    ScriptedController ctl;
    TickInputs in;
    TickOutputs out;
    uint32_t now = 1000;  // nonzero start, so tick arithmetic is exercised

    explicit Rig(const Params &p = Params{}) : sup(p)
    {
        healthy_inputs();
        in.built_inputs = manual_input_bit(ManualInput::Throttle) | manual_input_bit(ManualInput::Wheel) |
                          manual_input_bit(ManualInput::Rate);
        ctl.throttle_value = 300;
    }

    // The healthy baseline: still, level, charged, sensors reading, wheel stopped.
    void healthy_inputs()
    {
        in.rate_dps = 0.0f;
        in.wheel_rpm_safe = 0.0f;
        in.wheel_rpm_fast = 0.0f;
        in.hall_edge_age_ms = 10;
        in.vbat_avg_v = 10.4f;
        in.tilt_deg = 0.0f;
        in.imu_invalid_streak = 0;
        in.missed_tick_streak = 0;
    }

    const TickOutputs &tick(std::vector<Command> cmds = {}, bool estop = false)
    {
        ++now;
        in.tick = now;
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        in.estop_requested = estop;
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        return out;
    }

    // Steps `n` quiet ticks; a ping from the operator every 100 ms keeps an
    // armed box alive unless keepalive is false.
    void run(uint32_t n, bool keepalive = true)
    {
        for (uint32_t i = 0; i < n; ++i) {
            if (keepalive && i % 50 == 0) {
                tick({cmd(CommandKind::Ping)});
            } else {
                tick();
            }
        }
    }

    // SAFE to READY with the Hall installed: arm, then hold the wheel at bias.
    void arm_to_ready()
    {
        tick({cmd(CommandKind::Arm)});
        in.wheel_rpm_safe = sup.params().omega_bias_rpm;
        run(ticks_from_ms(kSpinupDwellMs) + 2);
    }

    void enter_manual(ManualInput input)
    {
        arm_to_ready();
        tick({mode_cmd(Mode::Manual, input)});
    }

    bool has_event_cause(const char *cause) const
    {
        for (std::size_t i = 0; i < out.events.count; ++i) {
            if (out.events.items[i].kind == Event::Kind::Mode && std::strcmp(out.events.items[i].cause, cause) == 0) {
                return true;
            }
        }
        return false;
    }

    std::string last_reason() const
    {
        return out.acks.count == 0 ? std::string("<no ack>") : std::string(out.acks.items[out.acks.count - 1].reason);
    }
    bool last_ok() const { return out.acks.count != 0 && out.acks.items[out.acks.count - 1].ok; }
};

// In-memory KvStore for persistence tests.
struct MemoryStore : KvStore {
    bool has_version = false;
    uint32_t version = 0;
    std::vector<uint8_t> blob;
    bool has_blob = false;
    bool fail_writes = false;

    bool read_u32(const char *key, uint32_t &value) override
    {
        if (std::strcmp(key, "param_version") != 0 || !has_version) return false;
        value = version;
        return true;
    }
    bool read_blob(const char *key, void *data, std::size_t &len) override
    {
        if (std::strcmp(key, "params") != 0 || !has_blob || blob.size() > len) return false;
        std::memcpy(data, blob.data(), blob.size());
        len = blob.size();
        return true;
    }
    bool write_u32(const char *key, uint32_t value) override
    {
        if (fail_writes || std::strcmp(key, "param_version") != 0) return false;
        has_version = true;
        version = value;
        return true;
    }
    bool write_blob(const char *key, const void *data, std::size_t len) override
    {
        if (fail_writes || std::strcmp(key, "params") != 0) return false;
        has_blob = true;
        blob.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + len);
        return true;
    }
    bool commit() override { return !fail_writes; }
};

}  // namespace harness
