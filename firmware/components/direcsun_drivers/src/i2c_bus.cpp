#include "direcsun/drivers/i2c_bus.hpp"

#include <cinttypes>

#include "esp_check.h"
#include "esp_log.h"
#include "i2cdev.h"

namespace direcsun::drivers {

namespace {

constexpr const char *TAG = "i2c_bus";

I2cBusConfig s_config;
i2c_master_bus_handle_t s_bus = nullptr;

// Exists only to make i2cdev create the bus: i2c_dev_check_present() sets up
// the port and then probes this address, without adding a device handle.
// 0x68 is the MPU-6050, so the probe result is logged as a first sign of life.
i2c_dev_t s_anchor = {};

}  // namespace

esp_err_t i2c_bus_init(const I2cBusConfig &config)
{
    if (s_bus != nullptr) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2cdev_init(), TAG, "i2cdev_init failed");

    s_config = config;
    s_anchor.port = config.port;
    s_anchor.addr = 0x68;
    s_anchor.cfg.sda_io_num = config.sda;
    s_anchor.cfg.scl_io_num = config.scl;
    s_anchor.cfg.sda_pullup_en = 0;
    s_anchor.cfg.scl_pullup_en = 0;
    s_anchor.cfg.master.clk_speed = config.scl_hz;

    // Creates the bus first, then probes; a failed probe still leaves the bus installed.
    const esp_err_t probe = i2c_dev_check_present(&s_anchor);

    void *handle = nullptr;
    const esp_err_t err = i2cdev_get_shared_handle(config.port, &handle);
    if (err != ESP_OK || handle == nullptr) {
        ESP_LOGE(TAG, "bus on port %d not created (probe %s, handle %s)", config.port,
                 esp_err_to_name(probe), esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_FAIL;
    }
    s_bus = static_cast<i2c_master_bus_handle_t>(handle);
    ESP_LOGI(TAG, "bus on port %d: SDA GPIO%d, SCL GPIO%d, %" PRIu32 " Hz per device; probe of 0x68: %s",
             config.port, config.sda, config.scl, config.scl_hz, esp_err_to_name(probe));
    return ESP_OK;
}

const I2cBusConfig &i2c_bus_config()
{
    return s_config;
}

i2c_master_bus_handle_t i2c_bus_handle()
{
    return s_bus;
}

esp_err_t i2c_bus_reset()
{
    if (s_bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_bus_reset(s_bus);
}

}  // namespace direcsun::drivers
