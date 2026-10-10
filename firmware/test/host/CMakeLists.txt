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

add_executable(host_tests ${CORE_SOURCES} ${TEST_SOURCES})
target_include_directories(host_tests PRIVATE "${CORE_DIR}/include")
target_include_directories(host_tests SYSTEM PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/doctest")
target_compile_options(host_tests PRIVATE
    -Wall -Wextra -Werror
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
target_link_options(host_tests PRIVATE -fsanitize=address,undefined)
if(DIRECSUN_HT_SELFTEST)
    target_compile_definitions(host_tests PRIVATE DIRECSUN_HT_SELFTEST)
endif()

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
