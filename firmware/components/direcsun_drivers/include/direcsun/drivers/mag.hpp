#pragma once

// QMC5883P on the shared bus (FD section 10.2), through the vendored
// duruofu/qmc5883p driver with the local patches in
// firmware/components/qmc5883p/LOCAL_PATCHES.md. Raw axes only: the axis map
// and calibration belong to Set 5.

#include <cstdint>

#include "esp_err.h"

namespace direcsun::drivers {

constexpr uint8_t kMagAddress = 0x2C;
constexpr uint8_t kMagChipId = 0x80;            // register 0x00
constexpr float kMagLsbPerGauss = 3750.0f;      // +-8 G range (datasheet Table 2)

struct MagConfig {
    int xfer_timeout_ms = 2;      // upper bound on one transaction, in ms
    uint32_t max_xfer_us = 1000;  // a slower status-plus-data read is invalid
};

struct MagSample {
    int16_t raw[3];     // x, y, z as the chip reports them (kMagLsbPerGauss per gauss)
    bool overflow;      // OVFL: an axis exceeded +-30000 LSB
    int64_t t_us;       // esp_timer time at the start of the poll
    uint32_t xfer_us;   // status plus data read time
};

struct MagStats {
    uint32_t polls;      // status reads attempted
    uint32_t ready;      // polls that found DRDY and returned a sample
    uint32_t errors;     // polls that hit a bus error
    uint32_t slow;       // samples dropped for exceeding max_xfer_us
    uint32_t overflows;  // samples with OVFL set
};

// Adds the chip to the shared bus at the bus speed, checks the chip ID, and
// sets 8 G range, normal mode, ODR 200 Hz. Returns ESP_ERR_NOT_FOUND when the
// chip does not answer (MAG_MISSING, not fatal) and ESP_ERR_INVALID_RESPONSE
// for a wrong chip ID or configuration read-back. On failure the device is
// removed from the bus again. Requires i2c_bus_init().
esp_err_t mag_init(const MagConfig &config = MagConfig{});

// Chip ID as read by mag_init() (0 if the read failed).
uint8_t mag_chip_id();

// Reads the status register and, only when DRDY is set, the six data bytes
// (FD section 10.2: poll every 2nd tick, so each 200 Hz sample is read once).
// Returns true when `out` holds a new valid sample. Call from a single task.
bool mag_poll(MagSample &out);

const MagStats &mag_stats();

}  // namespace direcsun::drivers
