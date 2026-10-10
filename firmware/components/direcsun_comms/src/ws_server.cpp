#include "ws_server.hpp"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "direcsun/core/protocol.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace direcsun::comms {

namespace {

constexpr const char *TAG = "ws";

constexpr int kMaxClients = 3;                         // FD section 15.2
constexpr std::size_t kRecvMax = 1024;                 // frames up to this are read and refused if over 512;
                                                       // anything larger closes the session
constexpr std::size_t kTxText = protocol::kMaxAckBytes;
constexpr UBaseType_t kTxDepth = 16;
constexpr int kParamSlots = 2;
constexpr int64_t kParamSlotTimeoutUs = 2000000;       // a slot whose ack never came back is reclaimed

// ---- httpd task only -----------------------------------------------------------
struct Client {
    int fd = -1;
    uint32_t id = 0;
    bool talker = false;  // has sent at least one message: receives events
};
Client s_clients[kMaxClients];
uint32_t s_next_client_id = 1;  // never reused, so a reconnect is a new client and resumes nothing

httpd_handle_t s_server = nullptr;
Backend *s_backend = nullptr;
uint8_t s_rx[kRecvMax + 1];
protocol::Inbound s_inbound;
char s_reply[protocol::kMaxAckBytes];

struct TxFrame {
    uint32_t client = kTalkers;
    uint16_t len = 0;
    char text[kTxText];
};
TxFrame s_tx_drain;
char s_tlm_copy[protocol::kMaxTlmBytes];
uint32_t s_tlm_sent_version = 0;

// ---- shared between the comms task and the httpd task ----------------------------
StaticQueue_t s_tx_queue_buf;
uint8_t s_tx_queue_storage[kTxDepth * sizeof(TxFrame)];
QueueHandle_t s_tx_queue = nullptr;

StaticSemaphore_t s_tlm_lock_buf;
SemaphoreHandle_t s_tlm_lock = nullptr;
char s_tlm[protocol::kMaxTlmBytes];
std::size_t s_tlm_len = 0;
uint32_t s_tlm_version = 0;

std::atomic<bool> s_flush_pending{false};
std::atomic<uint32_t> s_box_seq{0};
std::atomic<uint32_t> s_tlm_dropped{0};
std::atomic<uint32_t> s_handover_dropped{0};

// set_params storage. State: 0 free, 1 being written or read, 2 waiting for its ack.
struct ParamSlot {
    std::atomic<uint8_t> state{0};
    uint32_t client = 0;
    uint32_t seq = 0;
    int64_t since_us = 0;
    ParamUpdate params;
    char applied[protocol::kMaxInboundBytes + 1] = {};
};
ParamSlot s_param_slots[kParamSlots];

// ---- client table ----------------------------------------------------------------

Client *client_by_fd(int fd)
{
    for (Client &c : s_clients) {
        if (c.fd == fd) return &c;
    }
    return nullptr;
}

Client *add_client(int fd)
{
    if (Client *existing = client_by_fd(fd)) return existing;
    for (Client &c : s_clients) {
        if (c.fd < 0) {
            c.fd = fd;
            c.id = s_next_client_id++;
            c.talker = false;
            ESP_LOGI(TAG, "client %lu connected (fd %d)", static_cast<unsigned long>(c.id), fd);
            return &c;
        }
    }
    return nullptr;
}

void on_close(httpd_handle_t, int fd)
{
    if (Client *c = client_by_fd(fd)) {
        // A controlling client's heartbeat simply expires (FD 14.4): no instant
        // disarm, and its id is never reused, so reconnecting resumes nothing.
        ESP_LOGI(TAG, "client %lu disconnected", static_cast<unsigned long>(c->id));
        *c = Client{};
    }
    close(fd);
}

// ---- sending (httpd task) -----------------------------------------------------------

void send_to(const Client &c, const char *text, std::size_t len)
{
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(text));
    frame.len = len;
    const esp_err_t err = httpd_ws_send_frame_async(s_server, c.fd, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send to client %lu failed (%s): closing it", static_cast<unsigned long>(c.id),
                 esp_err_to_name(err));
        httpd_sess_trigger_close(s_server, c.fd);
    }
}

bool writable(int fd)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval zero = {0, 0};
    return select(fd + 1, nullptr, &set, nullptr, &zero) > 0;
}

