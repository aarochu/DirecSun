#include "direcsun/core/wheel_speed.hpp"

#include <algorithm>

namespace direcsun {

WheelSpeed::WheelSpeed(const WheelSpeedConfig &config) : config_(config) {}

EdgeResult WheelSpeed::add_edge(int64_t t_us)
{
    if (!have_reference_) {
        have_reference_ = true;
        reference_us_ = t_us;
        ++edges_;
        return EdgeResult::kFirst;
    }

    const int64_t period = t_us - reference_us_;
    if (period < config_.min_period_us) {
        ++glitches_;
        return EdgeResult::kTooShort;
    }

    reference_us_ = t_us;
    ++edges_;
    if (period > config_.max_period_us) {
        // The wheel was slower than 150 rpm (or stopped) across this gap, so the
        // stored periods no longer describe it. Forget them: reporting the old
        // speed right after a restart edge would be wrong in the unsafe direction.
        ++glitches_;
        count_ = 0;
        next_ = 0;
        last_period_us_ = 0;
        return EdgeResult::kTooLong;
    }

    periods_[next_] = period;
    next_ = (next_ + 1) % kMedianWindow;
    if (count_ < kMedianWindow) {
        ++count_;
    }
    last_period_us_ = period;
    return EdgeResult::kPeriod;
}

float WheelSpeed::rpm_from_period_us(float period_us) const
{
    // One period spans 1 / edges_per_rev of a turn: rpm = 60e6 / (edges * period).
    return 60.0e6f / (static_cast<float>(config_.edges_per_rev) * period_us);
}

bool WheelSpeed::stale(int64_t now_us) const
{
    return have_reference_ && count_ > 0 && (now_us - reference_us_) > 2 * last_period_us_;
}

float WheelSpeed::bounded(float estimate_rpm, int64_t now_us) const
{
    if (!stale(now_us)) {
        return estimate_rpm;
    }
    // More than two periods without an edge: the wheel takes longer than the
    // elapsed time per edge, so its speed is below one edge per elapsed time.
    const float bound = rpm_from_period_us(static_cast<float>(now_us - reference_us_));
    return std::min(estimate_rpm, bound);
}

float WheelSpeed::speed_fast_rpm(int64_t now_us) const
{
    if (count_ == 0) {
        return 0.0f;
    }
    return bounded(rpm_from_period_us(static_cast<float>(last_period_us_)), now_us);
}

float WheelSpeed::speed_safe_rpm(int64_t now_us) const
{
    if (count_ == 0) {
        return 0.0f;
    }
    // Insertion sort of at most four values. (std::sort here trips a GCC 15
    // -Warray-bounds false positive at -O2 on its 16-element threshold path.)
    int64_t sorted[kMedianWindow];
    for (std::size_t i = 0; i < count_; ++i) {
        const int64_t value = periods_[i];
        std::size_t j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = value;
    }
    float median;
    if (count_ % 2 == 1) {
        median = static_cast<float>(sorted[count_ / 2]);
    } else {
        median = 0.5f * static_cast<float>(sorted[count_ / 2 - 1] + sorted[count_ / 2]);
    }
    return bounded(rpm_from_period_us(median), now_us);
}

int64_t WheelSpeed::last_edge_age_us(int64_t now_us) const
{
    return have_reference_ ? now_us - reference_us_ : -1;
}

void WheelSpeed::reset()
{
    *this = WheelSpeed(config_);
}

}  // namespace direcsun
