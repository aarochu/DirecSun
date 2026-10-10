#include "services.hpp"

#include <cinttypes>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/core/snapshot.hpp"
#include "direcsun/drivers/baro.hpp"
#include "esp_log.h"
#include "nvs_store.hpp"
#include "shared.hpp"

namespace drv = direcsun::drivers;

namespace direcsun::app {

namespace {

constexpr UBaseType_t kServicePriority = 2;  // above idle, far below control and Wi-Fi
constexpr BaseType_t kServiceCore = 0;

NvsKvStore *s_store = nullptr;

void baro_task(void *)
{
    for (;;) {
        BaroReading reading;
        reading.valid = drv::baro_read(reading.t_c, reading.p_pa) == ESP_OK;
        baro_reading().write(reading);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void persist_task(void *)
{
    uint32_t persisted_seq = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t seq = params_to_persist().sequence();
        if (seq == persisted_seq) {
            continue;
        }
        Params params;
        if (!params_to_persist().read(params)) {
            continue;
        }
        const bool ok = s_store != nullptr && save_params(*s_store, params);
        persisted_seq = seq;
        if (ok) {
            ESP_LOGI("persist", "parameters saved to NVS (version %" PRIu32 ")", kParamVersion);
        } else {
            ESP_LOGE("persist", "parameters NOT saved: NVS refused a write");
        }
    }
}

void status_task(void *)
{
    uint32_t last_second = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        TickStats stats;
        if (!tick_stats().read(stats) || stats.second == last_second) {
            continue;
        }
        last_second = stats.second;
        TelemetrySnapshot snap;
        const bool have_snap = telemetry().read(snap);
        // One line per second; field names are what the bench tests grep for.
        ESP_LOGI("status",
                 "s %" PRIu32 " ticks %" PRIu32 " missed %" PRIu32 " (total %" PRIu32 ") loop_us_max %" PRIu32
                 " dev_max_us %" PRIu32 " p99_dev_us %" PRIu32 " dshot sent %" PRIu32 " dropped %" PRIu32
                 " imu_invalid %" PRIu32,
                 stats.second, stats.ticks, stats.missed, stats.missed_total, stats.loop_us_max,
                 stats.period_max_dev_us, stats.percentile_dev_us(0.99f), stats.dshot_sent, stats.dshot_dropped,
                 stats.imu_invalid);
        if (have_snap) {
            ESP_LOGI("status",
                     "mode %s throttle %d vbat %.2f rate %.2f bias %.2f tilt %.1f psi %.1f wheel %.0f latched 0x%04x"
                     " flags 0x%02" PRIx32 " baro %s",
                     mode_wire_name(snap.mode), snap.throttle, static_cast<double>(snap.vbat_v),
                     static_cast<double>(snap.rate_dps), static_cast<double>(snap.gyro_bias_dps),
                     static_cast<double>(snap.tilt_deg), static_cast<double>(snap.psi_deg),
                     static_cast<double>(snap.wheel_rpm), static_cast<unsigned>(snap.latched), snap.flags,
                     snap.baro_valid ? "ok" : "--");
        }
    }
}

}  // namespace

void baro_task_start()
{
    xTaskCreatePinnedToCore(baro_task, "baro", 3072, nullptr, kServicePriority, nullptr, kServiceCore);
}

TaskHandle_t persist_task_start(NvsKvStore &store)
{
    s_store = &store;
    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(persist_task, "persist", 4096, nullptr, kServicePriority, &handle, kServiceCore);
    return handle;
}

void status_task_start()
{
    xTaskCreatePinnedToCore(status_task, "status", 4096, nullptr, kServicePriority - 1, nullptr, kServiceCore);
}

}  // namespace direcsun::app
