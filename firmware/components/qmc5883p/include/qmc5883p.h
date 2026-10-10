/**
 * @file qmc5883p.h
 * @defgroup qmc5883p qmc5883p
 * @{
 *
 * ESP-IDF driver for QMC5883P 3-axis digital compass
 *
 */
#ifndef __QMC5883P_H__
#define __QMC5883P_H__

#include <stdbool.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * I2C address
 */
#define QMC5883P_I2C_ADDR_DEF 0x2C

/**
 * Chip ID
 */
#define QMC5883P_CHIP_ID_VAL 0x80

/**
 * Registers
 */
typedef enum {
    QMC5883P_REG_CHIP_ID = 0x00,
    QMC5883P_REG_X_L     = 0x01,
    QMC5883P_REG_X_H     = 0x02,
    QMC5883P_REG_Y_L     = 0x03,
    QMC5883P_REG_Y_H     = 0x04,
    QMC5883P_REG_Z_L     = 0x05,
    QMC5883P_REG_Z_H     = 0x06,
    QMC5883P_REG_STATUS  = 0x09,
    QMC5883P_REG_CTRL1   = 0x0A,
    QMC5883P_REG_CTRL2   = 0x0B,
    QMC5883P_REG_SIGN    = 0x29,
} qmc5883p_reg_t;

/**
 * Output data rate (ODR)
 */
typedef enum {
    QMC5883P_ODR_10HZ  = 0,
    QMC5883P_ODR_50HZ  = 1,
    QMC5883P_ODR_100HZ = 2,
    QMC5883P_ODR_200HZ = 3
} qmc5883p_odr_t;

/**
 * Oversampling rate (OSR)
 * Note: QMC5883P has OSR1 and OSR2 in CTRL1.
 * This enum simplifies common configurations.
 */
typedef enum {
    QMC5883P_OSR_512 = 0, // OSR2=00, OSR1=00
    QMC5883P_OSR_256 = 1, 
    QMC5883P_OSR_128 = 2,
    QMC5883P_OSR_64  = 3
} qmc5883p_osr_t;

/**
 * Field range
 * Note: Check datasheet for exact mapping.
 * Commonly: 00=2G, 01=8G, 10=12G, 11=30G
 */
typedef enum {
    QMC5883P_RNG_2G  = 0, // 00
    QMC5883P_RNG_8G  = 1, // 01
    QMC5883P_RNG_12G = 2, // 10
    QMC5883P_RNG_30G = 3  // 11
} qmc5883p_range_t;

/**
 * Operation mode
 */
typedef enum {
    QMC5883P_MODE_SUSPEND    = 0, // 00
    QMC5883P_MODE_CONTINUOUS = 1, // 01
    // 10 and 11 are reserved usually, but some drivers use them differently.
    // We will stick to 01 for Continuous.
    QMC5883P_MODE_NORMAL     = 1  // Alias
} qmc5883p_mode_t;

/**
 * Device handle structure
 */
typedef struct {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    uint8_t i2c_addr;
    float scale_factor_x;
    float scale_factor_y;
    float scale_factor_z;
    qmc5883p_range_t range;
} qmc5883p_dev_t;

/**
 * Data structure
 */
typedef struct {
    float x;
    float y;
    float z;
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
} qmc5883p_data_t;

/**
 * @brief Initialize QMC5883P device
 * 
 * @param dev Pointer to device structure
 * @param bus_handle I2C bus handle
 * @param i2c_addr I2C address (default 0x2C)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t qmc5883p_init(qmc5883p_dev_t *dev, i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr);

/**
 * @brief Write register
 */
esp_err_t qmc5883p_write_register(qmc5883p_dev_t *dev, uint8_t reg, uint8_t value);

/**
 * @brief Read register(s)
 */
esp_err_t qmc5883p_read_register(qmc5883p_dev_t *dev, uint8_t reg, uint8_t *data, size_t len);

/**
 * @brief Check if data is ready
 */
esp_err_t qmc5883p_is_data_ready(qmc5883p_dev_t *dev, bool *ready);

/**
 * @brief Read magnetic field data
 * 
 * @param dev Device handle
 * @param data Pointer to data structure to fill
 * @return esp_err_t 
 */
esp_err_t qmc5883p_read_data(qmc5883p_dev_t *dev, qmc5883p_data_t *data);

/**
 * @brief Set Operation Mode
 */
esp_err_t qmc5883p_set_mode(qmc5883p_dev_t *dev, qmc5883p_mode_t mode);

/**
 * @brief Set Range
 */
esp_err_t qmc5883p_set_range(qmc5883p_dev_t *dev, qmc5883p_range_t range);

/**
 * @brief Set ODR
 */
esp_err_t qmc5883p_set_odr(qmc5883p_dev_t *dev, qmc5883p_odr_t odr);

/**
 * @brief Perform Soft Reset
 */
esp_err_t qmc5883p_soft_reset(qmc5883p_dev_t *dev);

/**
 * @brief Get Chip ID
 */
esp_err_t qmc5883p_get_chip_id(qmc5883p_dev_t *dev, uint8_t *id);

#ifdef __cplusplus
}
#endif

#endif // __QMC5883P_H__
