#include "direcsun/drivers/baro.hpp"

#include <cinttypes>

#include "bmp280.h"
#include "direcsun/drivers/i2c_bus.hpp"
#include "esp_log.h"

namespace direcsun::drivers {

namespace {

constexpr const char *TAG = "baro";

bmp280_t s_dev = {};
bool s_present = false;

}  // namespace

esp_err_t baro_init()
{
    const I2cBusConfig &bus = i2c_bus_config();
    if (i2c_bus_handle() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_present = false;

    esp_err_t err = bmp280_init_desc(&s_dev, kBaroAddress, bus.port, bus.sda, bus.scl);
    if (err != ESP_OK) {
        return err;
    }
    // bmp280_init_desc() sets 1 MHz; the device handle is created with this
    // speed on the first transaction, inside bmp280_init().
    s_dev.i2c_dev.cfg.master.clk_speed = bus.scl_hz;

    bmp280_params_t params;
    bmp280_init_default_params(&params);
    err = bmp280_init(&s_dev, &params);
    if (err != ESP_OK) {
        if (s_dev.id != 0 && s_dev.id != kBaroChipId) {
            ESP_LOGE(TAG, "chip ID 0x%02X at 0x%02X, expected 0x%02X", s_dev.id, kBaroAddress, kBaroChipId);
            return ESP_ERR_INVALID_RESPONSE;
        }
        ESP_LOGW(TAG, "no BMP280 at 0x%02X (%s); continuing without it", kBaroAddress, esp_err_to_name(err));
        s_dev.id = 0;
        return ESP_ERR_NOT_FOUND;
    }
    if (s_dev.id != kBaroChipId) {
        ESP_LOGE(TAG, "chip ID 0x%02X at 0x%02X, expected 0x%02X", s_dev.id, kBaroAddress, kBaroChipId);
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_present = true;
    ESP_LOGI(TAG, "BMP280 chip ID 0x%02X at 0x%02X, %" PRIu32 " Hz SCL", s_dev.id, kBaroAddress, bus.scl_hz);
    return ESP_OK;
}

bool baro_present()
{
    return s_present;
}

uint8_t baro_chip_id()
{
    return s_dev.id;
}

esp_err_t baro_read(float &temperature_c, float &pressure_pa)
{
    if (!s_present) {
        return ESP_ERR_INVALID_STATE;
    }
    return bmp280_read_float(&s_dev, &temperature_c, &pressure_pa, nullptr);
}

}  // namespace direcsun::drivers
