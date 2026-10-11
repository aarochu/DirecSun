// R3.2 bring-up: the three sensors on one bus (changes/3-teleop/R3.2-sensor-bus.md).
//
// Boot prints the probe results, then a sampling task on core 1 reads the IMU
// every 2 ms and polls the magnetometer every 2nd tick, while a low-priority
// task reads the barometer at 1 Hz (baro_read() can block on bus errors). A
// printer task prints a live line at 5 Hz and a summary every second, so slow
// console output never delays sampling.

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>

#include "direcsun/drivers/baro.hpp"
#include "direcsun/drivers/i2c_bus.hpp"
#include "direcsun/drivers/imu.hpp"
#include "direcsun/drivers/mag.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace drv = direcsun::drivers;

namespace {

constexpr const char *TAG = "sensors";
constexpr TickType_t kTickPeriod = pdMS_TO_TICKS(2);  // 500 Hz with CONFIG_FREERTOS_HZ=1000
constexpr int kTicksPerSecond = 500;

struct Snapshot {
    drv::ImuSample imu;
    drv::MagSample mag;
    float baro_t_c;
    float baro_p_pa;
    bool baro_ok;
    // Per-second summary, written once a second by the sampling task.
    uint32_t sec;
    uint32_t imu_reads_last_s;
    uint32_t imu_invalid_last_s;
    uint32_t burst_min_us;
    uint32_t burst_med_us;
    uint32_t burst_max_us;
    uint32_t mag_samples_last_s;
    drv::ImuStats imu_stats;
    drv::MagStats mag_stats;
};

portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
Snapshot s_snap = {};
bool s_imu_ok = false;
bool s_mag_ok = false;

void sampling_task(void *)
{
    std::array<uint32_t, kTicksPerSecond + 16> bursts{};
    size_t n_bursts = 0;
    uint32_t reads_s = 0;
    uint32_t invalid_s = 0;
    uint32_t mag_s = 0;
    uint32_t tick = 0;
    uint32_t sec = 0;
    drv::ImuSample imu = {};
    drv::MagSample mag = {};
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, kTickPeriod);
        if (s_imu_ok) {
            const bool valid = drv::imu_read(imu);
            ++reads_s;
            invalid_s += valid ? 0 : 1;
            if (n_bursts < bursts.size()) {
                bursts[n_bursts++] = imu.burst_us;
            }
        }
        const bool new_mag = s_mag_ok && (tick % 2 == 0) && drv::mag_poll(mag);
        mag_s += new_mag ? 1 : 0;

        taskENTER_CRITICAL(&s_lock);
        s_snap.imu = imu;
        if (new_mag) {
            s_snap.mag = mag;
        }
        taskEXIT_CRITICAL(&s_lock);

        if (++tick % kTicksPerSecond == 0) {
            uint32_t min = 0;
            uint32_t med = 0;
            uint32_t max = 0;
            if (n_bursts > 0) {
                auto first = bursts.begin();
                auto last = bursts.begin() + static_cast<std::ptrdiff_t>(n_bursts);
                min = *std::min_element(first, last);
                max = *std::max_element(first, last);
                auto mid = first + static_cast<std::ptrdiff_t>(n_bursts / 2);
                std::nth_element(first, mid, last);
                med = *mid;
            }
            taskENTER_CRITICAL(&s_lock);
            s_snap.sec = ++sec;
            s_snap.imu_reads_last_s = reads_s;
            s_snap.imu_invalid_last_s = invalid_s;
            s_snap.burst_min_us = min;
            s_snap.burst_med_us = med;
            s_snap.burst_max_us = max;
            s_snap.mag_samples_last_s = mag_s;
            s_snap.imu_stats = drv::imu_stats();
            s_snap.mag_stats = drv::mag_stats();
            taskEXIT_CRITICAL(&s_lock);
            n_bursts = 0;
            reads_s = 0;
            invalid_s = 0;
            mag_s = 0;
        }
    }
}

