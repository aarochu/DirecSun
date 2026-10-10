#pragma once

// BMP280 on the shared bus (FD section 10.3). Optional: it never affects
// control, modes, or boot.
//
// baro_read() goes through the esp-idf-lib driver, whose i2cdev layer retries
// a failed transaction three times with 40 to 160 ms backoff. A bus error can
// therefore block the caller for about 0.3 s. Call it from a low-priority task,
// never from the control task.

#include <cstdint>

#include "esp_err.h"

namespace direcsun::drivers {

constexpr uint8_t kBaroAddress = 0x77;   // Adafruit #2651 default (SDO high), not 0x76
constexpr uint8_t kBaroChipId = 0x58;    // BMP280 (a BME280 reports 0x60)

// Returns ESP_ERR_NOT_FOUND when nothing answers at 0x77 and
// ESP_ERR_INVALID_RESPONSE for a chip that is not a BMP280. Requires i2c_bus_init().
esp_err_t baro_init();

bool baro_present();

// Chip ID as read by baro_init() (0 if absent).
uint8_t baro_chip_id();

// Temperature in deg C and pressure in Pa. Blocking; see the note above.
esp_err_t baro_read(float &temperature_c, float &pressure_pa);

}  // namespace direcsun::drivers
