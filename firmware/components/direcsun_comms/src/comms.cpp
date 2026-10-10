#include "direcsun/comms/comms.hpp"

#include "direcsun/core/protocol.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.hpp"
#include "ws_server.hpp"

namespace direcsun::comms {

namespace {

constexpr const char *TAG = "comms";
constexpr TickType_t kPeriod = pdMS_TO_TICKS(20);  // 50 Hz tlm (FD section 15.3)
constexpr UBaseType_t kPriority = 4;               // below the httpd task (5), so inbound wins
constexpr uint32_t kStack = 6144;

Backend *s_backend = nullptr;

// Comms task: acks and events from control, then the newest telemetry, then one
// flush request to the httpd task. Encoding happens here so the httpd task only
// moves bytes.
void comms_task(void *)
{
    static TelemetrySnapshot snap;
    static char text[protocol::kMaxAckBytes];
    static char tlm[protocol::kMaxTlmBytes];
    static char applied[protocol::kMaxInboundBytes + 1];
    uint32_t last_snapshot = 0;
    uint32_t reported_drops = 0;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, kPeriod);

        Outbound item;
        while (s_backend->pop_outbound(item)) {
            std::size_t len = 0;
            uint32_t target = kTalkers;
            if (item.kind == Outbound::Kind::Ack) {
                const bool had_params = ws_release_params(item.ack.client, item.ack.seq, applied, sizeof applied);
                len = protocol::encode_ack(item.ack, had_params && item.ack.ok ? applied : nullptr, text, sizeof text);
                target = item.ack.client;
            } else {
                len = protocol::encode_event(item.event, ws_next_box_seq(), esp_timer_get_time() / 1000, text,
                                             sizeof text);
            }
            if (len == 0 || !ws_enqueue(target, text, len)) ws_count_handover_drop();
        }

        const uint32_t sequence = s_backend->snapshot_sequence();
        if (sequence != last_snapshot && s_backend->read_snapshot(snap)) {
            last_snapshot = sequence;
            const std::size_t len = protocol::encode_tlm(snap, ws_next_box_seq(), ws_tlm_dropped(), tlm, sizeof tlm);
            if (len > 0) ws_publish_tlm(tlm, len);
            else ws_count_handover_drop();
        }
        ws_request_flush();

        const uint32_t drops = ws_handover_dropped();
        if (drops != reported_drops) {
            ESP_LOGW(TAG, "%lu frame(s) could not be handed to the WebSocket task", static_cast<unsigned long>(drops));
            reported_drops = drops;
        }
    }
}

}  // namespace

void start(Backend &backend)
{
    s_backend = &backend;
    if (!wifi_start()) {
        ESP_LOGW(TAG, "network not started: no ground link this boot (the box stays in SAFE)");
        return;
    }
    if (!ws_server_start(backend)) return;
    if (xTaskCreatePinnedToCore(comms_task, "comms", kStack, nullptr, kPriority, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "comms task failed to start");
    }
}

}  // namespace direcsun::comms
