#include "direcsun/drivers/imu.hpp"

#include <cinttypes>

#include "direcsun/drivers/i2c_bus.hpp"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mpu6050.h"

namespace direcsun::drivers {

namespace {

constexpr const char *TAG = "imu";

// Registers (MPU-6000/6050 register map rev 4.0).
constexpr uint8_t kRegSmplrtDiv = 0x19;
constexpr uint8_t kRegConfig = 0x1A;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegWhoAmI = 0x75;
constexpr size_t kBurstBytes = 14;

mpu6050_dev_t s_dev = {};
ImuConfig s_config;
ImuStats s_stats = {};
uint8_t s_who_am_i = 0;
bool s_reset_armed = true;

int16_t be16(const uint8_t *p)
{
    return static_cast<int16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

esp_err_t read_reg(uint8_t reg, uint8_t &value)
{
    return i2c_dev_read_reg(&s_dev.i2c_dev, reg, &value, 1);
}

// Reads back every register imu_init() wrote and compares with what FD
// section 10.1 asks for, so a configuration that silently failed is caught at boot.
esp_err_t verify_config()
{
    struct Expect {
        uint8_t reg;
        uint8_t mask;
        uint8_t value;
        const char *what;
    };
    static constexpr Expect kExpect[] = {
        {kRegPwrMgmt1, 0xFF, 0x01, "PWR_MGMT_1 awake, PLL with X gyro"},
        {kRegSmplrtDiv, 0xFF, 0x00, "SMPLRT_DIV 0"},
        {kRegConfig, 0x07, 0x04, "DLPF_CFG 4"},
        {kRegGyroConfig, 0x18, 0x18, "FS_SEL 3 (+-2000 deg/s)"},
        {kRegAccelConfig, 0x18, 0x08, "AFS_SEL 1 (+-4 g)"},
    };
    for (const Expect &e : kExpect) {
        uint8_t value = 0;
        ESP_RETURN_ON_ERROR(read_reg(e.reg, value), TAG, "read-back of 0x%02X failed", e.reg);
        if ((value & e.mask) != e.value) {
            ESP_LOGE(TAG, "read-back 0x%02X = 0x%02X, expected %s", e.reg, value, e.what);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

// FD section 10.1. Writing the clock source and clearing SLEEP leaves PWR_MGMT_1 = 0x01.
esp_err_t configure()
{
    ESP_RETURN_ON_ERROR(mpu6050_set_clock_source(&s_dev, MPU6050_CLOCK_PLL_X), TAG, "clock source");
    ESP_RETURN_ON_ERROR(mpu6050_set_rate(&s_dev, 0), TAG, "sample rate divider");
    ESP_RETURN_ON_ERROR(mpu6050_set_dlpf_mode(&s_dev, MPU6050_DLPF_4), TAG, "DLPF");
    ESP_RETURN_ON_ERROR(mpu6050_set_full_scale_gyro_range(&s_dev, MPU6050_GYRO_RANGE_2000), TAG, "gyro range");
    ESP_RETURN_ON_ERROR(mpu6050_set_full_scale_accel_range(&s_dev, MPU6050_ACCEL_RANGE_4), TAG, "accel range");
    ESP_RETURN_ON_ERROR(mpu6050_set_sleep_enabled(&s_dev, false), TAG, "wake");
    return verify_config();
}

}  // namespace

esp_err_t imu_init(const ImuConfig &config)
{
    const I2cBusConfig &bus = i2c_bus_config();
    if (i2c_bus_handle() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = config;

    ESP_RETURN_ON_ERROR(mpu6050_init_desc(&s_dev, kImuAddress, bus.port, bus.sda, bus.scl), TAG,
                        "descriptor failed");
    // mpu6050_init_desc() sets 1 MHz; the device handle is created with this
    // speed on the first transaction, which comes next.
    s_dev.i2c_dev.cfg.master.clk_speed = bus.scl_hz;

    if (read_reg(kRegWhoAmI, s_who_am_i) != ESP_OK) {
        s_who_am_i = 0;
        ESP_LOGE(TAG, "no answer at 0x%02X: IMU_MISSING", kImuAddress);
        return ESP_ERR_NOT_FOUND;
    }
    if (s_who_am_i != kImuWhoAmI) {
        ESP_LOGE(TAG, "WHO_AM_I 0x%02X, expected 0x%02X", s_who_am_i, kImuWhoAmI);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_ERROR(configure(), TAG, "configuration read-back");

    s_stats = {};
    s_reset_armed = true;
    ESP_LOGI(TAG, "MPU-6050 WHO_AM_I 0x%02X; DLPF 4, +-2000 deg/s, +-4 g, %" PRIu32 " Hz SCL",
             s_who_am_i, bus.scl_hz);
    return ESP_OK;
}

esp_err_t imu_reconfigure()
{
    ESP_RETURN_ON_FALSE(s_who_am_i == kImuWhoAmI, ESP_ERR_INVALID_STATE, TAG, "imu_init() never reached the IMU");
    ESP_RETURN_ON_ERROR(configure(), TAG, "configuration read-back");
    s_stats = {};
    s_reset_armed = true;
    ESP_LOGI(TAG, "MPU-6050 configured on a retry");
    return ESP_OK;
}

uint8_t imu_who_am_i()
{
    return s_who_am_i;
}

bool imu_read(ImuSample &out)
{
    auto handle = static_cast<i2c_master_dev_handle_t>(s_dev.i2c_dev.dev_handle);
    uint8_t buf[kBurstBytes];
    const uint8_t reg = kRegAccelXoutH;

    out.t_us = esp_timer_get_time();
    const esp_err_t err = handle == nullptr
                              ? ESP_ERR_INVALID_STATE
                              : i2c_master_transmit_receive(handle, &reg, 1, buf, sizeof buf, s_config.xfer_timeout_ms);
    out.burst_us = static_cast<uint32_t>(esp_timer_get_time() - out.t_us);
    ++s_stats.reads;

    if (err != ESP_OK) {
        out.valid = false;
        ++s_stats.errors;
        ++s_stats.consecutive_errors;
        if (s_reset_armed && s_stats.consecutive_errors >= s_config.errors_before_reset) {
            s_reset_armed = false;
            ++s_stats.bus_resets;
            const esp_err_t reset = i2c_bus_reset();
            ESP_LOGW(TAG, "%" PRIu32 " consecutive errors (last %s): bus reset %s", s_stats.consecutive_errors,
                     esp_err_to_name(err), esp_err_to_name(reset));
        }
        return false;
    }
    s_stats.consecutive_errors = 0;
    s_reset_armed = true;

    for (int i = 0; i < 3; ++i) {
        out.accel[i] = be16(&buf[2 * i]);
        out.gyro[i] = be16(&buf[8 + 2 * i]);
    }
    out.temp_raw = be16(&buf[6]);
    out.valid = out.burst_us <= s_config.max_burst_us;
    if (!out.valid) {
        ++s_stats.slow;
    }
    return out.valid;
}

const ImuStats &imu_stats()
{
    return s_stats;
}

}  // namespace direcsun::drivers
