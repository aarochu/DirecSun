#pragma once

// Channels between the control task (core 1) and the rest of the firmware
// (FD section 7.4). Control never waits on any of them: every queue operation
// on its side uses timeout 0, and the publish buffers are seqlocks.
//
// Interface for comms (R3.5), core 0:
//
//   shared_push_command(cmd)     A parsed, schema-valid command (including
//                                every `ping`, FD 7.4 / V-32). Returns false
//                                when the queue is full: answer
//                                ack ok:false reason:"busy". A SetParams command
//                                points at a ParamUpdate that comms must keep
//                                alive until the ack with that seq comes back.
//   shared_request_estop()       Any `estop` from any client (FD 14.4). Also
//                                push the command so it is acked; the flag
//                                makes the e-stop take effect even if the
//                                queue is full.
//   shared_pop_outbound(item)    Next ack (addressed to item.ack.client) or
//                                event (broadcast), non-blocking. Drain it
//                                every comms cycle.
//   telemetry().read(snapshot)   Latest TelemetrySnapshot, published at 50 Hz.
//
// Control side (control_task.cpp only): shared_take_estop(),
// shared_drain_commands(), shared_post_outbound(), and the writers of the
// seqlocks below.

#include <cstddef>
#include <cstdint>

#include "direcsun/comms/comms.hpp"
#include "direcsun/core/commands.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/core/snapshot.hpp"

namespace direcsun::app {

// Depths from FD section 7.4.
constexpr std::size_t kCommandQueueDepth = 16;
constexpr std::size_t kOutboundQueueDepth = 16;

// One item on the control-to-comms queue; defined in the core so the comms
// component can consume it without depending on main.
using direcsun::Outbound;

// BMP280 reading, published at 1 Hz by the barometer task (never by control).
struct BaroReading {
    bool valid = false;
    float t_c = 0.0f;
    float p_pa = 0.0f;
};

// Creates the queues. Call once from app_main before any task uses them.
void shared_init();

// ---- comms side --------------------------------------------------------
bool shared_push_command(const Command &cmd);
void shared_request_estop();
bool shared_pop_outbound(Outbound &item);

// ---- control side ------------------------------------------------------
// Reads and clears the e-stop flag.
bool shared_take_estop();
// Moves up to `capacity` waiting commands into `out`; returns how many.
std::size_t shared_drain_commands(Command *out, std::size_t capacity);
// Posts one ack or event; on a full queue it is dropped and counted.
bool shared_post_outbound(const Outbound &item);
// Acks and events dropped because the queue was full (tlm dropped_events).
uint32_t shared_dropped_outbound();

// The comms component's view of the channels above (R3.5).
comms::Backend &shared_comms_backend();

// ---- published state -----------------------------------------------------
SeqlockBuffer<TelemetrySnapshot> &telemetry();   // writer: control, 50 Hz
SeqlockBuffer<TickStats> &tick_stats();          // writer: control, 1 Hz
SeqlockBuffer<BaroReading> &baro_reading();      // writer: barometer task, 1 Hz
SeqlockBuffer<Params> &params_to_persist();      // writer: control, on params_changed

}  // namespace direcsun::app