void reply(httpd_req_t *req, std::size_t len)
{
    if (len == 0) return;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(s_reply);
    frame.len = len;
    httpd_ws_send_frame(req, &frame);
}

void reply_ack(httpd_req_t *req, uint32_t client, uint32_t seq, bool ok, const char *reason)
{
    Ack ack;
    ack.client = client;
    ack.seq = seq;
    ack.ok = ok;
    if (!ok) std::snprintf(ack.reason, sizeof ack.reason, "%s", reason);
    reply(req, protocol::encode_ack(ack, nullptr, s_reply, sizeof s_reply));
}

// Every queued ack and event, then the newest tlm frame to every ready client.
void flush_work(void *)
{
    s_flush_pending.store(false);
    while (xQueueReceive(s_tx_queue, &s_tx_drain, 0) == pdTRUE) {
        for (const Client &c : s_clients) {
            if (c.fd < 0) continue;
            const bool wanted = s_tx_drain.client == kTalkers ? c.talker : c.id == s_tx_drain.client;
            if (wanted) send_to(c, s_tx_drain.text, s_tx_drain.len);
        }
    }

    std::size_t len = 0;
    if (xSemaphoreTake(s_tlm_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_tlm_version != s_tlm_sent_version && s_tlm_len > 0) {
            std::memcpy(s_tlm_copy, s_tlm, s_tlm_len);
            len = s_tlm_len;
            s_tlm_sent_version = s_tlm_version;
        }
        xSemaphoreGive(s_tlm_lock);
    }
    if (len == 0) return;
    for (const Client &c : s_clients) {
        if (c.fd < 0) continue;
        // A client whose socket cannot take a frame now skips this one rather
        // than stall the httpd task, which also carries every inbound e-stop.
        if (writable(c.fd)) send_to(c, s_tlm_copy, len);
        else s_tlm_dropped.fetch_add(1);
    }
}

// ---- set_params slots ------------------------------------------------------------------

ParamSlot *claim_param_slot()
{
    const int64_t now = esp_timer_get_time();
    for (ParamSlot &slot : s_param_slots) {
        uint8_t expected = 0;
        if (slot.state.compare_exchange_strong(expected, 1)) return &slot;
    }
    for (ParamSlot &slot : s_param_slots) {
        uint8_t expected = 2;
        if (now - slot.since_us > kParamSlotTimeoutUs && slot.state.compare_exchange_strong(expected, 1)) {
            ESP_LOGW(TAG, "reclaiming a set_params slot whose ack never came back");
            return &slot;
        }
    }
    return nullptr;
}

// ---- inbound (httpd task) -----------------------------------------------------------------

esp_err_t ws_handler(httpd_req_t *req)
{
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        return add_client(fd) != nullptr ? ESP_OK : ESP_FAIL;
    }

    httpd_ws_frame_t frame = {};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.len > kRecvMax) {
        ESP_LOGW(TAG, "closing a client that sent a %u byte frame", static_cast<unsigned>(frame.len));
        return ESP_FAIL;
    }
    frame.payload = s_rx;
    if (frame.len > 0) {
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err != ESP_OK) return err;
    }

    Client *client = add_client(fd);
    if (client == nullptr) return ESP_FAIL;
    client->talker = true;
    const uint32_t id = client->id;

    if (frame.type != HTTPD_WS_TYPE_TEXT) {
        reply_ack(req, id, 0, false, "malformed JSON");
        return ESP_OK;
    }

    protocol::parse_inbound(reinterpret_cast<const char *>(s_rx), frame.len, id, s_inbound);
    switch (s_inbound.kind) {
    case protocol::InboundKind::Rejected:
        reply_ack(req, id, s_inbound.reject_seq, false, s_inbound.reason);
        break;

    case protocol::InboundKind::Ping:
        // The supervisor owns the heartbeat (FD V-32); comms answers the pong.
        s_backend->push_command(s_inbound.command);
        reply(req, protocol::encode_pong(s_box_seq.fetch_add(1) + 1, s_inbound.command.seq, s_inbound.t_client_ms,
                                         esp_timer_get_time() / 1000, s_reply, sizeof s_reply));
        break;

    case protocol::InboundKind::Command: {
        Command &cmd = s_inbound.command;
        if (cmd.kind == CommandKind::Estop) {
            s_backend->request_estop();
            // The flag stops the wheel even when the queue is full; the queued
            // copy only produces the ack, so a full queue is acked here.
            if (!s_backend->push_command(cmd)) reply_ack(req, id, cmd.seq, true, "");
            break;
        }
        ParamSlot *slot = nullptr;
        if (cmd.kind == CommandKind::SetParams) {
            slot = claim_param_slot();
            if (slot == nullptr) {
                reply_ack(req, id, cmd.seq, false, "busy");
                break;
            }
            slot->client = id;
            slot->seq = cmd.seq;
            slot->since_us = esp_timer_get_time();
            slot->params = s_inbound.params;
            std::memcpy(slot->applied, s_inbound.params_json, sizeof slot->applied);
            cmd.params = &slot->params;
            slot->state.store(2);
        }
        if (!s_backend->push_command(cmd)) {
            if (slot != nullptr) slot->state.store(0);
            reply_ack(req, id, cmd.seq, false, "busy");
        }
        break;
    }
    }
    return ESP_OK;
}

}  // namespace

