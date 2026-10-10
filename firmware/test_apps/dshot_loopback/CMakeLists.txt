# DShot bring-up (R3.1): on-chip loopback of the four HT-1 vectors at DShot150
# and DShot300, a corrupted-CRC positive control, then a DShot 0 stream on the
# ESC pin for the oscilloscope and ESC arming checks. See README.md.
cmake_minimum_required(VERSION 3.22)
# Same tracked defaults as the flight firmware (no Wi-Fi layer needed here).
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults")
# The real components, not copies. Only main and what it requires are built.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# C++17, as in firmware/CMakeLists.txt.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17" APPEND)
# Enables DshotRmt::send_raw_frame_for_test (bad-CRC frames only). The flight
# firmware never defines this.
idf_build_set_property(COMPILE_DEFINITIONS "DIRECSUN_DSHOT_ALLOW_RAW" APPEND)
project(direcsun_dshot_loopback)
