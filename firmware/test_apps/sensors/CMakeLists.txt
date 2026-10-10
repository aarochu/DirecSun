# R3.2 bring-up: MPU-6050, QMC5883P, and BMP280 on one i2cdev-owned bus.
# Pattern: firmware/test_apps/README.md.
cmake_minimum_required(VERSION 3.22)
# Same tracked defaults as the flight firmware (no Wi-Fi layer needed here).
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults")
# The real components, not copies.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# C++17, as in firmware/CMakeLists.txt.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17" APPEND)
project(direcsun_sensors)
