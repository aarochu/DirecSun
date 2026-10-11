#pragma once

// Estimator (FD sections 11.1, 11.3 and 11.5): box rate with a stillness-gated
// gyro bias, a heading that integrates the gyro and, once the magnetometer is
// calibrated (R5.1), is pulled toward the compass, and tilt from the averaged
// accelerometer. The sun angle (11.4) is in sun.hpp.
//
// Pure: the control task feeds one IMU sample per 2 ms tick and reads the
// results. Units: deg/s, deg, g. The gyro bias is kept in the sensor frame;
// s_gyro (from the parameters) maps sensor +z to box +z (FD section 2.3).

#include <cstddef>
#include <cstdint>

#include "direcsun/core/params.hpp"

namespace direcsun {

struct EstimatorConfig {
    float dt_s = 0.002f;               // one control tick
    float bias_tau_s = 5.0f;           // FD 11.1: slow exponential average
    float still_rate_dps = 2.0f;       // FD 11.1: |g_z - b_z| below this
    float still_accel_tol_g = 0.05f;   // FD 11.1 as amended: | |a| - reference | below this
    float still_wheel_rpm = 200.0f;    // FD 11.1: wheel below this, or not reported
    float min_accel_norm_g = 0.2f;     // below this the box is falling: tilt reads 90 deg
    float mag_dt_s = 0.02f;            // FD 11.3: one magnetometer step every 20 ms
    float mag_tau_s = 2.0f;            // FD 11.3: complementary filter time constant [A, tune in T-9]
    float mag_min_field = 1.0f;        // below this horizontal magnitude (counts) there is no heading
    uint32_t mag_stale_ticks = 250;    // no magnetometer step for 0.5 s: stop claiming fusion
};

// Magnetometer calibration (FD sections 10.2 and 11.3), taken from Params. The
// hard-iron offset is in the sensor's own axes; `axis` maps box X, Y, Z to
// signed sensor axes (+-1 x, +-2 y, +-3 z); `soft` is the horizontal soft-iron
// matrix, row-major, applied to the box-frame (X, Y) after the offset.
struct MagCalibration {
    bool valid = false;
    int8_t axis[3] = {1, -2, -3};
    float offset[3] = {0.0f, 0.0f, 0.0f};
    float soft[4] = {1.0f, 0.0f, 0.0f, 1.0f};
};

// The calibration the parameters describe (FD section 16).
inline MagCalibration mag_calibration_from(const Params &params)
{
    MagCalibration cal;
    cal.valid = params.mag_cal_valid;
    for (int i = 0; i < 3; ++i) {
        cal.axis[i] = params.mag_axis[i];
        cal.offset[i] = params.mag_offset[i];
    }
    for (int i = 0; i < 4; ++i) cal.soft[i] = params.mag_soft[i];
    return cal;
}

struct EstimatorInputs {
    float gyro_z_dps = 0.0f;           // raw sensor z rate, deg/s
    float accel_g[3] = {0.0f, 0.0f, 0.0f};
    bool safe_mode = false;            // FD 11.1: the bias learns only in SAFE
    bool wheel_known = false;          // false when no Hall speed is available
    float wheel_rpm = 0.0f;
    int8_t s_gyro = 1;                 // +1 or -1
};

class Estimator {
public:
    // 20 ms of accelerometer samples at 500 Hz (FD 11.5).
    static constexpr std::size_t kAccelWindow = 10;

    explicit Estimator(const EstimatorConfig &config = EstimatorConfig{});

    // Starts the bias at a value measured while the box was known to be still
    // (GyroBiasSeed). Without a seed the bias starts at 0.
    void seed_bias(float bias_dps);

    // The accelerometer norm that counts as "still" for the bias gate [D]: the
    // boot seed's own mean norm (GyroBiasSeed::reference_norm_g). An
    // uncalibrated MPU-6050 can read well off 1 g at rest (0.871 g on the
    // XIAO bench, Oct 10), so FD 11.1's absolute 1 g test would never open
    // the gate. 1 g until set.
    void set_accel_reference(float norm_g) { accel_ref_g_ = norm_g; }
    float accel_reference_g() const { return accel_ref_g_; }

    // One valid IMU sample. Invalid samples are not fed: every output holds
    // its last value and the supervisor counts the streak (IMU_SILENT).
    void update(const EstimatorInputs &in);

    float rate_dps() const { return rate_dps_; }      // box rate, CCW positive
    float bias_dps() const { return bias_dps_; }      // sensor-frame z bias
    bool bias_updating() const { return bias_updating_; }  // the gate was open on the last update
    float tilt_deg() const { return tilt_deg_; }      // from vertical, 0..180
    float accel_norm_g() const { return accel_norm_g_; }

    // Heading (R4.2, R5.1, FD section 11.3): the bias-corrected box rate
    // integrated every tick, counter-clockwise positive, kept continuous
    // (unwrapped) as double, pulled toward the magnetometer heading by the
    // complementary filter once the magnetometer is calibrated and fusing. It
    // is never reset: `zero_heading` is an offset the supervisor keeps, so one
    // tick can zero the heading and start a HOLD or SLEW against the same
    // reference.
    double heading_unwrapped_deg() const { return heading_deg_; }
    // The same, wrapped to (-180, 180] for display.
    float heading_deg() const;

