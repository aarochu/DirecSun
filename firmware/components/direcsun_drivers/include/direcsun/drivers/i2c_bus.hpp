#pragma once

// The shared sensor bus (FD section 10, TD section 5.3). i2cdev owns the I2C
// master bus; drivers that take a raw i2c_master_bus_handle_t (the QMC5883P)
// get it from i2c_bus_handle(). Every device on the bus runs at the same SCL
// speed, set here.

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace direcsun::drivers {

// XIAO ESP32-S3 sensor bus (TD section 5.4): SDA on D4 = GPIO5, SCL on D5 = GPIO6.
struct I2cBusConfig {
    i2c_port_t port = I2C_NUM_0;
    gpio_num_t sda = GPIO_NUM_5;
    gpio_num_t scl = GPIO_NUM_6;
    uint32_t scl_hz = 400000;  // TD section 5.3; 100 kHz is the documented fallback (FD section 9.2)
};

// Initialises i2cdev and creates the port's master bus. The bus is created
// even when no sensor answers, so the sensors can be probed afterwards in any
// order and a missing one never hides the others. Internal pull-ups stay off:
// each Adafruit board carries 10 kOhm pull-ups (TD section 5.3).
// Call once, before any sensor init; a second call returns ESP_OK.
esp_err_t i2c_bus_init(const I2cBusConfig &config = I2cBusConfig{});

// The configuration the bus was created with. Valid after i2c_bus_init().
const I2cBusConfig &i2c_bus_config();

// The shared bus handle, or nullptr before i2c_bus_init() succeeds.
i2c_master_bus_handle_t i2c_bus_handle();

// Bus recovery (FD section 10.1): nine SCL pulses and a STOP, then a reset of
// the controller (i2c_master_bus_reset). Device handles stay valid.
esp_err_t i2c_bus_reset();

}  // namespace direcsun::drivers
