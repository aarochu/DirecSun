#include "direcsun/core/estimator.hpp"

#include <cmath>

#include "direcsun/core/angles.hpp"

#if defined(ESP_PLATFORM) && (defined(DIRECSUN_MUTANT_NO_STILL_GATE) || defined(DIRECSUN_MUTANT_ABSOLUTE_STILLNESS) || \
                              defined(DIRECSUN_MUTANT_RAW_RATE_HEADING) || defined(DIRECSUN_MUTANT_MAG_SNAP_ARMED) || \
                              defined(DIRECSUN_MUTANT_MAG_SIGN_FLIP) || defined(DIRECSUN_MUTANT_RMS_KEEPS_DC))
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

namespace {

constexpr double kDegPerRad = 180.0 / 3.14159265358979323846;
constexpr float kDegPerRadF = 57.2957795130823208768f;

}  // namespace

Estimator::Estimator(const EstimatorConfig &config) : config_(config) {}

void Estimator::seed_bias(float bias_dps)
{
    bias_dps_ = bias_dps;
}

void Estimator::update(const EstimatorInputs &in)
{
    if (mag_ticks_since_ < UINT32_MAX) ++mag_ticks_since_;
    // 20 ms moving average of the accelerometer (FD 11.5).
    for (int axis = 0; axis < 3; ++axis) {
        accel_window_[accel_next_][axis] = in.accel_g[axis];
    }
    accel_next_ = (accel_next_ + 1) % kAccelWindow;
    if (accel_count_ < kAccelWindow) {
        ++accel_count_;
    }
    float avg[3] = {0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < accel_count_; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            avg[axis] += accel_window_[i][axis];
        }
    }
    for (float &a : avg) {
        a /= static_cast<float>(accel_count_);
    }
    accel_norm_g_ = std::sqrt(avg[0] * avg[0] + avg[1] * avg[1] + avg[2] * avg[2]);
    if (accel_norm_g_ < config_.min_accel_norm_g) {
        // Falling or lifted with no usable gravity vector: report a tilt that
        // trips the TILT interlock (more than 30 deg) rather than a guess.
        tilt_deg_ = 90.0f;
    } else {
        float c = avg[2] / accel_norm_g_;
        c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        tilt_deg_ = static_cast<float>(std::acos(c) * kDegPerRad);
    }

    // Bias: slow exponential average, only in SAFE and only while still (FD 11.1,
    // finding V-10). The gate uses the deviation from the current bias, so a
    // spinning box (as after an e-stop) never pulls the bias toward its rate.
    const bool rate_still = std::fabs(in.gyro_z_dps - bias_dps_) < config_.still_rate_dps;
    // Relative to the boot seed's reference norm, not to 1 g (FD 11.1 as
    // amended): an uncalibrated accelerometer reads off 1 g at rest.
#ifdef DIRECSUN_MUTANT_ABSOLUTE_STILLNESS
    const float accel_ref = 1.0f;
#else
    const float accel_ref = accel_ref_g_;
#endif
    const bool accel_still = std::fabs(accel_norm_g_ - accel_ref) < config_.still_accel_tol_g;
    const bool wheel_still = !in.wheel_known || in.wheel_rpm < config_.still_wheel_rpm;
#ifdef DIRECSUN_MUTANT_NO_STILL_GATE
    bias_updating_ = in.safe_mode;
    (void)rate_still;
    (void)accel_still;
    (void)wheel_still;
#else
    bias_updating_ = in.safe_mode && rate_still && accel_still && wheel_still;
#endif
    if (bias_updating_) {
        const float alpha = config_.dt_s / config_.bias_tau_s;
        bias_dps_ += alpha * (in.gyro_z_dps - bias_dps_);
    }

    const float sign = in.s_gyro < 0 ? -1.0f : 1.0f;
    rate_dps_ = sign * (in.gyro_z_dps - bias_dps_);
#ifdef DIRECSUN_MUTANT_RAW_RATE_HEADING
    // Positive control (R4.2): integrate the raw gyro, bias included.
    heading_deg_ += static_cast<double>(sign * in.gyro_z_dps) * config_.dt_s;
#else
    heading_deg_ += static_cast<double>(rate_dps_) * config_.dt_s;
#endif
}

float Estimator::heading_deg() const
{
    return wrap_deg180(heading_deg_);
}

void Estimator::set_mag_calibration(const MagCalibration &calibration)
{
    mag_cal_ = calibration;
    mag_started_ = false;
    mag_have_heading_ = false;
    mag_ticks_since_ = UINT32_MAX;
}

