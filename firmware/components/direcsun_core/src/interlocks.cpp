#include "direcsun/core/interlocks.hpp"

namespace direcsun {

const char *interlock_wire_name(Interlock i)
{
    switch (i) {
    case Interlock::Estop: return "ESTOP";
    case Interlock::HeartbeatLost: return "HEARTBEAT_LOST";
    case Interlock::Overspeed: return "OVERSPEED";
    case Interlock::Underspeed: return "UNDERSPEED";
    case Interlock::BatteryLow: return "BATTERY_LOW";
    case Interlock::ImuSilent: return "IMU_SILENT";
    case Interlock::Tilt: return "TILT";
    case Interlock::ControlStall: return "CONTROL_STALL";
    case Interlock::HallSilent: return "HALL_SILENT";
    case Interlock::Count: break;
    }
    return "UNKNOWN";
}

InterlockSet InterlockMonitor::evaluate(const FaultInputs &in, const Params &params)
{
    InterlockSet faults = 0;

    // Wheel-speed interlocks need the Hall sensor. With the bench bypass they
    // are suspended, and kBypassThrottleMax is the only overspeed protection.
    if (in.hall_installed && in.wheel_rpm_safe > params.omega_cap_rpm + kOverspeedMarginRpm) {
        faults |= interlock_bit(Interlock::Overspeed);
    }

    // A tick that missed alarms saw the condition only now; it is counted for
    // the whole interval since the previous tick, so a trip comes early, never
    // late (V-66).
    if (in.hall_installed && in.underspeed_applies && in.wheel_rpm_safe < params.omega_low_rpm) {
        underspeed_ticks_ += in.elapsed_ticks;
    } else {
        underspeed_ticks_ = 0;
    }
    if (underspeed_ticks_ >= ticks_from_ms(kUnderspeedMs)) faults |= interlock_bit(Interlock::Underspeed);

    if (in.vbat_avg_v < kBatteryLowV) faults |= interlock_bit(Interlock::BatteryLow);
    if (in.imu_invalid_streak >= kImuSilentReads) faults |= interlock_bit(Interlock::ImuSilent);
    if (in.tilt_deg > kTiltMaxDeg) faults |= interlock_bit(Interlock::Tilt);
    if (in.missed_tick_streak >= kControlStallTicks) faults |= interlock_bit(Interlock::ControlStall);

    // HALL_SILENT (finding V-11): the throttle has been high enough for the
    // wheel to turn above the low limit for 500 ms, and no edge arrived.
    if (in.hall_installed && in.throttle > params.hall_expect_throttle) {
        throttle_high_ticks_ += in.elapsed_ticks;
    } else {
        throttle_high_ticks_ = 0;
    }
    if (throttle_high_ticks_ >= ticks_from_ms(kHallSilentMs) && in.hall_edge_age_ms >= kHallSilentMs) {
        faults |= interlock_bit(Interlock::HallSilent);
    }

    return faults;
}

void InterlockMonitor::reset()
{
    underspeed_ticks_ = 0;
    throttle_high_ticks_ = 0;
}

bool latch_releasable(Interlock i, const ReleaseInputs &in, const Params &params)
{
    switch (i) {
    case Interlock::Overspeed: return in.wheel_rpm_safe < params.omega_bias_rpm;
    case Interlock::BatteryLow: return in.vbat_avg_v > kArmMinBatteryV;
    case Interlock::ImuSilent: return in.imu_invalid_streak == 0;
    case Interlock::Tilt: return in.tilt_deg <= kTiltMaxDeg;
    case Interlock::ControlStall: return false;
    case Interlock::Estop:
    case Interlock::HeartbeatLost:
    case Interlock::Underspeed:
    case Interlock::HallSilent: return true;
    case Interlock::Count: break;
    }
    return false;
}

void RampDown::start(int throttle0, uint32_t tick)
{
    active_ = true;
    throttle0_ = throttle0 < 0 ? 0 : throttle0;
    start_tick_ = tick;
}

int RampDown::value(uint32_t tick)
{
    if (!active_) return 0;
    const uint32_t total = ticks_from_ms(kRampDownMs);
    const uint32_t elapsed = ticks_since(tick, start_tick_);
    if (elapsed >= total) {
        active_ = false;
        return 0;
    }
    const long v = static_cast<long>(throttle0_) * static_cast<long>(total - elapsed) / static_cast<long>(total);
    return v < kDshotMinThrottle ? 0 : static_cast<int>(v);
}

}  // namespace direcsun
