# Pure C++17 control logic: estimator, controllers, mode machine, interlocks,
# protocol, DShot framing. No ESP-IDF, FreeRTOS, or driver headers, so the same
# sources build in firmware/test/host (tools/check-core-purity.sh enforces it).
# Header-only until R1.5 adds the first source file.
idf_component_register(INCLUDE_DIRS "include")
