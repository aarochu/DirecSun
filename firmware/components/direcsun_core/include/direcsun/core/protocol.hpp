#pragma once

// Wire schema v1 (protocol/schema-v1.json, FD section 15.3): parsing what clients
// send and encoding what the box sends. Pure C++ plus the vendored cJSON, so every
// rule here is host-tested against the schema fixtures.
//
// Wire frame (CONTEXT.md, 2026-10-10): the firmware computes counter-clockwise
// positive (FD section 2.3); on the wire every quantity about the spin axis is
// clockwise positive, as the main UI (frontend/) draws it. This file is the only
// place that converts: the to_wire / from_wire functions below, used by the
// encoders and the parser. No other code knows the wire frame exists.

#include <cstddef>
#include <cstdint>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/snapshot.hpp"

namespace direcsun::protocol {

// Inbound messages larger than this are refused with "too large" (FD 15.2).
constexpr std::size_t kMaxInboundBytes = 512;

// Output buffer sizes. A tlm frame with every bench field is under 600 bytes; an
// ack can carry the applied set_params object, which is at most the inbound size.
constexpr std::size_t kMaxTlmBytes = 768;
constexpr std::size_t kMaxAckBytes = 160 + kMaxInboundBytes;
constexpr std::size_t kMaxEventBytes = 256;
constexpr std::size_t kMaxPongBytes = 160;

// ---- wire frame --------------------------------------------------------------

// Internal (CCW+) angle in degrees to the wire (CW+), rounded to 0.01 and wrapped
// onto (-180, 180] after rounding, so the printed value always satisfies the
// schema. Non-finite input gives 0.
float angle_to_wire(float internal_deg);
// Internal (CCW+) rate, torque, or any signed quantity about the spin axis to the
// wire (CW+). Non-finite input gives 0.
float signed_to_wire(float internal);
// Wire (CW+) angle or signed quantity to the internal frame (CCW+).
float angle_from_wire(float wire_deg);
float signed_from_wire(float wire);

// ---- inbound -----------------------------------------------------------------

enum class InboundKind : uint8_t {
    Command,   // a cmd; command.kind says which
    Ping,      // a ping; command.kind is CommandKind::Ping, forwarded for the heartbeat
    Rejected,  // answer with ack ok:false, reject_seq, reason
};

struct Inbound {
    InboundKind kind = InboundKind::Rejected;
    Command command;            // Command and Ping: tagged with the client and seq
    double t_client_ms = 0.0;   // Ping: echoed in the pong

    // set_params: the parsed keys. command.params is left null; the caller points
    // it at storage that outlives the command (ParamUpdate is too large to queue).
    ParamUpdate params;
    // set_params: the params object as received, compact, echoed as ack.applied
    // when the box accepts the set (all-or-nothing, so applied == received).
    char params_json[kMaxInboundBytes + 1] = {};

    uint32_t reject_seq = 0;    // Rejected: the message's seq, or 0 if it had none
    char reason[Ack::kMaxReason] = {};
};

// Parses one text frame from `client`. Never throws; every failure becomes
// Rejected with a reason (FD 15.3): "too large", "malformed JSON", "missing v",
// "unsupported v", "invalid seq", "unknown type", "unexpected type <t>",
// "unknown cmd", "missing field: <f>", "unexpected field: <f>",
// "invalid field: <f>", "two setpoints", "missing setpoint",
// "unknown param: <k>", "invalid param: <k>", "too many params".
void parse_inbound(const char *data, std::size_t len, uint32_t client, Inbound &out);

// ---- outbound ----------------------------------------------------------------
// Each encoder writes one JSON text frame into `buf` and returns its length, or
// 0 if `cap` was too small (the caller counts it and drops the frame).

std::size_t encode_tlm(const TelemetrySnapshot &snap, uint32_t seq, uint32_t tlm_dropped, char *buf,
                       std::size_t cap);

// `applied_json`: the accepted set_params object (Inbound::params_json), or null.
std::size_t encode_ack(const Ack &ack, const char *applied_json, char *buf, std::size_t cap);

std::size_t encode_event(const Event &event, uint32_t seq, int64_t t_ms, char *buf, std::size_t cap);

std::size_t encode_pong(uint32_t seq, uint32_t ping_seq, double t_client_ms, int64_t t_ms, char *buf,
                        std::size_t cap);

}  // namespace direcsun::protocol
