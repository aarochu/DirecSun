#pragma once

// Estimator subset for Set 3 (FD sections 11.1 and 11.5): box rate with a
// stillness-gated gyro bias, a gyro-only heading, and tilt from the averaged
// accelerometer. Magnetometer fusion (FD section 11.3) and the sun angle
// (11.4) arrive in Set 5.
//
// Pure: the control task feeds one IMU sample per 2 ms tick and reads the
// results. Units: deg/s, deg, g. The gyro bias is kept in the sensor frame;
// s_gyro (from the parameters) maps sensor +z to box +z (FD section 2.3).

#include <cstddef>
#include <cstdint>

namespace direcsun {

struct EstimatorConfig {
    float dt_s = 0.002f;               // one control tick
    float bias_tau_s = 5.0f;           // FD 11.1: slow exponential average
    float still_rate_dps = 2.0f;       // FD 11.1: |g_z - b_z| below this
    float still_accel_tol_g = 0.05f;   // FD 11.1 as amended: | |a| - reference | below this
    float still_wheel_rpm = 200.0f;    // FD 11.1: wheel below this, or not reported
    float min_accel_norm_g = 0.2f;     // below this the box is falling: tilt reads 90 deg
};

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

    // Gyro-only heading integrated from the box rate [D]: Set 5 adds the
    // magnetometer. Kept continuous (unwrapped) for control; wrapped to
    // (-180, 180] for display.
    double heading_unwrapped_deg() const { return heading_deg_; }
    float heading_deg() const;

    // Adds an offset so the wrapped heading reads 0 now (`zero_heading`).
    void zero_heading() { heading_deg_ = 0.0; }

private:
    EstimatorConfig config_;
    float bias_dps_ = 0.0f;
    float accel_ref_g_ = 1.0f;
    float rate_dps_ = 0.0f;
    double heading_deg_ = 0.0;
    bool bias_updating_ = false;

    float accel_window_[kAccelWindow][3] = {};
    std::size_t accel_next_ = 0;
    std::size_t accel_count_ = 0;
    float accel_norm_g_ = 0.0f;
    float tilt_deg_ = 0.0f;
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