    // The calibration in force. Changing it drops the fusion state: the next
    // magnetometer step in SAFE starts the heading from the compass again.
    void set_mag_calibration(const MagCalibration &calibration);
    // One fresh magnetometer sample, raw counts in sensor axes (50 Hz, FD 11.3).
    // The first step after a calibration snaps the heading to the compass, but
    // only in SAFE: a jump while armed would move the reference of a HOLD or
    // SLEW, so until then the heading stays gyro-only. After that, each step
    // pulls the heading by k = mag_dt / mag_tau of the wrapped difference.
    void update_mag(const float raw[3], bool safe_mode);
    // True while the heading includes the compass: calibrated, started, and a
    // step arrived within mag_stale_ticks. False means gyro-only (HEADING_GYRO).
    bool mag_fusing() const { return mag_started_ && mag_ticks_since_ < config_.mag_stale_ticks; }
    // The last magnetometer heading, counter-clockwise positive, absolute
    // (not relative to zero_heading); wrapped to (-180, 180]. Valid when
    // mag_heading_valid().
    // Calibrated, and a step arrived within mag_stale_ticks.
    bool mag_heading_valid() const { return mag_have_heading_ && mag_ticks_since_ < config_.mag_stale_ticks; }
    float mag_heading_deg() const { return wrap_mag_heading_deg_; }

private:
    EstimatorConfig config_;
    float bias_dps_ = 0.0f;
    float accel_ref_g_ = 1.0f;
    float rate_dps_ = 0.0f;
    double heading_deg_ = 0.0;
    bool bias_updating_ = false;

    MagCalibration mag_cal_;
    bool mag_started_ = false;       // the heading has been snapped to the compass
    bool mag_have_heading_ = false;  // a magnetometer heading exists
    float wrap_mag_heading_deg_ = 0.0f;
    uint32_t mag_ticks_since_ = UINT32_MAX;

    float accel_window_[kAccelWindow][3] = {};
    std::size_t accel_next_ = 0;
    std::size_t accel_count_ = 0;
    float accel_norm_g_ = 0.0f;
    float tilt_deg_ = 0.0f;
};

// Accelerometer vibration for the balancing procedure (T-7, TD section 4.6,
// FD section 19.2): the RMS of the AC part of the acceleration vector over the
// last 200 ms (100 samples at 500 Hz, twelve turns of a 3600 rpm wheel). Each
// axis has its own window mean removed, so gravity and any tilt drop out and
// what remains is the vibration, in g. It is a broadband figure, not the
// amplitude at the wheel frequency: compare readings taken at the same wheel
// speed, as the washer procedure does.
class AccelRms {
public:
    static constexpr std::size_t kWindow = 100;

    // One valid IMU sample, in g.
    void add(const float accel_g[3]);

    // 0 until two samples have arrived.
    float rms_g() const;

    std::size_t count() const { return count_; }

private:
    float window_[kWindow][3] = {};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

// Boot-time bias seed [D]. FD 11.1 opens the bias update only while
// |g_z - b_z| < 2 deg/s. The MPU-6050's zero-rate offset can be larger than
// that (the register map's tolerance is +-20 deg/s), so starting from b_z = 0
// the gate might never open and the offset would read as a permanent rotation.
// The boot path therefore averages the gyro over the 1 s DShot-0 window and
// seeds the bias, but only when that window proves the box was still:
//   - the gyro stayed within a small range (a hand-turned or coasting box
//     changes rate), and its mean is inside the sensor's offset tolerance;
//   - every accelerometer norm stayed within the stillness tolerance of the
//     window's own mean norm (relative, as amended: an uncalibrated
//     accelerometer reads off 1 g at rest), and that mean is plausible for
//     gravity (a dead accelerometer reading a constant near 0 g is not still).
// The mean norm becomes the estimator's accel reference (set_accel_reference).
struct GyroBiasSeedConfig {
    std::size_t min_samples = 250;     // at least 0.5 s of samples
    float max_range_dps = 0.5f;        // max - min of g_z over the window
    float max_abs_mean_dps = 25.0f;    // beyond the sensor's +-20 deg/s offset tolerance plus margin
    float accel_tol_g = 0.05f;         // each norm within this of the window's mean norm
    float min_ref_norm_g = 0.7f;       // plausible gravity for an uncalibrated MPU-6050
    float max_ref_norm_g = 1.3f;
};

class GyroBiasSeed {
public:
    explicit GyroBiasSeed(const GyroBiasSeedConfig &config = GyroBiasSeedConfig{}) : config_(config) {}

    void add(float gyro_z_dps, const float accel_g[3]);

    // True and the mean in `bias_dps` if the window qualifies; false otherwise.
    bool result(float &bias_dps) const;

    // Why result() refuses, for the boot log: nullptr when the window qualifies.
    const char *failure() const;

    std::size_t samples() const { return count_; }
    float range_dps() const { return count_ == 0 ? 0.0f : max_ - min_; }
    // Mean accelerometer norm over the window (0 before the first sample).
    float reference_norm_g() const { return count_ == 0 ? 0.0f : static_cast<float>(norm_sum_ / static_cast<double>(count_)); }
    float norm_min_g() const { return norm_min_; }
    float norm_max_g() const { return norm_max_; }

private:
    GyroBiasSeedConfig config_;
    std::size_t count_ = 0;
    double sum_ = 0.0;
    float min_ = 0.0f;
    float max_ = 0.0f;
    double norm_sum_ = 0.0;
    float norm_min_ = 0.0f;
    float norm_max_ = 0.0f;
};

}  // namespace direcsun
