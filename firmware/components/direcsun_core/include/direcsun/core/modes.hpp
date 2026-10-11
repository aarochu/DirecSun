#pragma once

// The supervisor: mode machine (FD section 13), command guards, controlling
// client and heartbeat (14.4), interlocks and ramp-down (14.1 to 14.3), and the
// Hall bench bypass. The control task calls step() once per 2 ms tick:
//
//   drained commands + estimates  ->  step()  ->  final throttle, mode, acks, events
//
// step() calls the ThrottleController exactly once in SPINUP, READY, and
// MANUAL (between the mode step and the interlocks, FD section 9.1) and never
// in SAFE. Everything here is a pure function of its inputs and its own state,
// so every row of the mode table is host-testable.

#include <cstddef>
#include <cstdint>

#include "direcsun/core/commands.hpp"
#include "direcsun/core/interlocks.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/core/timebase.hpp"

namespace direcsun {

// Mode guard and timing constants (FD sections 13.1, 13.5).
constexpr float kArmMaxRateDps = 5.0f;          // box still
constexpr uint32_t kSpinupDwellMs = 500;        // in band this long before READY
constexpr float kSpinupBand = 0.05f;            // within 5 % of the target speed
constexpr uint32_t kManualStaleMs = 250;        // setpoints stop: rate falls to 0
constexpr uint32_t kManualMinIntervalMs = 50;   // at most 20 Hz
constexpr float kManualRateLimitDps = 90.0f;
// Open-loop SPINUP throttle ramp (R3.7 uses the same constant), DShot units/s.
constexpr int kSpinupThrottleRampPerS = 200;

// tlm flags owned by the supervisor (FD section 15.3).
enum Flag : uint32_t {
    kFlagMomentumOk = 1u << 0,
    kFlagMomentumNearLimit = 1u << 1,
    kFlagManualStale = 1u << 2,
    kFlagHallBypass = 1u << 3,
    kFlagWheelStep = 1u << 4,        // bench wheel_step hold: rate and attitude loops suspended (R3.8)
    kFlagMomentumSaturated = 1u << 5,  // omega_ref on a limit in the direction the loop needs (R3.9)
};

// Wire name of a single flag bit, or nullptr.
const char *flag_wire_name(uint32_t flag_bit);

// The band every MANUAL throttle setpoint is clamped into (FD section 13.5):
// [manual_throttle_min, manual_throttle_max], with the top capped at
// kBypassThrottleMax while the Hall is bypassed. The supervisor's clamp and the
// tlm `throttle_band` field both come from here, so what the operator sees is
// what the box enforces.
struct ThrottleBand {
    int lo = 0;
    int hi = 0;
};
ThrottleBand manual_throttle_band(const Params &params);

struct TickInputs {
    uint32_t tick = 0;                          // TickClock::now(): one per 2 ms alarm, missed ticks included
    float rate_dps = 0.0f;                      // box rate estimate
    float wheel_rpm_safe = 0.0f;                // median wheel speed (interlocks, spin-up)
    float wheel_rpm_fast = 0.0f;                // latest-period wheel speed (controllers)
    uint32_t hall_edge_age_ms = UINT32_MAX;     // since the last valid Hall edge
    float vbat_avg_v = 0.0f;                    // 1 s average
    float tilt_deg = 0.0f;                      // from the 20 ms averaged accelerometer
    uint32_t imu_invalid_streak = 0;            // consecutive invalid IMU reads
    uint32_t missed_tick_streak = 0;            // consecutive missed control ticks
    bool estop_requested = false;               // the atomic e-stop flag, read and cleared by the caller
    const Command *commands = nullptr;          // drained from the command queue this tick
    std::size_t command_count = 0;
    ManualInputSet built_inputs = 0;            // MANUAL inputs whose loops exist
};

// What the controller is asked to do this tick.
struct ControlDemand {
    Mode mode = Mode::Safe;
    ManualInput manual_input = ManualInput::None;
    uint32_t ticks_in_mode = 0;
    float omega_target_rpm = 0.0f;   // SPINUP target (the bias speed)
    bool spinup_open_loop = false;   // Hall bypassed: SPINUP ramps throttle open loop
    int manual_throttle = 0;         // clamped setpoints (FD section 13.5)
    float manual_wheel_rpm = 0.0f;
    float manual_rate_dps = 0.0f;    // 0 once stale
    bool manual_stale = false;
    int previous_throttle = 0;       // the throttle sent last tick
    // Bench wheel step (R3.8, FD section 15.3): `wheel_step_rpm` is added to
    // omega_ref once, on the tick the command was accepted; `wheel_hold` stays
    // true until `mode READY` is sent again or the mode changes, and suspends
    // the rate and attitude loops so the step is the only input.
    float wheel_step_rpm = 0.0f;
    bool wheel_hold = false;
    // The rate loop's command, deg/s, internal frame (counter-clockwise
    // positive). READY holds rate, so it is 0 there (TD section 6.6); the
    // attitude loop (Set 4) and MANUAL `rate` (Set 5) are what will drive it.
    float omega_cmd_dps = 0.0f;
};

class ThrottleController {
public:
    virtual ~ThrottleController() = default;
    // Called once per tick in SPINUP, READY, and MANUAL. The result is clamped
    // to 0..2047, to the bypass ceiling when the Hall is bypassed, and then
    // overridden by the interlocks.
    virtual int throttle(const ControlDemand &demand, const TickInputs &in, const Params &params) = 0;
    // Called whenever the box drops to SAFE, so integrators restart clean.
    virtual void reset() = 0;
    // tlm flags the controller owns (MOMENTUM_SATURATED), for the tick just
    // run. The supervisor reports them only while armed.
    virtual uint32_t flags() const { return 0; }
};

struct TickOutputs {
    Mode mode = Mode::Safe;
    ManualInput manual_input = ManualInput::None;
    int throttle = 0;                 // final DShot value: 0 or 48..2047
    bool ramping = false;             // ramp-down in progress (mode is SAFE)
    uint32_t flags = 0;               // Flag bits
    InterlockSet latched = 0;         // latched faults (also reported as tlm flags)
    uint32_t controlling_client = 0;  // 0 when no client has armed the box
    bool params_changed = false;      // persist outside the control loop
    Outbox<Ack, 16> acks;
    Outbox<Event, 8> events;
};

class Supervisor {
public:
    // `params` comes from load_params(); if that reset them, the first step()
    // posts the PARAMS_RESET notice with `reset_detail`.
    explicit Supervisor(const Params &params, bool params_were_reset = false, const char *reset_detail = "");

