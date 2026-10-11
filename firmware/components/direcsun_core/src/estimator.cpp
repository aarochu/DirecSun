// Mutation-test switch (host selftest builds only): NO_VIB_MEAN leaves the accelerometer mean in accel_rms_g.
#if defined(ESP_PLATFORM) && defined(DIRECSUN_MUTANT_NO_VIB_MEAN)
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif
#include "direcsun/core/estimator.hpp"

#include <cmath>

#include "direcsun/core/angles.hpp"

#if defined(ESP_PLATFORM) && (defined(DIRECSUN_MUTANT_NO_STILL_GATE) || defined(DIRECSUN_MUTANT_ABSOLUTE_STILLNESS) || \
                              defined(DIRECSUN_MUTANT_RAW_RATE_HEADING))
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

namespace {

constexpr double kDegPerRad = 180.0 / 3.14159265358979323846;
constexpr uint32_t kMagStepTicks = 10;        // 20 ms
constexpr uint32_t kMagActiveTicks = 250;     // a step within the last 0.5 s keeps the heading absolute
constexpr float kMagGain = 0.01f;             // 0.02 s / tau of 2 s (FD section 11.3)

}  // namespace

Estimator::Estimator(const EstimatorConfig &config) : config_(config) {}

void mag_config_from_params(const Params &params, MagConfig &cfg)
{
    cfg.calibrated = params.mag_cal_set;
    for (int i = 0; i < 3; ++i) {
        cfg.axis[i] = params.mag_axis[i];
        cfg.sign[i] = params.mag_sign[i];
        cfg.offset[i] = params.mag_offset[i];
    }
    for (int i = 0; i < 4; ++i) cfg.soft[i] = params.mag_soft[i];
}

bool mag_heading_deg(const MagConfig &cfg, const int16_t raw[3], float &heading_deg)
{
    float box[3];
    for (int i = 0; i < 3; ++i) box[i] = static_cast<float>(cfg.sign[i]) * static_cast<float>(raw[cfg.axis[i]]) - cfg.offset[i];
    const float hx = cfg.soft[0] * box[0] + cfg.soft[1] * box[1];
    const float hy = cfg.soft[2] * box[0] + cfg.soft[3] * box[1];
    if (!(hx * hx + hy * hy > 1.0f)) return false;   // no horizontal field to read
    // The field is fixed in the world, so as the box turns counter-clockwise by psi it
    // rotates the other way in the box frame: north is (cos psi, -sin psi).
    heading_deg = static_cast<float>(std::atan2(-hy, hx) * kDegPerRad);
    return true;
}

void Estimator::set_mag_config(const MagConfig &cfg)
{
    mag_ = cfg;
    mag_samples_ = 0;
    for (float &v : mag_sum_) v = 0.0f;
    if (!cfg.calibrated) {
        mag_active_ticks_ = 0;
        mag_initialised_ = false;
    }
}

void Estimator::seed_bias(float bias_dps)
{
    bias_dps_ = bias_dps;
}

void Estimator::update(const EstimatorInputs &in)
{
    vib_x_[vib_next_] = in.accel_g[0];
    vib_y_[vib_next_] = in.accel_g[1];
    vib_next_ = (vib_next_ + 1) % kVibrationWindow;
    if (vib_count_ < kVibrationWindow) ++vib_count_;

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

    // Complementary filter (FD section 11.3): the gyro integrates every tick; every 20 ms the
    // averaged magnetometer samples pull the heading toward the compass with k = 0.02 s / 2 s.
    if (mag_.calibrated) {
        if (in.mag_new) {
            for (int i = 0; i < 3; ++i) mag_sum_[i] += static_cast<float>(in.mag_raw[i]);
            ++mag_samples_;
        }
        if (++mag_tick_ >= kMagStepTicks) {
            mag_tick_ = 0;
            if (mag_samples_ > 0) {
                int16_t avg[3];
                for (int i = 0; i < 3; ++i) avg[i] = static_cast<int16_t>(std::lround(mag_sum_[i] / static_cast<float>(mag_samples_)));
                float psi_mag = 0.0f;
                if (direcsun::mag_heading_deg(mag_, avg, psi_mag)) {
                    mag_heading_deg_ = psi_mag;
                    if (!mag_initialised_) {
                        heading_deg_ = psi_mag;   // the first fix sets the absolute heading
                        mag_initialised_ = true;
                    } else {
                        heading_deg_ += static_cast<double>(kMagGain) * static_cast<double>(wrap_deg180(psi_mag - heading_deg_));
                    }
                    mag_active_ticks_ = kMagActiveTicks;
                }
            }
            mag_samples_ = 0;
            for (float &v : mag_sum_) v = 0.0f;
        }
        if (mag_active_ticks_ > 0) --mag_active_ticks_;
    }
}

float Estimator::accel_rms_g() const
{
    if (vib_count_ < 2) return 0.0f;
    double sx = 0.0, sy = 0.0, qx = 0.0, qy = 0.0;
    for (std::size_t i = 0; i < vib_count_; ++i) {
        sx += vib_x_[i];
        sy += vib_y_[i];
        qx += static_cast<double>(vib_x_[i]) * vib_x_[i];
        qy += static_cast<double>(vib_y_[i]) * vib_y_[i];
    }
    const double n = static_cast<double>(vib_count_);
#ifdef DIRECSUN_MUTANT_NO_VIB_MEAN
    const double var = qx / n + qy / n;   // gravity and mount tilt left in
#else
    const double var = (qx - sx * sx / n) / n + (qy - sy * sy / n) / n;
#endif
    return var > 0.0 ? static_cast<float>(std::sqrt(var)) : 0.0f;
}

float Estimator::heading_deg() const
{
    return wrap_deg180(heading_deg_);
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
