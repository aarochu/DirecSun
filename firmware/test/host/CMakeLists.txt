# Host tests for direcsun_core: plain CMake, the desktop compiler, no ESP-IDF.
# Run through tools/host-test.sh, which also checks that tests actually ran.
cmake_minimum_required(VERSION 3.16)
project(direcsun_host_tests CXX)

# Same language level as the firmware (gnu++17, see firmware/CMakeLists.txt).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(DIRECSUN_HT_SELFTEST "Compile the deliberately failing positive-control test" OFF)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../components/direcsun_core")
# Every core source, matching SRC_DIRS "src" in the core component's CMakeLists.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CORE_DIR}/src/*.cpp")
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.cpp")

function(direcsun_host_test_target name)
    add_executable(${name} ${CORE_SOURCES} ${TEST_SOURCES})
    target_include_directories(${name} PRIVATE "${CORE_DIR}/include")
    target_include_directories(${name} SYSTEM PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/doctest")
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
    foreach(mutant NO_LATCH NO_BYPASS_CLAMP)
        string(TOLOWER "${mutant}" mutant_lower)
        direcsun_host_test_target(host_tests_mutant_${mutant_lower})
        target_compile_definitions(host_tests_mutant_${mutant_lower} PRIVATE DIRECSUN_MUTANT_${mutant})
    endforeach()
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
