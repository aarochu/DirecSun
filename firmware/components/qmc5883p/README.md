# QMC5883P 三轴数字罗盘

QMC5883P 为三轴数字罗盘传感器，支持 I2C 通信，可用于磁场方向检测。

此组件未完成数据校准功能, 请根据实际应用场景进行校准.

## 使用示例

### 初始化 I2C
```c
static esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 15,
        .scl_io_num = 14,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true
    };
    return i2c_new_master_bus(&bus_config, bus_handle);
}
```

### 设备初始化与模式设置
```c
qmc5883p_t dev;
esp_err_t ret = qmc5883p_init_desc_ex(&dev, bus_handle, QMC5883P_I2C_ADDR, 100000);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "init failed: %d", ret);
}

ret = qmc5883p_reset(&dev);
ret = qmc5883p_set_mode(&dev, QMC5883P_MODE_CONTINUOUS);
```

### 读取数据
```c
qmc5883p_data_t data;
bool ready = false;
if (qmc5883p_data_ready(&dev, &ready) == ESP_OK && ready) {
    if (qmc5883p_get_data(&dev, &data) == ESP_OK) {
        ESP_LOGI(TAG, "Magnetic: X=%.2f mG, Y=%.2f mG, Z=%.2f mG", data.x, data.y, data.z);
    }
}
```
