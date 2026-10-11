#pragma once

// The WebSocket server (esp_http_server, port 80, /ws) and its client table.
// Everything touching sockets or the client table runs in the httpd task; the
// comms task only hands frames over through the functions below.

#include <cstddef>
#include <cstdint>

#include "direcsun/comms/comms.hpp"

namespace direcsun::comms {

// Client id meaning "every client that has spoken" (events).
constexpr uint32_t kTalkers = 0;

bool ws_server_start(Backend &backend);

// Box message counter for tlm, events, and pongs (acks echo the command seq).
uint32_t ws_next_box_seq();

// Comms task side. ws_enqueue copies an ack or event for the httpd task to send
// (client = the ack's client, or kTalkers); false if the hand-over queue is full.
bool ws_enqueue(uint32_t client, const char *text, std::size_t len);
// Replaces the latest tlm frame; every client gets the newest one at the next flush.
void ws_publish_tlm(const char *text, std::size_t len);
// Asks the httpd task to send what is waiting. Cheap to call every cycle.
void ws_request_flush();
// tlm frames skipped because a client's socket was not ready (tlm_dropped).
uint32_t ws_tlm_dropped();
// Frames the comms task could not hand over (queue full or encode failure).
uint32_t ws_handover_dropped();
void ws_count_handover_drop();

// set_params payloads stay alive in comms until their ack (R3.6). When the ack
// for (client, seq) passes through the comms task, this releases the slot and
// copies the applied params object (for ack.applied) into `applied`.
bool ws_release_params(uint32_t client, uint32_t seq, char *applied, std::size_t cap);

}  // namespace direcsun::comms
