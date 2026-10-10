# Hardware access: shared I2C bus, IMU, magnetometer, barometer, Hall capture,
# ADC, DShot over RMT. Every source in src/ is compiled (SRC_DIRS), so features
# add driver files without editing this list. REQUIRES is the full set the
# drivers need, fixed here so parallel features never edit this file.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES direcsun_core
             esp-idf-lib__mpu6050 esp-idf-lib__bmp280 esp-idf-lib__i2cdev qmc5883p
             esp_driver_i2c esp_driver_rmt esp_driver_gpio esp_adc esp_timer
)