void Estimator::update_mag(const float raw[3], bool safe_mode)
{
    if (!mag_cal_.valid) return;
    float box[3];
    for (int i = 0; i < 3; ++i) {
        const int a = mag_cal_.axis[i];
        const int index = (a < 0 ? -a : a) - 1;
        if (index < 0 || index > 2) return;
        const float v = raw[index] - mag_cal_.offset[index];
        box[i] = a < 0 ? -v : v;
    }
    const float hx = mag_cal_.soft[0] * box[0] + mag_cal_.soft[1] * box[1];
    const float hy = mag_cal_.soft[2] * box[0] + mag_cal_.soft[3] * box[1];
    if (!(std::sqrt(hx * hx + hy * hy) >= config_.mag_min_field)) return;  // also rejects NaN

    // The field is fixed in the world, so a counter-clockwise turn of the box
    // rotates it clockwise in the box frame: the heading is minus the field's
    // bearing in the box frame, which makes a counter-clockwise turn increase it
    // (FD section 11.3). Single precision (the S3's FPU) on purpose: this runs in
    // the worst tick of the 2 ms budget (FD section 9.2), and a double atan2 or
    // remainder would cost it tens of microseconds in software.
#ifdef DIRECSUN_MUTANT_MAG_SIGN_FLIP
    float psi_mag = std::atan2(hy, hx) * kDegPerRadF;
#else
    float psi_mag = -std::atan2(hy, hx) * kDegPerRadF;
#endif
    if (psi_mag <= -180.0f) psi_mag += 360.0f;  // atan2 gives [-pi, pi]: put the half-turn on +180
    wrap_mag_heading_deg_ = psi_mag;
    mag_have_heading_ = true;
    mag_ticks_since_ = 0;
    if (!mag_started_) {
#ifndef DIRECSUN_MUTANT_MAG_SNAP_ARMED
        if (!safe_mode) return;  // never jump the heading while armed
#else
        (void)safe_mode;
#endif
        heading_deg_ = static_cast<double>(psi_mag);
        mag_started_ = true;
        return;
    }
    // The error onto (-180, 180]: ψ̂ is unwrapped (it may hold many turns), so reduce
    // the difference by whole turns. The floor is a double operation, once per sample.
    double error = static_cast<double>(psi_mag) - heading_deg_;
    error -= 360.0 * std::floor(error / 360.0 + 0.5);
    heading_deg_ += static_cast<double>(config_.mag_dt_s / config_.mag_tau_s) * error;
}

void GyroBiasSeed::add(float gyro_z_dps, const float accel_g[3])
{
    const float norm = std::sqrt(accel_g[0] * accel_g[0] + accel_g[1] * accel_g[1] + accel_g[2] * accel_g[2]);
    if (count_ == 0) {
        min_ = gyro_z_dps;
        max_ = gyro_z_dps;
        norm_min_ = norm;
        norm_max_ = norm;
    } else {
        min_ = gyro_z_dps < min_ ? gyro_z_dps : min_;
        max_ = gyro_z_dps > max_ ? gyro_z_dps : max_;
        norm_min_ = norm < norm_min_ ? norm : norm_min_;
        norm_max_ = norm > norm_max_ ? norm : norm_max_;
    }
    sum_ += gyro_z_dps;
    norm_sum_ += norm;
    ++count_;
}

const char *GyroBiasSeed::failure() const
{
    if (count_ < config_.min_samples) {
        return "too few samples";
    }
    if ((max_ - min_) > config_.max_range_dps) {
        return "gyro range too wide (the box turned)";
    }
    const float mean = static_cast<float>(sum_ / static_cast<double>(count_));
    if (std::fabs(mean) > config_.max_abs_mean_dps) {
        return "gyro mean beyond the sensor's offset tolerance (the box was spinning)";
    }
#ifdef DIRECSUN_MUTANT_ABSOLUTE_STILLNESS
    // The pre-amendment rule: every norm within the tolerance of exactly 1 g.
    if (norm_max_ - 1.0f >= config_.accel_tol_g || 1.0f - norm_min_ >= config_.accel_tol_g) {
        return "accelerometer norm off 1 g";
    }
#else
    const float ref = reference_norm_g();
    if (ref < config_.min_ref_norm_g || ref > config_.max_ref_norm_g) {
        return "accelerometer norm implausible for gravity";
    }
    if (norm_max_ - ref >= config_.accel_tol_g || ref - norm_min_ >= config_.accel_tol_g) {
        return "accelerometer norm varied (the box moved)";
    }
#endif
    return nullptr;
}

bool GyroBiasSeed::result(float &bias_dps) const
{
    if (failure() != nullptr) {
        return false;
    }
    bias_dps = static_cast<float>(sum_ / static_cast<double>(count_));
    return true;
}

}  // namespace direcsun

namespace direcsun {

void AccelRms::add(const float accel_g[3])
{
    for (int axis = 0; axis < 3; ++axis) window_[next_][axis] = accel_g[axis];
    next_ = (next_ + 1) % kWindow;
    if (count_ < kWindow) ++count_;
}

float AccelRms::rms_g() const
{
    if (count_ < 2) return 0.0f;
    float sum_sq = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float mean = 0.0f;
        for (std::size_t i = 0; i < count_; ++i) mean += window_[i][axis];
        mean /= static_cast<float>(count_);
#ifdef DIRECSUN_MUTANT_RMS_KEEPS_DC
        mean = 0.0f;
#endif
        for (std::size_t i = 0; i < count_; ++i) {
            const float d = window_[i][axis] - mean;
            sum_sq += d * d;
        }
    }
    return std::sqrt(sum_sq / static_cast<float>(count_));
}

}  // namespace direcsun
