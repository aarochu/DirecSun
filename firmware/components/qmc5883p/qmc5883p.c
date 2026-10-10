#include "qmc5883p.h"
#include "esp_log.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "QMC5883P";

// Sensitivity LSB/Gauss based on range
static const float SENSITIVITY_30G = 1000.0f; // Approx
static const float SENSITIVITY_12G = 2500.0f;
static const float SENSITIVITY_8G  = 12000.0f; // Docs say 3000 for 8G in some places, others 4096.
static const float SENSITIVITY_2G  = 12000.0f;

esp_err_t qmc5883p_init(qmc5883p_dev_t *dev, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr) {
    if (!dev || !bus_handle) return ESP_ERR_INVALID_ARG;

    dev->bus_handle = bus_handle;
    dev->i2c_addr = i2c_addr;
    dev->range = QMC5883P_RNG_8G; // Default
    dev->scale_factor_x = 1.0f / SENSITIVITY_8G;
    dev->scale_factor_y = 1.0f / SENSITIVITY_8G;
    dev->scale_factor_z = 1.0f / SENSITIVITY_8G;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 100000, // Standard 100kHz
        .scl_wait_us = 0,
        .flags.disable_ack_check = false
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device");
        return ret;
    }

    // 1. Check Chip ID to verify connection
    uint8_t chip_id = 0;
    ret = qmc5883p_get_chip_id(dev, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to communicate with QMC5883P (NACK or Error)");
        // Don't return error yet, try reset might help if chip is stuck
    } else {
        ESP_LOGI(TAG, "QMC5883P Chip ID: 0x%02X", chip_id);
        if (chip_id != QMC5883P_CHIP_ID_VAL) {
             ESP_LOGW(TAG, "Unexpected Chip ID (Expected 0x%02X)", QMC5883P_CHIP_ID_VAL);
        }
    }

    // 2. Soft Reset
    // Force reset the CTRL2 register (clearing soft_reset bit and setting range to 2G)
    ret = qmc5883p_write_register(dev, QMC5883P_REG_CTRL2, 0x00);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10)); 

    // 3. Recommended Initialization Flow
    // Register 0x29 (SIGN) = 0x06
    ret = qmc5883p_write_register(dev, QMC5883P_REG_SIGN, 0x06);
    if (ret != ESP_OK) return ret;

    // Register 0x0B (CTRL2): Range 8G
    ret = qmc5883p_set_range(dev, QMC5883P_RNG_8G);
    if (ret != ESP_OK) return ret;

    // Register 0x0A (CTRL1): Mode Normal, ODR 200Hz, OSR1=0
    // User guide says 0xCD.
    // 0xCD = 1100 1101
    // OSR2 (7:6) = 11
    // OSR1 (5:4) = 00
    // ODR  (3:2) = 11 (200Hz)
    // MODE (1:0) = 01 (Normal)
    ret = qmc5883p_write_register(dev, QMC5883P_REG_CTRL1, 0xCD);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "QMC5883P initialized successfully");
    return ESP_OK;
}

esp_err_t qmc5883p_write_register(qmc5883p_dev_t *dev, uint8_t reg, uint8_t value) {
    if (!dev || !dev->dev_handle) return ESP_ERR_INVALID_ARG;
    uint8_t tx[2] = {reg, value};
    return i2c_master_transmit(dev->dev_handle, tx, 2, 1000);
}

esp_err_t qmc5883p_read_register(qmc5883p_dev_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    if (!dev || !dev->dev_handle || !data) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(dev->dev_handle, &reg, 1, data, len, 1000);
}

esp_err_t qmc5883p_get_chip_id(qmc5883p_dev_t *dev, uint8_t *id) {
    return qmc5883p_read_register(dev, QMC5883P_REG_CHIP_ID, id, 1);
}

esp_err_t qmc5883p_soft_reset(qmc5883p_dev_t *dev) {
    // Write 0x80 to CTRL2 (0x0B)
    esp_err_t ret = qmc5883p_write_register(dev, QMC5883P_REG_CTRL2, 0x80);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait for reset
    }
    return ret;
}

