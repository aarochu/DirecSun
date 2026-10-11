#pragma once

// Interlock table (FD section 14.1, TD section 6.7): thresholds, the hard-fault
// monitor, latch release rules, and the open-loop ramp-down (FD section 14.3).
// The supervisor (modes.hpp) owns latching, mode changes, and events.

#include <cstdint>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/dshot.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/core/timebase.hpp"

namespace direcsun {

enum class Interlock : uint8_t {
    Estop,
    HeartbeatLost,
    Overspeed,
    Underspeed,
    BatteryLow,
    ImuSilent,
    Tilt,
    ControlStall,
    HallSilent,
    Count,
};

using InterlockSet = uint16_t;

constexpr InterlockSet interlock_bit(Interlock i)
{
    return static_cast<InterlockSet>(1u << static_cast<unsigned>(i));
}

// Wire names (FD section 14.1), e.g. "OVERSPEED".
const char *interlock_wire_name(Interlock i);

// Hard faults cut the throttle to 0 in the tripping tick; the others
// (ESTOP, HEARTBEAT_LOST) start the 5 s ramp-down.
constexpr InterlockSet kHardFaults =
    interlock_bit(Interlock::Overspeed) | interlock_bit(Interlock::Underspeed) |
    interlock_bit(Interlock::BatteryLow) | interlock_bit(Interlock::ImuSilent) | interlock_bit(Interlock::Tilt) |
    interlock_bit(Interlock::ControlStall) | interlock_bit(Interlock::HallSilent);

// ---------------------------------------------------------------------------
// Thresholds. Changing one weakens or strengthens safety: FD section 21 rule 2
// requires a TD revision, and HT-8 pins each at its value.
// ---------------------------------------------------------------------------
constexpr float kOverspeedMarginRpm = 100.0f;       // trip above cap + 100 (4100 rpm)
constexpr uint32_t kUnderspeedMs = 200;             // below omega_low for 200 ms
constexpr float kBatteryLowV = 8.8f;                // 1 s average below 8.8 V (1.1 V per NiMH cell)
constexpr float kArmMinBatteryV = 9.6f;             // arm guard: above 9.6 V (1.2 V per cell)
constexpr uint32_t kImuSilentReads = 25;            // 25 consecutive invalid reads (50 ms)
constexpr float kTiltMaxDeg = 30.0f;                // more than 30 deg from vertical
constexpr uint32_t kControlStallTicks = 25;         // 25 consecutive missed ticks (50 ms)
constexpr uint32_t kHallSilentMs = 500;             // throttle high, no Hall edge, for 500 ms
constexpr uint32_t kHeartbeatTimeoutMs = 1000;      // no ping or cmd from the controlling client
constexpr float kMomentumWarnRpm = 300.0f;          // MOMENTUM_NEAR_LIMIT band
constexpr uint32_t kRampDownMs = 5000;              // e-stop, disarm, heartbeat loss

// Hall bench bypass ceiling (approved 2026-10-10). A constexpr, not a
// parameter: no command can raise it. About 17.5 % duty, estimated [A] at
// 1600 rpm no-load at 10 V; R3.7 measures it and lowers it if it exceeds
// 2000 rpm.
constexpr int kBypassThrottleMax = 400;

// The DShot throttle range comes from dshot.hpp, its one definition:
// kDshotMinThrottle (48; 1..47 are ESC commands flight code never sends) and
// kDshotMaxValue. The supervisor computes throttle in int.
constexpr int kDshotMaxThrottle = kDshotMaxValue;

// What the monitor needs each tick.
struct FaultInputs {
    uint32_t elapsed_ticks = 1;        // alarms since the previous evaluation (TickClock)
    bool hall_installed = true;
    bool underspeed_applies = false;   // mode is neither SAFE nor SPINUP
    float wheel_rpm_safe = 0.0f;       // median wheel speed (FD section 10.4)
    uint32_t hall_edge_age_ms = UINT32_MAX;
    int throttle = 0;                  // throttle about to be sent this tick
    float vbat_avg_v = 0.0f;
    float tilt_deg = 0.0f;
    uint32_t imu_invalid_streak = 0;
    uint32_t missed_tick_streak = 0;
};

// Evaluates the hard faults every tick. It keeps the two duration counters
// (UNDERSPEED, HALL_SILENT), which add the alarms each evaluation stands for,
// so a missed tick cannot stretch them; everything else is a threshold on its
// input.
class InterlockMonitor {
public:
    InterlockSet evaluate(const FaultInputs &in, const Params &params);
    void reset();

private:
    uint32_t underspeed_ticks_ = 0;
    uint32_t throttle_high_ticks_ = 0;
};

// Inputs to the latch release rules checked by `arm` (FD section 14.1).
struct ReleaseInputs {
    float wheel_rpm_safe = 0.0f;
    uint32_t imu_invalid_streak = 0;
    float tilt_deg = 0.0f;
    float vbat_avg_v = 0.0f;
};

// True if a latched interlock may be cleared by `arm` now. CONTROL_STALL never
// clears: it needs a reboot.
bool latch_releasable(Interlock i, const ReleaseInputs &in, const Params &params);

// Open-loop ramp-down: throttle_0 x (1 - t / 5 s), mapped to 0 below 48, then
// 0 at 5 s. Open loop on purpose: the safety path must not depend on the Hall
// sensor, and control below the sensorless limit is not trusted (FD 14.3).
class RampDown {
public:
    void start(int throttle0, uint32_t tick);
    void stop() { active_ = false; }
    bool active() const { return active_; }
    // Throttle for `tick`; ends the ramp once 5 s have elapsed.
    int value(uint32_t tick);

private:
    bool active_ = false;
    int throttle0_ = 0;
    uint32_t start_tick_ = 0;
};

}  // namespace direcsun
