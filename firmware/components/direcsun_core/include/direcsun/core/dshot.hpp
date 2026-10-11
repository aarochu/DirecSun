#pragma once

#include <cstddef>
#include <cstdint>

// DShot framing (FD section 10.6). Pure: no hardware, so the same code is
// checked on the desktop (HT-1) and drives the RMT driver on the box.
//
// A frame is 16 bits sent MSB first: an 11-bit value, one telemetry-request
// bit (always 0 here), and a 4-bit CRC over the 12 bits before it.
// Values: 0 = stop/disarm, 1 to 47 = ESC commands (never sent by flight code,
// FD section 21 rule 1), 48 to 2047 = throttle.

namespace direcsun {

inline constexpr uint16_t kDshotMaxValue = 2047;
inline constexpr uint16_t kDshotMinThrottle = 48;

// 4-bit CRC over a 12-bit value: (v ^ (v >> 4) ^ (v >> 8)) & 0xF.
constexpr uint16_t dshot_crc(uint16_t value12)
{
    const uint16_t v = value12 & 0x0FFFu;
    return static_cast<uint16_t>((v ^ (v >> 4) ^ (v >> 8)) & 0x0Fu);
}

// 16-bit frame for a 12-bit value (11-bit value shifted left, telemetry bit 0).
uint16_t dshot_frame(uint16_t value12);

// 16-bit frame for an 11-bit value with the telemetry bit clear.
uint16_t dshot_frame_for_value(uint16_t value11);

// Maps a controller request onto a value that is legal to send:
// 0 stays 0, 1 to 47 become 0 (commands are never sent), 48 to 2047 pass
// through, anything above 2047 clamps to 2047, and negatives become 0.
uint16_t dshot_value_for_throttle(int request);

// True for the values flight code may send: 0, or 48 to 2047.
constexpr bool dshot_value_is_sendable(uint16_t value11)
{
    return value11 == 0 || (value11 >= kDshotMinThrottle && value11 <= kDshotMaxValue);
}

// Bit timing in channel ticks for a DShot rate (kbit/s) at a tick resolution.
// The '1' bit is high for 75 % of the bit, the '0' bit for 37.5 %.
struct DshotTiming {
    uint32_t bit_ticks;
    uint32_t one_high_ticks;
    uint32_t zero_high_ticks;
    // Decoding: a high pulse at least this long is a '1'. Midway between the two.
    uint32_t one_threshold_ticks;
};
DshotTiming dshot_timing(uint32_t rate_kbit_s, uint32_t resolution_hz);

// Rebuilds a frame from the 16 high-pulse widths of a received frame (MSB
// first). Returns false unless exactly 16 pulses are given.
bool dshot_frame_from_pulses(const uint32_t* high_ticks, size_t count,
                             uint32_t one_threshold_ticks, uint16_t* frame16);

// Splits a frame into its 12-bit value. Returns false if the CRC is wrong.
bool dshot_check_frame(uint16_t frame16, uint16_t* value12);

}  // namespace direcsun