esp_err_t qmc5883p_is_data_ready(qmc5883p_dev_t *dev, bool *ready) {
    uint8_t status;
    esp_err_t ret = qmc5883p_read_register(dev, QMC5883P_REG_STATUS, &status, 1);
    if (ret == ESP_OK) {
        *ready = (status & 0x01); // Bit 0 is DRDY
    }
    return ret;
}

esp_err_t qmc5883p_read_data(qmc5883p_dev_t *dev, qmc5883p_data_t *data) {
    if (!dev || !data) return ESP_ERR_INVALID_ARG;

    // Check Status first for Overflow (OVL)
    uint8_t status;
    esp_err_t ret = qmc5883p_read_register(dev, QMC5883P_REG_STATUS, &status, 1);
    if (ret != ESP_OK) return ret;
    
    if (status & 0x02) { // Bit 1 is OVL
        ESP_LOGW(TAG, "Magnetic sensor overflow!");
        // We can still read data, but it might be clipped
    }

    uint8_t buf[6];
    ret = qmc5883p_read_register(dev, QMC5883P_REG_X_L, buf, 6);
    if (ret != ESP_OK) return ret;

    // Data is Little Endian
    data->raw_x = (int16_t)((buf[1] << 8) | buf[0]);
    data->raw_y = (int16_t)((buf[3] << 8) | buf[2]);
    data->raw_z = (int16_t)((buf[5] << 8) | buf[4]);

    // Convert to Gauss (or mG)
    // Using simple scale factor for now
    data->x = data->raw_x * dev->scale_factor_x;
    data->y = data->raw_y * dev->scale_factor_y;
    data->z = data->raw_z * dev->scale_factor_z;

    return ESP_OK;
}

esp_err_t qmc5883p_set_mode(qmc5883p_dev_t *dev, qmc5883p_mode_t mode) {
    uint8_t ctrl1;
    esp_err_t ret = qmc5883p_read_register(dev, QMC5883P_REG_CTRL1, &ctrl1, 1);
    if (ret != ESP_OK) return ret;

    ctrl1 &= ~0x03; // Clear Mode bits (1:0)
    ctrl1 |= (mode & 0x03);

    return qmc5883p_write_register(dev, QMC5883P_REG_CTRL1, ctrl1);
}

esp_err_t qmc5883p_set_range(qmc5883p_dev_t *dev, qmc5883p_range_t range) {
    uint8_t ctrl2;
    esp_err_t ret = qmc5883p_read_register(dev, QMC5883P_REG_CTRL2, &ctrl2, 1);
    if (ret != ESP_OK) return ret;

    ctrl2 &= ~0x0C; // Clear RNG bits (3:2) -> Mask 0000 1100
    ctrl2 |= ((range & 0x03) << 2);

    ret = qmc5883p_write_register(dev, QMC5883P_REG_CTRL2, ctrl2);
    if (ret == ESP_OK) {
        dev->range = range;
        // Update scale factors
        switch(range) {
            case QMC5883P_RNG_30G: dev->scale_factor_x = 1.0f/SENSITIVITY_30G; break; 
            case QMC5883P_RNG_12G: dev->scale_factor_x = 1.0f/SENSITIVITY_12G; break;
            case QMC5883P_RNG_8G:  dev->scale_factor_x = 1.0f/SENSITIVITY_8G; break;
            case QMC5883P_RNG_2G:  dev->scale_factor_x = 1.0f/SENSITIVITY_2G; break;
        }
        dev->scale_factor_y = dev->scale_factor_x;
        dev->scale_factor_z = dev->scale_factor_x;
    }
    return ret;
}

esp_err_t qmc5883p_set_odr(qmc5883p_dev_t *dev, qmc5883p_odr_t odr) {
    uint8_t ctrl1;
    esp_err_t ret = qmc5883p_read_register(dev, QMC5883P_REG_CTRL1, &ctrl1, 1);
    if (ret != ESP_OK) return ret;

    ctrl1 &= ~0x0C; // Clear ODR bits (3:2)
    ctrl1 |= ((odr & 0x03) << 2);

    return qmc5883p_write_register(dev, QMC5883P_REG_CTRL1, ctrl1);
}