void baro_task(void *)
{
    for (;;) {
        float t = 0.0f;
        float p = 0.0f;
        const bool ok = drv::baro_read(t, p) == ESP_OK;
        taskENTER_CRITICAL(&s_lock);
        s_snap.baro_ok = ok;
        if (ok) {
            s_snap.baro_t_c = t;
            s_snap.baro_p_pa = p;
        }
        taskEXIT_CRITICAL(&s_lock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void printer_task(void *)
{
    uint32_t printed_sec = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        Snapshot s;
        taskENTER_CRITICAL(&s_lock);
        s = s_snap;
        taskEXIT_CRITICAL(&s_lock);

        ESP_LOGI(TAG, "gyro dps x %+8.2f y %+8.2f z %+8.2f | accel g %+6.3f %+6.3f %+6.3f | mag raw %+6d %+6d %+6d%s | baro %s %.2f C %.0f Pa",
                 drv::imu_gyro_dps(s.imu.gyro[0]), drv::imu_gyro_dps(s.imu.gyro[1]), drv::imu_gyro_dps(s.imu.gyro[2]),
                 drv::imu_accel_g(s.imu.accel[0]), drv::imu_accel_g(s.imu.accel[1]), drv::imu_accel_g(s.imu.accel[2]),
                 s.mag.raw[0], s.mag.raw[1], s.mag.raw[2], s.mag.overflow ? " OVFL" : "", s.baro_ok ? "ok" : "--",
                 s.baro_t_c, s.baro_p_pa);

        if (s.sec != printed_sec) {
            printed_sec = s.sec;
            ESP_LOGI(TAG,
                     "summary t=%" PRIu32 "s | imu reads %" PRIu32 "/s invalid %" PRIu32 " burst us min %" PRIu32
                     " med %" PRIu32 " max %" PRIu32 " | imu total errors %" PRIu32 " slow %" PRIu32
                     " bus_resets %" PRIu32 " | mag %" PRIu32 "/s errors %" PRIu32 " ovfl %" PRIu32,
                     s.sec, s.imu_reads_last_s, s.imu_invalid_last_s, s.burst_min_us, s.burst_med_us, s.burst_max_us,
                     s.imu_stats.errors, s.imu_stats.slow, s.imu_stats.bus_resets, s.mag_samples_last_s,
                     s.mag_stats.errors, s.mag_stats.overflows);
        }
    }
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "R3.2 sensor bring-up");
    ESP_ERROR_CHECK(drv::i2c_bus_init());

    const esp_err_t imu = drv::imu_init();
    s_imu_ok = imu == ESP_OK;
    ESP_LOGI(TAG, "probe: MPU-6050 WHO_AM_I 0x%02X (expect 0x%02X): %s", drv::imu_who_am_i(), drv::kImuWhoAmI,
             s_imu_ok ? "OK" : "IMU_MISSING");

    const esp_err_t mag = drv::mag_init();
    s_mag_ok = mag == ESP_OK;
    ESP_LOGI(TAG, "probe: QMC5883P chip ID 0x%02X (expect 0x%02X): %s", drv::mag_chip_id(), drv::kMagChipId,
             s_mag_ok ? "OK" : "MAG_MISSING");

    const esp_err_t baro = drv::baro_init();
    ESP_LOGI(TAG, "probe: BMP280 chip ID 0x%02X at 0x%02X (expect 0x%02X): %s", drv::baro_chip_id(),
             drv::kBaroAddress, drv::kBaroChipId, baro == ESP_OK ? "OK" : "BARO_MISSING");

    xTaskCreatePinnedToCore(sampling_task, "sampling", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
    xTaskCreatePinnedToCore(printer_task, "printer", 4096, nullptr, 2, nullptr, 0);
    if (drv::baro_present()) {
        xTaskCreatePinnedToCore(baro_task, "baro", 4096, nullptr, 1, nullptr, 0);
    }
}
