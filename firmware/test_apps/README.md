# Bring-up test apps

Each directory here is its own small ESP-IDF project. It brings up one driver from `firmware/components/` on the XIAO without touching `firmware/main/`, so several drivers can be developed in parallel.

## Pattern

`<name>/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22)
# Same tracked defaults as the flight firmware (no Wi-Fi layer needed here).
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults")
# The real components, not copies.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# C++17, as in firmware/CMakeLists.txt.
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17" APPEND)
project(direcsun_<name>)
```

`<name>/main/` holds `CMakeLists.txt` and `app_main.cpp`. Commit `<name>/dependencies.lock`; `build/`, `sdkconfig`, and `managed_components/` are ignored, because `firmware/.gitignore`'s patterns match at any depth.

## Running

`tools/idf.sh` runs from `firmware/`, so pass the app with `-C`:

```bash
tools/idf.sh -C test_apps/<name> set-target esp32s3
tools/idf.sh -C test_apps/<name> -p /dev/ttyACM0 build flash monitor
```
