# Host tests for direcsun_core: plain CMake, the desktop compiler, no ESP-IDF.
# Run through tools/host-test.sh, which also checks that tests actually ran.
cmake_minimum_required(VERSION 3.16)
project(direcsun_host_tests C CXX)

# Same language level as the firmware (gnu++17, see firmware/CMakeLists.txt).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(DIRECSUN_HT_SELFTEST "Compile the deliberately failing positive-control test" OFF)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../components/direcsun_core")
# Every core source, matching SRC_DIRS "src" in the core component's CMakeLists.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CORE_DIR}/src/*.cpp")
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")

# The vendored cJSON (direcsun_core/third_party/cjson), the same file the firmware
# compiles, with the same nesting limit (third_party/cjson/README.md).
set(CJSON_DIR "${CORE_DIR}/third_party/cjson")
add_library(direcsun_cjson STATIC "${CJSON_DIR}/cJSON.c")
target_compile_definitions(direcsun_cjson PUBLIC CJSON_NESTING_LIMIT=8)
target_compile_options(direcsun_cjson PRIVATE
    -Wall -Wextra -Werror
    -Wno-error=deprecated-declarations
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

# Protocol tests read the schema fixtures and write the messages they encode for
# tools/check-protocol.sh to validate against protocol/schema-v1.json.
get_filename_component(REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(DIRECSUN_PROTOCOL_DIR "${REPO_DIR}/protocol")

function(direcsun_host_test_target name)
    add_executable(${name} ${CORE_SOURCES} ${TEST_SOURCES})
    target_include_directories(${name} PRIVATE "${CORE_DIR}/include")
    target_include_directories(${name} SYSTEM PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/doctest" "${CJSON_DIR}")
    target_link_libraries(${name} PRIVATE direcsun_cjson)
    target_compile_definitions(${name} PRIVATE
        DIRECSUN_PROTOCOL_DIR="${DIRECSUN_PROTOCOL_DIR}"
        DIRECSUN_PROTOCOL_CAPTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/protocol-capture")
    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Werror
        -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
endfunction()

direcsun_host_test_target(host_tests)
if(DIRECSUN_HT_SELFTEST)
    target_compile_definitions(host_tests PRIVATE DIRECSUN_HT_SELFTEST)
    # Mutants: the same core and tests with one safety mechanism compiled out
    # (DIRECSUN_MUTANT_* in the core, which refuses to build for the firmware).
    # tools/host-test.sh --selftest runs each one and requires the test that
    # guards that mechanism to fail, proving the test can see the defect.
    foreach(mutant NO_LATCH NO_BYPASS_CLAMP NO_STILL_GATE NO_WIRE_SIGN NO_SPINUP_RAMP NO_WHEEL_ANTIWINDUP
            RATE_SIGN_FLIP NO_RATE_ANTIWINDUP NO_RATE_BLEED RAW_HEADING)
        string(TOLOWER "${mutant}" mutant_lower)
        direcsun_host_test_target(host_tests_mutant_${mutant_lower})
        target_compile_definitions(host_tests_mutant_${mutant_lower} PRIVATE DIRECSUN_MUTANT_${mutant})
    endforeach()
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
