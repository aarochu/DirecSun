#pragma once

// Sun angle from the two OP550 sensors (FD section 11.4, R5.4).
//
//   d = (A - B) / (A + B)        valid only when A + B exceeds light_threshold
//   sun = calibration(d)         piecewise-linear table from T-10
//
// The angle is degrees counter-clockwise positive, the box's internal frame
// (FD section 2.3); the wire flips the sign. `valid` is false without a
// calibration table, below the light threshold, or when d falls outside the
// calibrated range (no extrapolation: the table is only known where T-10
// measured it).
//
// Pure: the control task feeds the two ADC readings (about 50 Hz) and reads the
// result.

#include "direcsun/core/params.hpp"

namespace direcsun {

struct SunReading {
    bool valid = false;
    float d = 0.0f;          // normalised difference, meaningful when the sum is non-zero
    float angle_deg = 0.0f;  // counter-clockwise positive; 0 when not valid
};

// Piecewise-linear lookup of the calibration table. `valid` is false when the
// table has fewer than two points or d lies outside [d_first, d_last].
SunReading sun_angle_from_d(float d, const Params &params);

// Smooths A and B with a first-order filter, then applies the threshold and the
// table. The filter makes one noisy ADC sample unable to flip validity.
class SunSensor {
public:
    // Smoothing time constant, seconds [D]; with samples at 50 Hz the weight of
    // each new sample is dt / (tau + dt).
    static constexpr float kTauS = 0.1f;

    // One pair of readings in millivolts, `dt_s` after the previous pair.
    void update(float a_mv, float b_mv, float dt_s, const Params &params);

    const SunReading &reading() const { return reading_; }
    float a_mv() const { return a_mv_; }
    float b_mv() const { return b_mv_; }

private:
    bool started_ = false;
    float a_mv_ = 0.0f;
    float b_mv_ = 0.0f;
    SunReading reading_;
};

}  // namespace direcsun
