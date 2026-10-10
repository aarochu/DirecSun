# Hardware access: shared I2C bus, IMU, magnetometer, barometer, Hall capture,
# ADC, DShot over RMT. Empty until Set 3; the REQUIRES list makes every sensor
# library compile and link now, so integration breaks surface in Set 1.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES direcsun_core esp-idf-lib__mpu6050 esp-idf-lib__bmp280 esp-idf-lib__i2cdev qmc5883p
)
