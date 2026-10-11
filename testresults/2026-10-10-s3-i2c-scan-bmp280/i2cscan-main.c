#include <stdio.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = 0, .sda_io_num = 5, .scl_io_num = 6,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    for (int pass = 1; pass <= 5; ++pass) {
        printf("SCAN pass %d:", pass);
        for (uint16_t a = 0x08; a <= 0x77; ++a) {
            if (i2c_master_probe(bus, a, 50) == ESP_OK) printf(" 0x%02x", a);
        }
        printf(" END\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
