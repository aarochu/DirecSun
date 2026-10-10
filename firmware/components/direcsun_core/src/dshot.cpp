#include "direcsun/core/dshot.hpp"

namespace direcsun {

uint16_t dshot_frame(uint16_t value12)
{
    const uint16_t v = value12 & 0x0FFFu;
    return static_cast<uint16_t>((v << 4) | dshot_crc(v));
}

uint16_t dshot_frame_for_value(uint16_t value11)
{
    return dshot_frame(static_cast<uint16_t>((value11 & kDshotMaxValue) << 1));
}

uint16_t dshot_value_for_throttle(int request)
{
    if (request < kDshotMinThrottle) {
        return 0;  // negative, zero, and the command range 1 to 47
    }
    if (request > kDshotMaxValue) {
        return kDshotMaxValue;
    }
    return static_cast<uint16_t>(request);
}

DshotTiming dshot_timing(uint32_t rate_kbit_s, uint32_t resolution_hz)
{
    // Rounded to the nearest tick; computed in 64 bits so large resolutions
    // cannot overflow.
    const uint64_t bit_hz = static_cast<uint64_t>(rate_kbit_s) * 1000u;
    const auto round_div = [](uint64_t num, uint64_t den) {
        return static_cast<uint32_t>((num + den / 2) / den);
    };
    DshotTiming t{};
    t.bit_ticks = round_div(resolution_hz, bit_hz);
    t.one_high_ticks = round_div(static_cast<uint64_t>(resolution_hz) * 3, bit_hz * 4);
    t.zero_high_ticks = round_div(static_cast<uint64_t>(resolution_hz) * 3, bit_hz * 8);
    t.one_threshold_ticks = (t.one_high_ticks + t.zero_high_ticks + 1) / 2;
    return t;
}

bool dshot_frame_from_pulses(const uint32_t* high_ticks, size_t count,
                             uint32_t one_threshold_ticks, uint16_t* frame16)
{
    if (high_ticks == nullptr || frame16 == nullptr || count != 16) {
        return false;
    }
    uint16_t frame = 0;
    for (size_t i = 0; i < count; ++i) {
        frame = static_cast<uint16_t>(frame << 1);
        if (high_ticks[i] >= one_threshold_ticks) {
            frame |= 1u;
        }
    }
    *frame16 = frame;
    return true;
}

bool dshot_check_frame(uint16_t frame16, uint16_t* value12)
{
    const uint16_t v = static_cast<uint16_t>(frame16 >> 4);
    if ((frame16 & 0x0Fu) != dshot_crc(v)) {
        return false;
    }
    if (value12 != nullptr) {
        *value12 = v;
    }
    return true;
}

}  // namespace direcsun
