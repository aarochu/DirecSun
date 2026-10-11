#include "direcsun/core/modes.hpp"

#include <cmath>

#include "direcsun/core/angles.hpp"
#include "direcsun/core/controllers.hpp"
#include <cstdio>

// Mutation-test switches remove one safety mechanism so the host selftest can
// prove the tests notice. They exist only in host selftest builds.
#if defined(ESP_PLATFORM) && (defined(DIRECSUN_MUTANT_NO_LATCH) || defined(DIRECSUN_MUTANT_NO_BYPASS_CLAMP) || \
                              defined(DIRECSUN_MUTANT_PARAMS_DURING_RAMP) || defined(DIRECSUN_MUTANT_DURATIONS_PER_STEP) || \
                              defined(DIRECSUN_MUTANT_SLEW_SETTLE_PER_STEP))
#error "DIRECSUN_MUTANT_* switches are for host selftest builds only"
#endif

namespace direcsun {

const char *mode_wire_name(Mode mode)
{
    switch (mode) {
    case Mode::Safe: return "SAFE";
    case Mode::Spinup: return "SPINUP";
    case Mode::Ready: return "READY";
    case Mode::Manual: return "MANUAL";
    case Mode::Detumble: return "DETUMBLE";
    case Mode::Hold: return "HOLD";
    case Mode::Slew: return "SLEW";
    case Mode::SunPoint: return "SUN_POINT";
    }
    return "SAFE";
}

const char *manual_input_wire_name(ManualInput input)
{
    switch (input) {
    case ManualInput::Throttle: return "throttle";
    case ManualInput::Wheel: return "wheel";
    case ManualInput::Rate: return "rate";
    case ManualInput::None: break;
    }
    return nullptr;
}

const char *flag_wire_name(uint32_t flag_bit)
{
    switch (flag_bit) {
    case kFlagMomentumOk: return "MOMENTUM_OK";
    case kFlagMomentumNearLimit: return "MOMENTUM_NEAR_LIMIT";
    case kFlagManualStale: return "MANUAL_STALE";
    case kFlagHallBypass: return "HALL_BYPASS";
    case kFlagWheelStep: return "WHEEL_STEP";
    case kFlagMomentumSaturated: return "MOMENTUM_SATURATED";
    case kFlagHeadingGyro: return "HEADING_GYRO";
    default: return nullptr;
    }
}

namespace {

// Refusal reasons sent in ack.reason [D]; HT-7 pins each one.
constexpr const char *kNotInSafe = "not in SAFE";
constexpr const char *kNotArmed = "not armed";
constexpr const char *kNotInReady = "not in READY";
constexpr const char *kNotInManual = "not in MANUAL";
constexpr const char *kNotSafeOrReady = "not in SAFE or READY";
constexpr const char *kNotControlling = "not controlling client";
constexpr const char *kRampInProgress = "ramp-down in progress";
constexpr const char *kBenchRequired = "bench_mode required";
constexpr const char *kBoxNotStill = "box not still";
constexpr const char *kBatteryLow = "battery low";
constexpr const char *kInputNotAvailable = "input not available";
constexpr const char *kInputMismatch = "input mismatch";
constexpr const char *kTooFast = "too fast";
constexpr const char *kNotImplemented = "not implemented";
constexpr const char *kWheelNotAtBias = "wheel not at bias";
constexpr const char *kInvalidMode = "invalid mode";
constexpr const char *kInvalidSetpoint = "invalid setpoint";
constexpr const char *kNoParams = "no params";
constexpr const char *kWheelLoopUnavailable = "wheel loop not available";

bool is_later_set_mode(Mode m)
{
    return m == Mode::SunPoint;
}

bool has_heading_reference(Mode m)
{
    return m == Mode::Hold || m == Mode::Slew;
}

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

Supervisor::Supervisor(const Params &params, bool params_were_reset, const char *reset_detail)
    : params_(params)
{
    // Every boot starts with the Hall assumed fitted and bench mode off, whatever
    // the caller passes: the bypass needs two deliberate commands after boot.
    params_.hall_installed = true;
    params_.bench_mode = false;
    params_.bench_underspeed_rpm = 0.0f;
    params_.bench_cap_rpm = 0.0f;
    pending_reset_notice_ = params_were_reset;
    reset_detail_ = reset_detail ? reset_detail : "";
    // A set that fails validation (for example a cap above kOmegaCapMaxRpm)
    // never reaches the control loop: the defaults load instead.
    if (!validate_params(params_).ok) {
        params_ = Params{};
        pending_reset_notice_ = true;
        reset_detail_ = "invalid";
    }
}

void Supervisor::ack(TickOutputs &out, const Command &cmd, const char *reason)
{
    Ack a;
    a.client = cmd.client;
    a.seq = cmd.seq;
    a.ok = false;
    std::snprintf(a.reason, sizeof a.reason, "%s", reason);
    out.acks.push(a);
}

void Supervisor::ack_ok(TickOutputs &out, const Command &cmd)
{
    Ack a;
    a.client = cmd.client;
    a.seq = cmd.seq;
    a.ok = true;
    out.acks.push(a);
}

void Supervisor::post_mode_event(TickOutputs &out, const char *cause)
{
    Event e;
    e.kind = Event::Kind::Mode;
    e.mode = mode_;
    e.cause = cause;
    out.events.push(e);
}

void Supervisor::enter(Mode mode, uint32_t tick)
{
    mode_ = mode;
    mode_entry_tick_ = tick;
    if (mode != Mode::Manual) manual_input_ = ManualInput::None;
    manual_stale_ = false;
    spinup_in_band_ticks_ = 0;
    wheel_hold_ = false;
    pending_wheel_step_rpm_ = 0.0f;
    slew_settled_ticks_ = 0;
}

void Supervisor::drop_to_safe(ThrottleController &controller)
{
    if (mode_ == Mode::Safe) return;
    enter(Mode::Safe, mode_entry_tick_);
    controller.reset();
}

void Supervisor::begin_ramp(uint32_t tick)
{
    // Nothing to ramp from zero throttle; a zero-length ramp would only block
    // the next arm for 5 s.
    if (!ramp_.active() && prev_throttle_ >= kDshotMinThrottle) ramp_.start(prev_throttle_, tick);
}

bool Supervisor::input_available(ManualInput input, ManualInputSet built) const
{
    if (input == ManualInput::None) return false;
    if ((built & manual_input_bit(input)) == 0) return false;
    // With the Hall bypassed, only open-loop throttle exists.
    return params_.hall_installed || input == ManualInput::Throttle;
}

ThrottleBand manual_throttle_band(const Params &params)
{
    ThrottleBand band;
    band.hi = params.manual_throttle_max;
    if (!params.hall_installed && band.hi > kBypassThrottleMax) band.hi = kBypassThrottleMax;
    // A minimum above the bypass ceiling collapses the band onto the ceiling.
    band.lo = params.manual_throttle_min < band.hi ? params.manual_throttle_min : band.hi;
    return band;
}

int Supervisor::clamp_manual_throttle(float requested) const
{
    const ThrottleBand band = manual_throttle_band(params_);
    // Clamp before rounding so huge or negative requests cannot overflow lround.
    long v = std::lround(clampf(requested, 0.0f, static_cast<float>(kDshotMaxThrottle)));
    if (v < band.lo) v = band.lo;
    if (v > band.hi) v = band.hi;
    return static_cast<int>(v);
}

uint32_t Supervisor::spinup_open_loop_ticks() const
{
    const uint32_t rate = static_cast<uint32_t>(kSpinupThrottleRampPerS);
    return (static_cast<uint32_t>(params_.spinup_throttle) * kTickHz + rate - 1) / rate;
}

void Supervisor::handle(const Command &cmd, const TickInputs &in, TickOutputs &out, bool &estop,
                        ThrottleController &controller)
{
    const uint32_t t = in.tick;

    // Heartbeat (FD section 14.4): any message from the controlling client while
    // armed, accepted or not, proves the link is alive.
    if (armed() && cmd.client == controlling_client_) last_heartbeat_tick_ = t;

    if (cmd.kind == CommandKind::Ping) return;  // comms answers pings itself
    if (cmd.kind == CommandKind::Estop) {       // any client, any state
        estop = true;
        ack_ok(out, cmd);
        return;
    }
    if (armed() && cmd.client != controlling_client_) {
        ack(out, cmd, kNotControlling);
        return;
    }

    switch (cmd.kind) {
    case CommandKind::Arm: {
        if (armed()) return ack(out, cmd, kNotInSafe);
        if (ramp_.active()) return ack(out, cmd, kRampInProgress);
        if (!params_.hall_installed && !params_.bench_mode) return ack(out, cmd, kBenchRequired);
        if (!(std::fabs(in.rate_dps) < kArmMaxRateDps)) return ack(out, cmd, kBoxNotStill);
        if (!(in.vbat_avg_v > kArmMinBatteryV)) return ack(out, cmd, kBatteryLow);
        ReleaseInputs release;
        release.wheel_rpm_safe = in.wheel_rpm_safe;
        release.imu_invalid_streak = in.imu_invalid_streak;
        release.tilt_deg = in.tilt_deg;
        release.vbat_avg_v = in.vbat_avg_v;
        for (unsigned i = 0; i < static_cast<unsigned>(Interlock::Count); ++i) {
            const Interlock il = static_cast<Interlock>(i);
            if ((latched_ & interlock_bit(il)) != 0 && !latch_releasable(il, release, params_)) {
                char reason[Ack::kMaxReason];
                std::snprintf(reason, sizeof reason, "latched fault: %s", interlock_wire_name(il));
                return ack(out, cmd, reason);
            }
        }
        latched_ = 0;
        controlling_client_ = cmd.client;
        last_heartbeat_tick_ = t;
        spinup_target_rpm_ = params_.omega_bias_rpm;
        enter(Mode::Spinup, t);
        post_mode_event(out, "arm");
        return ack_ok(out, cmd);
    }
    case CommandKind::Disarm:
        if (!armed()) return ack(out, cmd, kNotArmed);
        begin_ramp(t);
        drop_to_safe(controller);
        post_mode_event(out, "disarm");
        return ack_ok(out, cmd);

    case CommandKind::Mode: {
        const Mode target = cmd.target_mode;
        if (is_later_set_mode(target)) return ack(out, cmd, kNotImplemented);
        if (target == Mode::Ready) {
            if (mode_ == Mode::Ready) {
                // Also ends a bench wheel-step hold (FD section 15.3).
                wheel_hold_ = false;
                pending_wheel_step_rpm_ = 0.0f;
                return ack_ok(out, cmd);
            }
            if (mode_ == Mode::Manual || mode_ == Mode::Detumble || has_heading_reference(mode_)) {
                enter(Mode::Ready, t);
                post_mode_event(out, "mode");
                return ack_ok(out, cmd);
            }
            if (mode_ == Mode::Spinup) return ack(out, cmd, kWheelNotAtBias);
            return ack(out, cmd, kNotArmed);
        }
        if (target == Mode::Manual) {
            if (mode_ != Mode::Ready) return ack(out, cmd, kNotInReady);
            if (!input_available(cmd.manual_input, in.built_inputs)) return ack(out, cmd, kInputNotAvailable);
            enter(Mode::Manual, t);
            manual_input_ = cmd.manual_input;
            // Start from what the wheel is doing now, so entry is not a step.
            manual_throttle_ = clamp_manual_throttle(static_cast<float>(prev_throttle_));
            manual_wheel_rpm_ = clampf(spinup_target_rpm_, params_.omega_low_rpm, params_.omega_cap_rpm);
            manual_rate_dps_ = 0.0f;
            manual_has_setpoint_ = false;
            manual_fresh_tick_ = t;
            post_mode_event(out, "mode");
            return ack_ok(out, cmd);
        }
        if (target == Mode::Detumble) {
            if (mode_ != Mode::Ready) return ack(out, cmd, kNotInReady);
            if (!wheel_loop_available(params_)) return ack(out, cmd, kWheelLoopUnavailable);
            enter(Mode::Detumble, t);
            post_mode_event(out, "mode");
            return ack_ok(out, cmd);
        }
        if (has_heading_reference(target)) {
            // R4.3. From READY only (FD section 13.1). The attitude loop drives
            // the rate loop, which needs the closed wheel loop (V-43). Until Set 5
            // the heading is gyro-only and relative to the last zero_heading
            // (R4.2's guard amendment, V-48), so no magnetometer is required.
            if (mode_ != Mode::Ready) return ack(out, cmd, kNotInReady);
            if (!wheel_loop_available(params_)) return ack(out, cmd, kWheelLoopUnavailable);
            const double psi = in.heading_deg - heading_zero_deg_;
            enter(target, t);
            psi_ref_deg_ = psi;
            if (target == Mode::Slew) {
                // The shortest wrapped path to the target; a half-turn is
                // deterministic: wrap onto (-180, 180] sends it to +180.
                const double end = psi + static_cast<double>(wrap_deg180(static_cast<double>(cmd.psi_ref_deg) - psi));
                slew_.start(psi, end);
            }
            post_mode_event(out, "mode");
            return ack_ok(out, cmd);
        }
        return ack(out, cmd, kInvalidMode);
    }
    case CommandKind::Manual: {
        if (mode_ != Mode::Manual) return ack(out, cmd, kNotInManual);
        if (cmd.setpoint_input != manual_input_) return ack(out, cmd, kInputMismatch);
        if (manual_has_setpoint_ &&
            ticks_since(t, manual_last_setpoint_tick_) < ticks_from_ms(kManualMinIntervalMs)) {
            return ack(out, cmd, kTooFast);
        }
        if (!std::isfinite(cmd.setpoint)) return ack(out, cmd, kInvalidSetpoint);
        switch (manual_input_) {
        case ManualInput::Throttle: manual_throttle_ = clamp_manual_throttle(cmd.setpoint); break;
        case ManualInput::Wheel:
            manual_wheel_rpm_ = clampf(cmd.setpoint, params_.omega_low_rpm, params_.omega_cap_rpm);
            break;
        case ManualInput::Rate:
            manual_rate_dps_ = clampf(cmd.setpoint, -kManualRateLimitDps, kManualRateLimitDps);
            break;
        case ManualInput::None: return ack(out, cmd, kInputMismatch);
        }
        manual_has_setpoint_ = true;
        manual_last_setpoint_tick_ = t;
        manual_fresh_tick_ = t;
        manual_stale_ = false;
        return ack_ok(out, cmd);
    }
    case CommandKind::SetParams: {
        if (armed()) return ack(out, cmd, kNotInSafe);
#ifndef DIRECSUN_MUTANT_PARAMS_DURING_RAMP
        // The ramp-down is SAFE with the wheel still driven (V-65): a new cap
        // would not apply to the running ramp, and the flash write that follows
        // would stall the caches while the motor turns. Wait for the ramp to end.
        if (ramp_.active()) return ack(out, cmd, kRampInProgress);
#endif
        if (cmd.params == nullptr) return ack(out, cmd, kNoParams);
        Params next;
        const ParamsResult r = apply_param_update(params_, *cmd.params, next);
        if (!r.ok) return ack(out, cmd, r.reason);
        const bool bypass_changed = next.hall_installed != params_.hall_installed;
        params_ = next;
        out.params_changed = true;
        if (bypass_changed) {
            Event e;
            e.kind = Event::Kind::Notice;
            e.notice = "HALL_BYPASS";
            e.detail = params_.hall_installed ? "off" : "on";
            out.events.push(e);
        }
        return ack_ok(out, cmd);
    }
    case CommandKind::WheelStep:
        // Bench only (FD section 15.3, R3.8): a step of omega_ref with the rate
        // and attitude loops suspended until `mode READY`. Interlocks stay on.
        if (!params_.bench_mode) return ack(out, cmd, kBenchRequired);
        if (mode_ != Mode::Ready) return ack(out, cmd, kNotInReady);
        if (!wheel_loop_available(params_)) return ack(out, cmd, kWheelLoopUnavailable);
        if (!std::isfinite(cmd.delta_rpm) || std::fabs(cmd.delta_rpm) > kWheelStepMaxRpm) {
            return ack(out, cmd, kInvalidSetpoint);
        }
        wheel_hold_ = true;
        pending_wheel_step_rpm_ += cmd.delta_rpm;
        return ack_ok(out, cmd);

    case CommandKind::ZeroHeading:
        // R4.2: accepted in SAFE and READY only, so it can never move the
        // reference of a HOLD or SLEW in progress. The heading reads 0 from
        // this tick on; SLEW targets are relative to it.
        if (mode_ != Mode::Safe && mode_ != Mode::Ready) return ack(out, cmd, kNotSafeOrReady);
        heading_zero_deg_ = in.heading_deg;
        return ack_ok(out, cmd);

    case CommandKind::Respin:
        return ack(out, cmd, kNotImplemented);
    case CommandKind::Estop:
    case CommandKind::Ping:
        break;
    }
}

void Supervisor::step(const TickInputs &in, ThrottleController &controller, TickOutputs &out)
{
    out.acks.clear();
    out.events.clear();
    out.params_changed = false;
    const uint32_t t = in.tick;
    // Alarms since the previous step: 1 when the control task kept up, more
    // after missed ticks (TickClock). The duration counters below add this, so
    // they follow wall time like everything timed by `t` (V-66).
#ifdef DIRECSUN_MUTANT_DURATIONS_PER_STEP
    const uint32_t elapsed = 1;
#else
    const uint32_t since_last = ticks_since(t, last_step_tick_);
    const uint32_t elapsed = stepped_ && since_last > 0 ? since_last : 1;
#endif
    last_step_tick_ = t;
    stepped_ = true;

    if (pending_reset_notice_) {
        Event e;
        e.kind = Event::Kind::Notice;
        e.notice = "PARAMS_RESET";
        e.detail = reset_detail_;
        out.events.push(e);
        pending_reset_notice_ = false;
    }

    // 1. Commands, in arrival order (FD section 9.1, T9).
    bool estop = in.estop_requested;
    if (in.commands != nullptr) {
        for (std::size_t i = 0; i < in.command_count; ++i) handle(in.commands[i], in, out, estop, controller);
    }

    // 2. Automatic transitions.
    if (mode_ == Mode::Spinup) {
        bool complete = false;
        if (params_.hall_installed) {
            const float band = kSpinupBand * spinup_target_rpm_;
            if (std::fabs(in.wheel_rpm_safe - spinup_target_rpm_) <= band) {
                spinup_in_band_ticks_ += elapsed;
            } else {
                spinup_in_band_ticks_ = 0;
            }
            complete = spinup_in_band_ticks_ >= ticks_from_ms(kSpinupDwellMs);
        } else {
            // Bypass: no speed to check. READY after the open-loop ramp plus the dwell.
            complete = ticks_since(t, mode_entry_tick_) >= spinup_open_loop_ticks() + ticks_from_ms(kSpinupDwellMs);
        }
        if (complete) {
            enter(Mode::Ready, t);
            post_mode_event(out, "spinup_complete");
        }
    }
    if (mode_ == Mode::Manual) {
        manual_stale_ = ticks_since(t, manual_fresh_tick_) >= ticks_from_ms(kManualStaleMs);
        if (manual_stale_ && manual_input_ == ManualInput::Rate) manual_rate_dps_ = 0.0f;
    }
    const double psi = in.heading_deg - heading_zero_deg_;
    bool reference_still = mode_ == Mode::Hold;
    if (mode_ == Mode::Slew) {
        // FD section 12.4: the reference moves along the trapezoid; the slew is
        // complete once the profile has ended and the box has sat within 3 deg
        // and 5 deg/s of the target for 0.5 s, and times out after 6 s.
        const uint32_t ticks = ticks_since(t, mode_entry_tick_);
        const double t_s = static_cast<double>(ticks) / static_cast<double>(kTickHz);
        psi_ref_deg_ = slew_.position_deg(t_s);
        reference_still = slew_.finished(t_s);
        const bool settled = reference_still && std::fabs(slew_.target_deg() - psi) < kSlewSettleDeg &&
                             std::fabs(in.rate_dps) < kSlewSettleRateDps;
        // A duration counter like the others: it adds the alarms this tick
        // stands for, so missed ticks cannot lengthen the dwell (V-66).
#ifdef DIRECSUN_MUTANT_SLEW_SETTLE_PER_STEP
        slew_settled_ticks_ = settled ? slew_settled_ticks_ + 1 : 0;
#else
        slew_settled_ticks_ = settled ? slew_settled_ticks_ + elapsed : 0;
#endif
        if (slew_settled_ticks_ >= ticks_from_ms(kSlewSettleMs)) {
            enter(Mode::Ready, t);
            post_mode_event(out, "slew_complete");
        } else if (ticks >= ticks_from_ms(kSlewTimeoutMs)) {
            enter(Mode::Ready, t);
            post_mode_event(out, "SLEW_TIMEOUT");
        }
    }

    // 3. Controller request (armed modes only) or the ramp (SAFE).
    int candidate = 0;
    if (armed()) {
        ControlDemand d;
        d.mode = mode_;
        d.manual_input = manual_input_;
        d.ticks_in_mode = ticks_since(t, mode_entry_tick_);
        d.omega_target_rpm = spinup_target_rpm_;
        d.spinup_open_loop = !params_.hall_installed;
        d.manual_throttle = manual_throttle_;
        d.manual_wheel_rpm = manual_wheel_rpm_;
        d.manual_rate_dps = manual_rate_dps_;
        d.manual_stale = manual_stale_;
        d.previous_throttle = prev_throttle_;
        d.wheel_step_rpm = pending_wheel_step_rpm_;
        d.wheel_hold = wheel_hold_;
        // READY holds rate (TD section 6.6); HOLD and SLEW close the attitude
        // loop around it (FD section 12.1 step 1, R4.3), with the hold deadband
        // once the reference stands still.
        d.omega_cmd_dps = has_heading_reference(mode_)
                              ? attitude_rate_cmd_dps(psi_ref_deg_, psi, params_.k_psi,
                                                      reference_still ? kHoldDeadbandDeg : 0.0f)
                              : 0.0f;
        pending_wheel_step_rpm_ = 0.0f;
        int request = controller.throttle(d, in, params_);
        if (request < 0) request = 0;
        if (request > kDshotMaxThrottle) request = kDshotMaxThrottle;
#ifndef DIRECSUN_MUTANT_NO_BYPASS_CLAMP
        if (!params_.hall_installed) {
            const int ceiling = params_.manual_throttle_max < kBypassThrottleMax ? params_.manual_throttle_max
                                                                                 : kBypassThrottleMax;
            if (request > ceiling) request = ceiling;
        }
#endif
        candidate = request;
    } else if (ramp_.active()) {
        candidate = ramp_.value(t);
    }

    // 4. Interlocks (FD section 14.2): hard faults first, then e-stop and
    // heartbeat loss, then the ramp.
    FaultInputs faults_in;
    faults_in.elapsed_ticks = elapsed;
    faults_in.hall_installed = params_.hall_installed;
    faults_in.underspeed_applies = mode_ != Mode::Safe && mode_ != Mode::Spinup;
    faults_in.wheel_rpm_safe = in.wheel_rpm_safe;
    faults_in.hall_edge_age_ms = in.hall_edge_age_ms;
    faults_in.throttle = candidate;
    faults_in.vbat_avg_v = in.vbat_avg_v;
    faults_in.tilt_deg = in.tilt_deg;
    faults_in.imu_invalid_streak = in.imu_invalid_streak;
    faults_in.missed_tick_streak = in.missed_tick_streak;
    const InterlockSet hard = monitor_.evaluate(faults_in, params_) & kHardFaults;

    int throttle = candidate;
    if (estop) latched_ |= interlock_bit(Interlock::Estop);
    if (hard != 0) {
        const InterlockSet fresh = hard & static_cast<InterlockSet>(~latched_);
#ifndef DIRECSUN_MUTANT_NO_LATCH
        latched_ |= hard;
#endif
        ramp_.stop();
        drop_to_safe(controller);
        throttle = 0;
        for (unsigned i = 0; i < static_cast<unsigned>(Interlock::Count); ++i) {
            const Interlock il = static_cast<Interlock>(i);
            if ((fresh & interlock_bit(il)) != 0) post_mode_event(out, interlock_wire_name(il));
        }
    } else if (estop) {
        if (armed() || ramp_.active()) {
            begin_ramp(t);
            drop_to_safe(controller);
            throttle = ramp_.value(t);
        } else {
            throttle = 0;
        }
        post_mode_event(out, interlock_wire_name(Interlock::Estop));
    } else if (armed() && ticks_since(t, last_heartbeat_tick_) >= ticks_from_ms(kHeartbeatTimeoutMs)) {
        latched_ |= interlock_bit(Interlock::HeartbeatLost);
        begin_ramp(t);
        drop_to_safe(controller);
        throttle = ramp_.value(t);
        post_mode_event(out, interlock_wire_name(Interlock::HeartbeatLost));
    }

    // 5. A legal DShot value: 0 or 48..2047, and 0 in SAFE unless ramping.
    if (!armed() && !ramp_.active()) throttle = 0;
    if (throttle > 0 && throttle < kDshotMinThrottle) throttle = 0;
    if (throttle > kDshotMaxThrottle) throttle = kDshotMaxThrottle;
    prev_throttle_ = throttle;

    // 6. Flags and outputs.
    // The heading is gyro-only until Set 5 adds the magnetometer (R4.2).
    uint32_t flags = kFlagHeadingGyro;
    if (!params_.hall_installed) flags |= kFlagHallBypass;
    if (mode_ == Mode::Manual && manual_stale_) flags |= kFlagManualStale;
    if (wheel_hold_) flags |= kFlagWheelStep;
    if (armed()) flags |= controller.flags();
    if (params_.hall_installed && (mode_ == Mode::Ready || mode_ == Mode::Detumble ||
                                   mode_ == Mode::Manual || has_heading_reference(mode_))) {
        const bool near = in.wheel_rpm_safe > effective_cap_rpm(params_) - kMomentumWarnRpm ||
                          in.wheel_rpm_safe < params_.omega_low_rpm + kMomentumWarnRpm;
        flags |= near ? kFlagMomentumNearLimit : kFlagMomentumOk;
    }

    out.mode = mode_;
    out.manual_input = manual_input_;
    out.throttle = throttle;
    out.ramping = ramp_.active();
    out.flags = flags;
    out.latched = latched_;
    out.controlling_client = controlling_client_;
    out.heading_deg = in.heading_deg - heading_zero_deg_;
    out.psi_ref_valid = has_heading_reference(mode_);
    out.psi_ref_deg = out.psi_ref_valid ? psi_ref_deg_ : 0.0;
}

}  // namespace direcsun
