// HT-1: DShot framing (FD section 10.6 and section 19.1).
#include <array>
#include <cstdint>

#include "direcsun/core/dshot.hpp"
#include "doctest.h"

using namespace direcsun;

TEST_CASE("HT-1: dshot frames match the FD section 10.6 vectors")
{
    // Throttle, 12-bit value, CRC, frame, as tabulated in FD section 10.6.
    struct Vector {
        uint16_t throttle;
        uint16_t value12;
        uint16_t crc;
        uint16_t frame;
    };
    const std::array<Vector, 4> vectors{{
        {0, 0x000, 0x0, 0x0000},
        {48, 0x060, 0x6, 0x0606},
        {1046, 0x82C, 0x6, 0x82C6},
        {2047, 0xFFE, 0xE, 0xFFEE},
    }};
    for (const Vector& v : vectors) {
        CAPTURE(v.throttle);
        CHECK(static_cast<uint16_t>(v.throttle << 1) == v.value12);
        CHECK(dshot_crc(v.value12) == v.crc);
        CHECK(dshot_frame(v.value12) == v.frame);
        CHECK(dshot_frame_for_value(v.throttle) == v.frame);
    }
}

TEST_CASE("HT-1: requests in the command range 1 to 47 are never sendable")
{
    for (int request = 1; request <= 47; ++request) {
        CAPTURE(request);
        CHECK(dshot_value_for_throttle(request) == 0);
        CHECK_FALSE(dshot_value_is_sendable(static_cast<uint16_t>(request)));
    }
}

TEST_CASE("HT-1: throttle requests clamp into 0 or 48 to 2047")
{
    CHECK(dshot_value_for_throttle(0) == 0);
    CHECK(dshot_value_for_throttle(-1) == 0);
    CHECK(dshot_value_for_throttle(-100000) == 0);
    CHECK(dshot_value_for_throttle(48) == 48);
    CHECK(dshot_value_for_throttle(1046) == 1046);
    CHECK(dshot_value_for_throttle(2047) == 2047);
    CHECK(dshot_value_for_throttle(2048) == 2047);
    CHECK(dshot_value_for_throttle(65535) == 2047);

    // Every possible request maps to a sendable value.
    for (int request = -5000; request <= 70000; ++request) {
        REQUIRE(dshot_value_is_sendable(dshot_value_for_throttle(request)));
    }
    CHECK(dshot_value_is_sendable(0));
    CHECK(dshot_value_is_sendable(48));
    CHECK(dshot_value_is_sendable(2047));
    CHECK_FALSE(dshot_value_is_sendable(2048));
}

TEST_CASE("dshot timing at 40 MHz matches the FD and R3.1 tick counts")
{
    const DshotTiming d150 = dshot_timing(150, 40'000'000);
    CHECK(d150.bit_ticks == 267);
    CHECK(d150.one_high_ticks == 200);
    CHECK(d150.zero_high_ticks == 100);
    CHECK(d150.one_threshold_ticks == 150);

    const DshotTiming d300 = dshot_timing(300, 40'000'000);
    CHECK(d300.bit_ticks == 133);
    CHECK(d300.one_high_ticks == 100);
    CHECK(d300.zero_high_ticks == 50);
    CHECK(d300.one_threshold_ticks == 75);
}

TEST_CASE("dshot frames survive an encode and pulse-decode round trip")
{
    const DshotTiming t = dshot_timing(150, 40'000'000);
    for (uint16_t value = 0; value <= kDshotMaxValue; ++value) {
        const uint16_t frame = dshot_frame_for_value(value);
        std::array<uint32_t, 16> pulses{};
        for (int bit = 0; bit < 16; ++bit) {
            const bool one = ((frame >> (15 - bit)) & 1u) != 0;
            pulses[static_cast<size_t>(bit)] = one ? t.one_high_ticks : t.zero_high_ticks;
        }
        uint16_t decoded = 0;
        REQUIRE(dshot_frame_from_pulses(pulses.data(), pulses.size(), t.one_threshold_ticks, &decoded));
        REQUIRE(decoded == frame);
        uint16_t value12 = 0xFFFF;
        REQUIRE(dshot_check_frame(decoded, &value12));
        REQUIRE(value12 == static_cast<uint16_t>(value << 1));
    }
}

TEST_CASE("dshot_check_frame rejects a corrupted CRC (loopback positive control)")
{
    // The loopback test app sends 0x0607 (the 48 vector with a wrong CRC) and
    // expects exactly this rejection.
    CHECK_FALSE(dshot_check_frame(0x0607, nullptr));
    // Every single-bit flip of a valid frame is caught.
    const uint16_t good = dshot_frame_for_value(1046);
    REQUIRE(dshot_check_frame(good, nullptr));
    for (int bit = 0; bit < 16; ++bit) {
        CAPTURE(bit);
        CHECK_FALSE(dshot_check_frame(static_cast<uint16_t>(good ^ (1u << bit)), nullptr));
    }
}

TEST_CASE("dshot_frame_from_pulses needs exactly 16 pulses")
{
    std::array<uint32_t, 17> pulses{};
    uint16_t frame = 0;
    CHECK_FALSE(dshot_frame_from_pulses(pulses.data(), 15, 150, &frame));
    CHECK_FALSE(dshot_frame_from_pulses(pulses.data(), 17, 150, &frame));
    CHECK_FALSE(dshot_frame_from_pulses(nullptr, 16, 150, &frame));
    CHECK(dshot_frame_from_pulses(pulses.data(), 16, 150, &frame));
    CHECK(frame == 0);
}
