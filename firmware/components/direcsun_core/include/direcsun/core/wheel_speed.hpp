#pragma once

#include <cstddef>
#include <cstdint>

namespace direcsun {

// Wheel speed from Hall edge timestamps (FD sections 10.4 and 11.2).
//
// Feed every Hall edge, oldest first, with add_edge(). The class keeps the last
// accepted edge as its reference and turns the period between accepted edges
// into two estimates:
//
//   speed_fast_rpm()  latest single period. For the wheel-speed loop, where the
//                     delay of a median would cost too much phase (FD finding V-7).
//   speed_safe_rpm()  median of the last 4 accepted periods. For the interlocks
//                     (OVERSPEED, UNDERSPEED) and telemetry, because one bad edge
//                     cannot move it.
//
// Plausibility: a period shorter than min_period_us (3 ms, 10000 rpm with two
// magnets) is a glitch, such as contact bounce or noise. The edge is ignored and
// the reference stays where it was, so the next real edge is still measured from
// the last good one. A period longer than max_period_us (200 ms, 150 rpm) is not
// used as a speed either: the edge becomes the new reference (it is a real edge,
// typically the first one after the wheel was stopped), no period is recorded,
// and the stored periods are cleared because they describe the wheel before the
// gap. Both cases count in glitch_count(), as the FD specifies.
//
// Staleness: if no edge has been accepted for more than twice the last accepted
// period, the wheel must be slower than one edge per elapsed time, so both
// estimates are replaced by that bound, edges_per_rev turns per elapsed time
// converted to rpm. The bound keeps falling as time passes, so a wheel that
// stops, or a Hall wire that falls off, reads as slowing rather than holding its
// last speed.
//
// Units: timestamps in microseconds (esp_timer_get_time() on the target), speeds
// in rpm in the wheel's forward direction (always >= 0). No clock is read here:
// every query takes "now", so the class is pure and host-testable.
//
// Not thread-safe. One owner (the control task) calls everything.
struct WheelSpeedConfig {
    int64_t min_period_us = 3000;    // shorter periods are glitches (10000 rpm at 2 edges/rev)
    int64_t max_period_us = 200000;  // longer periods are not speeds (150 rpm at 2 edges/rev)
    int edges_per_rev = 2;           // two magnets, both south-out (TD section 4.6)
};

enum class EdgeResult {
    kFirst,     // first accepted edge since construction or reset(); no period yet
    kPeriod,    // accepted; the period was recorded
    kTooShort,  // rejected as a glitch; the reference edge is unchanged
    kTooLong,   // accepted as the new reference; no period recorded, stored periods cleared
};

class WheelSpeed {
public:
    static constexpr std::size_t kMedianWindow = 4;

    explicit WheelSpeed(const WheelSpeedConfig &config = WheelSpeedConfig{});

    // Adds one edge. Timestamps must not go backwards; an edge at or before the
    // reference edge is treated as too short.
    EdgeResult add_edge(int64_t t_us);

    // Latest-period estimate in rpm, staleness-bounded. 0 until the first period.
    float speed_fast_rpm(int64_t now_us) const;

    // Median of the last 4 periods in rpm (of the 1 to 3 available before that;
    // with an even count, the mean of the two middle periods), staleness-bounded.
    // 0 until the first period.
    float speed_safe_rpm(int64_t now_us) const;

    // Microseconds since the last accepted edge, or -1 if no edge has been
    // accepted yet. Glitches do not reset it, so a noisy disconnected Hall input
    // still looks silent to HALL_SILENT.
    int64_t last_edge_age_us(int64_t now_us) const;

    // True when the staleness bound is currently replacing the estimates.
    bool stale(int64_t now_us) const;

    uint32_t edge_count() const { return edges_; }      // accepted edges
    uint32_t glitch_count() const { return glitches_; }  // rejected periods, short or long
    std::size_t period_count() const { return count_; }  // periods in the median window (0 to 4)

    // Converts a period between consecutive edges to rpm for this configuration.
    float rpm_from_period_us(float period_us) const;

    void reset();

private:
    float bounded(float estimate_rpm, int64_t now_us) const;

    WheelSpeedConfig config_;
    bool have_reference_ = false;
    int64_t reference_us_ = 0;
    int64_t periods_[kMedianWindow] = {};  // ring of accepted periods
    std::size_t next_ = 0;                 // ring write position
    std::size_t count_ = 0;                // valid periods in the ring
    int64_t last_period_us_ = 0;
    uint32_t edges_ = 0;
    uint32_t glitches_ = 0;
};

}  // namespace direcsun
