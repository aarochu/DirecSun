#pragma once

#include <cstddef>

#include "esp_err.h"

namespace direcsun::drivers {

// Analog inputs (FD section 10.5, TD section 5.4). All on ADC1, because ADC2
// cannot be used while Wi-Fi is on. 12 dB attenuation (about 0 to 2.9 V usable)
// with the curve-fitting calibration scheme, so readings are in millivolts at
// the pin.
//
//   Sun A    XIAO D0 = GPIO1, ADC1 channel 0, OP550 emitter load resistor
//   Sun B    XIAO D1 = GPIO2, ADC1 channel 1
//   Battery  XIAO D9 = GPIO8, ADC1 channel 7, divider from the pack (see kBatteryDividerRatio)
//
// One-shot reads take tens of microseconds each, so the control task spreads
// them over separate ticks (FD section 9.1). Single caller only.

enum class AdcInput {
    kSunA,
    kSunB,
    kBattery,
};

esp_err_t adc_inputs_init();

// Calibrated voltage at the pin, in millivolts.
esp_err_t adc_read_mv(AdcInput input, int *millivolts);

// Battery divider: the fraction of the pack voltage the pin sees. TD section 5.1
// designs 100 kOhm / 22 kOhm (0.1803). On the bench rig it is calibrated end to
// end instead: Julian's meter read 1.761 V at the tap with the supply at 10.01 V
// (2026-10-11), a ratio of 0.1759 (FD V-67). Re-measure and change these two
// values whenever the divider's resistors change.
constexpr float kBatteryDividerCalTapVolts = 1.761f;
constexpr float kBatteryDividerCalSupplyVolts = 10.01f;
constexpr float kBatteryDividerRatio = kBatteryDividerCalTapVolts / kBatteryDividerCalSupplyVolts;

constexpr float battery_volts_from_pin_mv(int pin_mv)
{
    return static_cast<float>(pin_mv) / 1000.0f / kBatteryDividerRatio;
}

// Moving average of battery volts over the last kSamples readings. At the
// FD's 50 Hz battery rate that is 1 s, the window the BATTERY_LOW interlock
// uses (TD section 6.7), so a motor current spike cannot trip it alone.
class BatteryAverage {
public:
    static constexpr std::size_t kSamples = 50;

    void add(float volts)
    {
        samples_[next_] = volts;
        next_ = (next_ + 1) % kSamples;
        if (count_ < kSamples) {
            ++count_;
        }
    }

    // Average of the readings so far (0 before the first one). Summed afresh
    // on each call, so float error cannot accumulate over hours.
    float average() const
    {
        if (count_ == 0) {
            return 0.0f;
        }
        float sum = 0.0f;
        for (std::size_t i = 0; i < count_; ++i) {
            sum += samples_[i];
        }
        return sum / static_cast<float>(count_);
    }

    // True once a full window has been collected; the interlock should not act
    // on a partial window right after boot.
    bool full() const { return count_ == kSamples; }
    std::size_t count() const { return count_; }

private:
    float samples_[kSamples] = {};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

}  // namespace direcsun::drivers
