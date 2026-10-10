# DirecSun flight firmware: ESP-IDF v6.0, target esp32s3, C++17.
# Build with tools/idf.sh from anywhere in the repo, or idf.py from this directory.
cmake_minimum_required(VERSION 3.22)

# Tracked defaults first, then the gitignored local layer (Wi-Fi credentials)
# when it exists. Later files override earlier ones.
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
endif()
# Extra layers, for example the bench build: -D DIRECSUN_EXTRA_DEFAULTS=sdkconfig.bench
# (paths relative to this directory). Use a separate build directory and
# SDKCONFIG for it, so the flight sdkconfig is never touched.
if(DEFINED DIRECSUN_EXTRA_DEFAULTS)
    foreach(layer ${DIRECSUN_EXTRA_DEFAULTS})
        if(NOT IS_ABSOLUTE "${layer}")
            set(layer "${CMAKE_CURRENT_LIST_DIR}/${layer}")
        endif()
        list(APPEND SDKCONFIG_DEFAULTS "${layer}")
    endforeach()
endif()

# Firmware version as reported in the boot banner and the app descriptor.
execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    OUTPUT_VARIABLE PROJECT_VER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT PROJECT_VER)
    set(PROJECT_VER "unknown")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# C++17 for every C++ translation unit. IDF v6.0 appends -std=gnu++26 to
# CXX_COMPILE_OPTIONS while project.cmake is included; appending here puts
# -std=gnu++17 after it on every compile line, and GCC honours the last -std.
# The host test build (firmware/test/host) uses the same standard, so core code
# that compiles on the desktop compiles here too.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17" APPEND)

project(direcsun)
