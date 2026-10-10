#include "shared.hpp"

#include <atomic>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace direcsun::app {

namespace {

constexpr const char *TAG = "shared";

// Static storage: no heap, and the queues exist before any task starts.
StaticQueue_t s_command_queue_buf;
uint8_t s_command_storage[kCommandQueueDepth * sizeof(Command)];
QueueHandle_t s_commands = nullptr;

StaticQueue_t s_outbound_queue_buf;
uint8_t s_outbound_storage[kOutboundQueueDepth * sizeof(Outbound)];
QueueHandle_t s_outbound = nullptr;

std::atomic<bool> s_estop{false};
std::atomic<uint32_t> s_dropped_outbound{0};

SeqlockBuffer<TelemetrySnapshot> s_telemetry;
SeqlockBuffer<TickStats> s_tick_stats;
SeqlockBuffer<BaroReading> s_baro;
SeqlockBuffer<Params> s_params_to_persist;

}  // namespace

void shared_init()
{
    if (s_commands != nullptr) {
        return;
    }
    s_commands = xQueueCreateStatic(kCommandQueueDepth, sizeof(Command), s_command_storage, &s_command_queue_buf);
    s_outbound = xQueueCreateStatic(kOutboundQueueDepth, sizeof(Outbound), s_outbound_storage, &s_outbound_queue_buf);
    configASSERT(s_commands != nullptr && s_outbound != nullptr);
    ESP_LOGI(TAG, "command queue %u x %u B, outbound queue %u x %u B", static_cast<unsigned>(kCommandQueueDepth),
             static_cast<unsigned>(sizeof(Command)), static_cast<unsigned>(kOutboundQueueDepth),
             static_cast<unsigned>(sizeof(Outbound)));
}

bool shared_push_command(const Command &cmd)
{
    return xQueueSend(s_commands, &cmd, 0) == pdTRUE;
}

void shared_request_estop()
{
    s_estop.store(true, std::memory_order_release);
}

bool shared_pop_outbound(Outbound &item)
{
    return xQueueReceive(s_outbound, &item, 0) == pdTRUE;
}

bool shared_take_estop()
{
    return s_estop.exchange(false, std::memory_order_acq_rel);
}

std::size_t shared_drain_commands(Command *out, std::size_t capacity)
{
    std::size_t n = 0;
    while (n < capacity && xQueueReceive(s_commands, &out[n], 0) == pdTRUE) {
        ++n;
    }
    return n;
}

bool shared_post_outbound(const Outbound &item)
{
    if (xQueueSend(s_outbound, &item, 0) == pdTRUE) {
        return true;
    }
    s_dropped_outbound.fetch_add(1, std::memory_order_relaxed);
    return false;
}

uint32_t shared_dropped_outbound()
{
    return s_dropped_outbound.load(std::memory_order_relaxed);
}

SeqlockBuffer<TelemetrySnapshot> &telemetry()
{
    return s_telemetry;
}

SeqlockBuffer<TickStats> &tick_stats()
{
    return s_tick_stats;
}

SeqlockBuffer<BaroReading> &baro_reading()
{
    return s_baro;
}

SeqlockBuffer<Params> &params_to_persist()
{
    return s_params_to_persist;
}

namespace {

// The channels above, as the comms component sees them. Every call is
// non-blocking, as the Backend contract requires.
class SharedBackend final : public comms::Backend {
public:
    bool push_command(const Command &cmd) override { return shared_push_command(cmd); }
    void request_estop() override { shared_request_estop(); }
    bool pop_outbound(Outbound &item) override { return shared_pop_outbound(item); }
    bool read_snapshot(TelemetrySnapshot &snap) override { return telemetry().read(snap); }
    uint32_t snapshot_sequence() override { return telemetry().sequence(); }
};

}  // namespace

comms::Backend &shared_comms_backend()
{
    static SharedBackend backend;
    return backend;
}

}  // namespace direcsun::app
