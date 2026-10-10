#include "direcsun/drivers/mag.hpp"

#include <cinttypes>

#include "direcsun/drivers/i2c_bus.hpp"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "qmc5883p.h"

namespace direcsun::drivers {

namespace {

constexpr const char *TAG = "mag";

constexpr uint8_t kStatusDrdy = 0x01;
constexpr uint8_t kStatusOvfl = 0x02;
// Written by qmc5883p_init(): datasheet section 7.1 normal-mode setup.
constexpr uint8_t kExpectCtrl1 = 0xCD;  // OSR2 11, OSR1 00, ODR 200 Hz, MODE normal
constexpr uint8_t kExpectCtrl2 = 0x08;  // RNG 8 G, set/reset on

qmc5883p_dev_t s_dev = {};
MagConfig s_config;
MagStats s_stats = {};
uint8_t s_chip_id = 0;

void remove_device()
{
    if (s_dev.dev_handle != nullptr) {
        i2c_master_bus_rm_device(s_dev.dev_handle);
        s_dev.dev_handle = nullptr;
    }
}

}  // namespace

esp_err_t mag_init(const MagConfig &config)
{
    i2c_master_bus_handle_t bus = i2c_bus_handle();
    if (bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = config;
    s_chip_id = 0;

    const esp_err_t init = qmc5883p_init(&s_dev, bus, kMagAddress, i2c_bus_config().scl_hz);
    if (qmc5883p_get_chip_id(&s_dev, &s_chip_id) != ESP_OK) {
        s_chip_id = 0;
        ESP_LOGE(TAG, "no answer at 0x%02X: MAG_MISSING", kMagAddress);
        remove_device();
        return ESP_ERR_NOT_FOUND;
    }
    if (s_chip_id != kMagChipId) {
        ESP_LOGE(TAG, "chip ID 0x%02X, expected 0x%02X", s_chip_id, kMagChipId);
        remove_device();
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t ctrl1 = 0;
    uint8_t ctrl2 = 0;
    if (init != ESP_OK || qmc5883p_read_register(&s_dev, QMC5883P_REG_CTRL1, &ctrl1, 1) != ESP_OK ||
        qmc5883p_read_register(&s_dev, QMC5883P_REG_CTRL2, &ctrl2, 1) != ESP_OK || ctrl1 != kExpectCtrl1 ||
        ctrl2 != kExpectCtrl2) {
        ESP_LOGE(TAG, "configuration failed (init %s, CTRL1 0x%02X, CTRL2 0x%02X)", esp_err_to_name(init), ctrl1,
                 ctrl2);
        remove_device();
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_stats = {};
    ESP_LOGI(TAG, "QMC5883P chip ID 0x%02X; 8 G, normal mode, ODR 200 Hz, %" PRIu32 " Hz SCL", s_chip_id,
             i2c_bus_config().scl_hz);
    return ESP_OK;
}

uint8_t mag_chip_id()
{
    return s_chip_id;
}

bool mag_poll(MagSample &out)
{
    if (s_dev.dev_handle == nullptr) {
        return false;
    }
    ++s_stats.polls;
    out.t_us = esp_timer_get_time();

    uint8_t reg = QMC5883P_REG_STATUS;
    uint8_t status = 0;
    if (i2c_master_transmit_receive(s_dev.dev_handle, &reg, 1, &status, 1, s_config.xfer_timeout_ms) != ESP_OK) {
        ++s_stats.errors;
        return false;
    }
    if ((status & kStatusDrdy) == 0) {
        return false;
    }

    uint8_t buf[6];
    reg = QMC5883P_REG_X_L;
    if (i2c_master_transmit_receive(s_dev.dev_handle, &reg, 1, buf, sizeof buf, s_config.xfer_timeout_ms) != ESP_OK) {
        ++s_stats.errors;
        return false;
    }
    out.xfer_us = static_cast<uint32_t>(esp_timer_get_time() - out.t_us);
    if (out.xfer_us > s_config.max_xfer_us) {
        ++s_stats.slow;
        return false;
    }

    // Little-endian, X, Y, Z from 0x01 (datasheet Table 15).
    for (int i = 0; i < 3; ++i) {
        out.raw[i] = static_cast<int16_t>((static_cast<uint16_t>(buf[2 * i + 1]) << 8) | buf[2 * i]);
    }
    out.overflow = (status & kStatusOvfl) != 0;
    if (out.overflow) {
        ++s_stats.overflows;
    }
    ++s_stats.ready;
    return true;
}

const MagStats &mag_stats()
{
    return s_stats;
}

}  // namespace direcsun::drivers
