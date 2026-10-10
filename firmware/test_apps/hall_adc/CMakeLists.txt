# Bring-up app for R3.3: Hall edge capture, wheel-speed estimates, battery and
# sun ADC. Pattern from firmware/test_apps/README.md.
cmake_minimum_required(VERSION 3.22)
# Same tracked defaults as the flight firmware (no Wi-Fi layer needed here).
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults")
# The real components, not copies.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# C++17, as in firmware/CMakeLists.txt.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17" APPEND)
# Build only main and what it requires, so the comms component (Wi-Fi, mDNS,
# cJSON) in the shared component directory stays out of this app.
idf_build_set_property(MINIMAL_BUILD ON)
project(direcsun_hall_adc)