    void step(const TickInputs &in, ThrottleController &controller, TickOutputs &out);

    const Params &params() const { return params_; }
    Mode mode() const { return mode_; }

private:
    bool armed() const { return mode_ != Mode::Safe; }
    void handle(const Command &cmd, const TickInputs &in, TickOutputs &out, bool &estop,
                ThrottleController &controller);
    void ack(TickOutputs &out, const Command &cmd, const char *reason);
    void ack_ok(TickOutputs &out, const Command &cmd);
    void post_mode_event(TickOutputs &out, const char *cause);
    void enter(Mode mode, uint32_t tick);
    void drop_to_safe(ThrottleController &controller);
    void begin_ramp(uint32_t tick);
    bool input_available(ManualInput input, ManualInputSet built) const;
    int clamp_manual_throttle(float requested) const;
    uint32_t spinup_open_loop_ticks() const;

    Params params_;
    Mode mode_ = Mode::Safe;
    ManualInput manual_input_ = ManualInput::None;
    uint32_t mode_entry_tick_ = 0;
    uint32_t controlling_client_ = 0;
    uint32_t last_heartbeat_tick_ = 0;
    InterlockSet latched_ = 0;
    InterlockMonitor monitor_;
    RampDown ramp_;
    int prev_throttle_ = 0;
    uint32_t last_step_tick_ = 0;
    bool stepped_ = false;

    float spinup_target_rpm_ = 0.0f;
    uint32_t spinup_in_band_ticks_ = 0;

    int manual_throttle_ = 0;
    float manual_wheel_rpm_ = 0.0f;
    float manual_rate_dps_ = 0.0f;
    bool manual_has_setpoint_ = false;
    uint32_t manual_last_setpoint_tick_ = 0;
    uint32_t manual_fresh_tick_ = 0;  // last setpoint, or MANUAL entry
    bool manual_stale_ = false;

    bool wheel_hold_ = false;
    float pending_wheel_step_rpm_ = 0.0f;

    bool pending_reset_notice_ = false;
    const char *reset_detail_ = "";
};

}  // namespace direcsun
