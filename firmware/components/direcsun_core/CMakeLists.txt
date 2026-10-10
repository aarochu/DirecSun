# Pure C++17 control logic: estimator, controllers, mode machine, interlocks,
# protocol, DShot framing. No ESP-IDF, FreeRTOS, or driver headers, so the same
# sources build in firmware/test/host (tools/check-core-purity.sh enforces it).
# SRC_DIRS picks up every source in src/, matching the host build's glob, so a
# new file cannot end up tested on the desktop but missing from the firmware.
idf_component_register(SRC_DIRS "src" INCLUDE_DIRS "include")
