#pragma once

// MPU-6050 on the shared bus (FD section 10.1). Configuration goes through the
// esp-idf-lib driver at boot; the per-tick read is one 14-byte burst straight
// on the device handle with a short timeout, because i2cdev's own read path
// retries with backoff for up to seconds, which a 2 ms control tick cannot afford.

#include <cstdint>

#include "esp_err.h"

namespace direcsun::drivers {

constexpr uint8_t kImuAddress = 0x68;       // AD0 low, the Adafruit #3886 default
constexpr uint8_t kImuWhoAmI = 0x68;        // WHO_AM_I (0x75) of a genuine MPU-6050
constexpr float kGyroLsbPerDps = 16.4f;     // FS_SEL 3, +-2000 deg/s (FD finding V-9)
constexpr float kAccelLsbPerG = 8192.0f;    // AFS_SEL 1, +-4 g

struct ImuConfig {
    // A burst slower than this is invalid (FD section 10.1). 0.39 ms is
    // expected at 400 kHz; at the 100 kHz fallback raise it (FD section 9.2).
    uint32_t max_burst_us = 1000;
    // Upper bound on how long one transaction may block, in ms.
    int xfer_timeout_ms = 2;
    // Consecutive bus errors before one bus recovery (FD section 10.1).
    uint32_t errors_before_reset = 3;
};

struct ImuSample {
    int16_t accel[3];   // x, y, z, raw counts (kAccelLsbPerG per g)
    int16_t temp_raw;   // deg C = temp_raw / 340 + 36.53
    int16_t gyro[3];    // x, y, z, raw counts (kGyroLsbPerDps per deg/s)
    int64_t t_us;       // esp_timer time at the start of the burst
    uint32_t burst_us;  // duration of the transaction
    bool valid;         // false on a bus error or a burst slower than max_burst_us
};

struct ImuStats {
    uint32_t reads;               // bursts attempted
    uint32_t errors;              // bursts that returned a bus error
    uint32_t slow;                // bursts that succeeded but took longer than max_burst_us
    uint32_t consecutive_errors;  // current run of bus errors
    uint32_t bus_resets;          // recoveries run (at most one per run of errors)
};

// Probes WHO_AM_I, configures the sensor, and reads the configuration back.
// Returns ESP_ERR_NOT_FOUND when nothing answers (IMU_MISSING) and
// ESP_ERR_INVALID_RESPONSE for a wrong WHO_AM_I or a failed read-back.
// Requires i2c_bus_init().
esp_err_t imu_init(const ImuConfig &config = ImuConfig{});

// WHO_AM_I as read by imu_init() (0 if the read failed).
uint8_t imu_who_am_i();

// One 14-byte burst from ACCEL_XOUT_H (0x3B). Returns out.valid. After
// errors_before_reset consecutive bus errors, runs one bus recovery; the next
// successful read re-arms it. Call from a single task.
bool imu_read(ImuSample &out);

const ImuStats &imu_stats();

constexpr float imu_gyro_dps(int16_t raw)
{
    return static_cast<float>(raw) / kGyroLsbPerDps;
}

constexpr float imu_accel_g(int16_t raw)
{
    return static_cast<float>(raw) / kAccelLsbPerG;
}

constexpr float imu_temp_c(int16_t raw)
{
    return static_cast<float>(raw) / 340.0f + 36.53f;
}

}  // namespace direcsun::drivers
