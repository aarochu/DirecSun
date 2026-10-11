#include "direcsun/core/estimator.hpp"

#include <cmath>

#include "direcsun/core/angles.hpp"

#if defined(ESP_PLATFORM) && (defined(DIRECSUN_MUTANT_NO_STILL_GATE) || defined(DIRECSUN_MUTANT_ABSOLUTE_STILLNESS))
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

namespace {

constexpr double kRadPerDeg = 3.14159265358979323846 / 180.0;
constexpr double kDegPerRad = 180.0 / 3.14159265358979323846;

}  // namespace

Estimator::Estimator(const EstimatorConfig &config) : config_(config) {}

void Estimator::seed_bias(float bias_dps)
{
    bias_dps_ = bias_dps;
}

void Estimator::update(const EstimatorInputs &in)
{
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
    heading_deg_ += static_cast<double>(rate_dps_) * config_.dt_s;
}

float Estimator::heading_deg() const
{
    // Reduce in double first so a long-running heading keeps its precision,
    // then wrap onto (-180, 180] with the core's wrap.
    const double reduced = std::remainder(heading_deg_, 360.0);
    return static_cast<float>(wrap_pi(static_cast<float>(reduced * kRadPerDeg)) * kDegPerRad);
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