bool ws_server_start(Backend &backend)
{
    s_backend = &backend;
    s_tx_queue = xQueueCreateStatic(kTxDepth, sizeof(TxFrame), s_tx_queue_storage, &s_tx_queue_buf);
    s_tlm_lock = xSemaphoreCreateMutexStatic(&s_tlm_lock_buf);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = kMaxClients;
    config.lru_purge_enable = false;  // never evict a connected (possibly controlling) client
    config.core_id = 0;               // core 1 belongs to the control task
    config.stack_size = 8192;
    config.send_wait_timeout = 1;     // seconds; a client that cannot take an ack in 1 s is dropped
    config.close_fn = on_close;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket server failed to start: %s", esp_err_to_name(err));
        return false;
    }
    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = nullptr,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    err = httpd_register_uri_handler(s_server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "registering /ws failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "WebSocket server on port 80 at /ws (up to %d clients)", kMaxClients);
    return true;
}

uint32_t ws_next_box_seq()
{
    return s_box_seq.fetch_add(1) + 1;
}

bool ws_enqueue(uint32_t client, const char *text, std::size_t len)
{
    if (s_tx_queue == nullptr || len == 0 || len >= kTxText) return false;
    static TxFrame frame;  // comms task only
    frame.client = client;
    frame.len = static_cast<uint16_t>(len);
    std::memcpy(frame.text, text, len);
    frame.text[len] = '\0';
    return xQueueSend(s_tx_queue, &frame, 0) == pdTRUE;
}

void ws_publish_tlm(const char *text, std::size_t len)
{
    if (s_tlm_lock == nullptr || len == 0 || len > sizeof s_tlm) return;
    if (xSemaphoreTake(s_tlm_lock, pdMS_TO_TICKS(5)) != pdTRUE) {
        ws_count_handover_drop();
        return;
    }
    std::memcpy(s_tlm, text, len);
    s_tlm_len = len;
    ++s_tlm_version;
    xSemaphoreGive(s_tlm_lock);
}

void ws_request_flush()
{
    if (s_server == nullptr) return;
    if (!s_flush_pending.exchange(true)) {
        if (httpd_queue_work(s_server, flush_work, nullptr) != ESP_OK) s_flush_pending.store(false);
    }
}

uint32_t ws_tlm_dropped()
{
    return s_tlm_dropped.load();
}

uint32_t ws_handover_dropped()
{
    return s_handover_dropped.load();
}

void ws_count_handover_drop()
{
    s_handover_dropped.fetch_add(1);
}

bool ws_release_params(uint32_t client, uint32_t seq, char *applied, std::size_t cap)
{
    for (ParamSlot &slot : s_param_slots) {
        // Claim first, then compare: client and seq are only stable while we own it.
        uint8_t expected = 2;
        if (!slot.state.compare_exchange_strong(expected, 1)) continue;
        if (slot.client != client || slot.seq != seq) {
            slot.state.store(2);
            continue;
        }
        if (applied != nullptr && cap > 0) std::snprintf(applied, cap, "%s", slot.applied);
        slot.state.store(0);
        return true;
    }
    return false;
}

}  // namespace direcsun::comms
