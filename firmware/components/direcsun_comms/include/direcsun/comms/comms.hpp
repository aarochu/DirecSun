#pragma once

// The ground link (FD section 15): Wi-Fi, mDNS, and the WebSocket server on
// port 80 at /ws that speaks schema v1 to the main UI and the bench tools.
//
// Threads, all on core 0 (control owns core 1 and never waits on any of this):
//   httpd task   WebSocket handshakes, inbound frames (parse, then push), pongs
//                and parse refusals sent inline, and every outbound send.
//   comms task   50 Hz: drains acks and events from control, encodes the latest
//                telemetry snapshot, and hands both to the httpd task to send.
//
// Delivery rules (R3.5): tlm goes to every client; an ack goes to the client
// that sent the command; events go only to clients that have sent at least one
// message, so a read-only viewer (the main UI's device link) receives only tlm.

#include <cstdint>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/snapshot.hpp"

namespace direcsun::comms {

// How comms reaches the control task. main implements it over its queues and
// seqlocks (main/shared.hpp); every call is non-blocking.
class Backend {
public:
    virtual ~Backend() = default;
    // A parsed command, including every ping (FD finding V-32). False when the
    // command queue is full: comms answers ack ok:false reason "busy".
    virtual bool push_command(const Command &cmd) = 0;
    // Any estop from any client (FD section 14.4). Takes effect even when the
    // command queue is full.
    virtual void request_estop() = 0;
    // Next ack or event from control, if any.
    virtual bool pop_outbound(Outbound &item) = 0;
    // Latest telemetry snapshot and its sequence (changes at 50 Hz).
    virtual bool read_snapshot(TelemetrySnapshot &snap) = 0;
    virtual uint32_t snapshot_sequence() = 0;
};

// Starts Wi-Fi (station, or SoftAP from menuconfig), mDNS, the WebSocket
// server, and the comms task. Returns at once: the box is safe without a
// network, and control never waits for one. Failures are logged.
void start(Backend &backend);

}  // namespace direcsun::comms
