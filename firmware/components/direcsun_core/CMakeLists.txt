# Pure C++17 control logic: estimator, controllers, mode machine, interlocks,
# protocol, DShot framing. No ESP-IDF, FreeRTOS, or driver headers, so the same
# sources build in firmware/test/host (tools/check-core-purity.sh enforces it).
# SRC_DIRS picks up every source in src/, matching the host build's glob, so a
# new file cannot end up tested on the desktop but missing from the firmware.
# cJSON is vendored here (third_party/cjson) so the firmware and the host tests
# parse with the same code; only protocol.cpp includes it. linker.lf places the
# control-path code in IRAM (firmware build only; see the file).
idf_component_register(
    SRC_DIRS "src" "third_party/cjson"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "third_party/cjson"
    LDFRAGMENTS "linker.lf")

# Inbound messages are at most 512 bytes and 4 levels deep; see
# third_party/cjson/README.md. The host build sets the same value.
target_compile_definitions(${COMPONENT_LIB} PRIVATE CJSON_NESTING_LIMIT=8)
