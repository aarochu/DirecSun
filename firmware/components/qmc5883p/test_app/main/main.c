#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "qmc5883p.h"

#define I2C_MASTER_SCL_IO           1
#define I2C_MASTER_SDA_IO           2
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000

// QMC5883P Address (Default 0x2C)
#define QMC5883P_ADDR               QMC5883P_I2C_ADDR_DEF

static const char *TAG = "qmc5883p_example";

static esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true
    };

    return i2c_new_master_bus(&bus_config, bus_handle);
}

static void qmc5883p_test_task(void *arg)
{
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)arg;
    qmc5883p_dev_t dev = {0};
    qmc5883p_data_t data;

    ESP_LOGI(TAG, "Initializing QMC5883P...");
    esp_err_t ret = qmc5883p_init(&dev, bus_handle, QMC5883P_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize QMC5883P (err=%d)", ret);
        vTaskDelete(NULL);
    }

    // Configure sensor parameters
    ESP_LOGI(TAG, "Configuring QMC5883P...");
    // qmc5883p_set_range(&dev, QMC5883P_RNG_8G);
    // qmc5883p_set_odr(&dev, QMC5883P_ODR_200HZ);
    // qmc5883p_set_mode(&dev, QMC5883P_MODE_CONTINUOUS);

    // Debug: Check registers
    uint8_t ctrl1, ctrl2;
    qmc5883p_read_register(&dev, QMC5883P_REG_CTRL1, &ctrl1, 1);
    qmc5883p_read_register(&dev, QMC5883P_REG_CTRL2, &ctrl2, 1);
    ESP_LOGI(TAG, "Reg Verify: CTRL1=0x%02X, CTRL2=0x%02X", ctrl1, ctrl2);

    ESP_LOGI(TAG, "Starting data loop...");
    
    while (1) {
        bool ready = false;
        ret = qmc5883p_is_data_ready(&dev, &ready);
        
        if (ret == ESP_OK && ready) {
            ret = qmc5883p_read_data(&dev, &data);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Mag: X=%.3f Gauss, Y=%.3f Gauss, Z=%.3f Gauss", 
                         data.x, data.y, data.z);
                ESP_LOGI(TAG, "Raw: X=%d, Y=%d, Z=%d", data.raw_x, data.raw_y, data.raw_z);
                ESP_LOGI(TAG, "----------------------------------------");
            } else {
                ESP_LOGE(TAG, "Failed to read sensor data (err=%d)", ret);
            }
        } else {
             // Debug info if not ready
             if (ret != ESP_OK) {
                 ESP_LOGE(TAG, "Data ready check failed (err=%d)", ret);
             } else {
                 // ESP_LOGW(TAG, "Data not ready..."); 
                 // Too noisy, maybe just print status reg?
                 uint8_t status;
                 qmc5883p_read_register(&dev, QMC5883P_REG_STATUS, &status, 1);
                 ESP_LOGD(TAG, "Status Reg: 0x%02X", status);
             }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing I2C...");
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_master_init(&bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C (err=%d)", ret);
        return;
    }

    xTaskCreate(qmc5883p_test_task, "qmc5883p_task", 4096, bus_handle, 5, NULL);
}
