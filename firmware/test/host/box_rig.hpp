#pragma once

// The whole box for closed-loop host tests: the real estimator, supervisor,
// and cascade driving the throttle-driven wheel of plant_sim.hpp inside the
// two-body box, in the control task's order. Shared by test_closed_loop.cpp
// (R3.9, R4.3) and test_ui_contract.cpp (R4.1, HT-13).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "direcsun/core/control.hpp"
#include "direcsun/core/controllers.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/core/timebase.hpp"
#include "doctest.h"
#include "plant_sim.hpp"
#include "supervisor_harness.hpp"

namespace harness {

constexpr double kBoxRigDt = 1.0 / static_cast<double>(kTickHz);

// Feed-forward and wheel gains for the throttle-driven wheel of the cascade
// tests: steady rpm = 2.5 (throttle - 48), tau 0.8 s, as in test_controllers.cpp.
inline Params cascade_params()
{
    Params p;
    p.ff_count = static_cast<uint8_t>(kFfPoints);
    for (std::size_t i = 0; i < kFfPoints; ++i) {
        p.ff_rpm[i] = 500.0f * static_cast<float>(i + 1);
        p.ff_throttle[i] = 48.0f + p.ff_rpm[i] / 2.5f;
    }
    const WheelPiGains g = wheel_pi_gains(2.5f, 0.8f, kWheelPiBandwidthHz);
    p.wheel_kp = g.kp;
    p.wheel_ki = g.ki;
    return p;
}

// The real estimator, supervisor, and cascade driving the throttle-driven
// wheel inside the two-body box, in the control task's order. The box is
// pinned (the lock pin) until release().
// Counts a magnetometer in the default flipped-deck mounting reads with the box at
// `psi_deg` (counter-clockwise positive): a 400 count horizontal field, no distortion.
inline void mag_counts_at(double psi_deg, int16_t raw[3])
{
    const double psi = psi_deg * plant::kTwoPi / 360.0;
    const double box_x = 400.0 * std::cos(psi);
    const double box_y = -400.0 * std::sin(psi);
    raw[0] = static_cast<int16_t>(std::lround(box_x));    // box X = +sensor X
    raw[1] = static_cast<int16_t>(std::lround(-box_y));   // box Y = -sensor Y
    raw[2] = static_cast<int16_t>(-120);                  // box Z = -sensor Z
}

struct BoxRig {
    Supervisor sup;
    CascadeController ctl;
    Estimator est;
    plant::Box box;
    plant::Gyro gyro;
    TickInputs in;
    TickOutputs out;
    uint32_t now = 1;
    bool pinned = true;
    double peak_tau_nm = 0.0;
    bool saw_saturated_flag = false;
    bool mag_on = false;   // feed the estimator a compass (R5.1); see enable_compass()

    explicit BoxRig(const Params &p) : sup(p)
    {
        box.wheel_rpm = 0.0;
        in.hall_edge_age_ms = 5;
        in.vbat_avg_v = 10.4f;
        in.built_inputs = kAllBuiltInputs;
    }

    void tick(std::vector<Command> cmds = {})
    {
        ++now;
        in.tick = now;
        EstimatorInputs e;
        e.gyro_z_dps = static_cast<float>(gyro.read(box.rate_dps()));
        e.accel_g[2] = 1.0f;
        e.safe_mode = out.mode == Mode::Safe;
        e.wheel_known = true;
        e.wheel_rpm = static_cast<float>(box.wheel_rpm);
        if (mag_on && now % 2 == 0) {
            e.mag_new = true;
            mag_counts_at(box.heading_deg, e.mag_raw);
        }
        est.update(e);
        in.heading_absolute = est.heading_absolute();
        in.rate_dps = est.rate_dps();
        in.heading_deg = est.heading_unwrapped_deg();
        in.wheel_rpm_safe = static_cast<float>(box.wheel_rpm);
        in.wheel_rpm_fast = static_cast<float>(box.wheel_rpm);
        in.commands = cmds.empty() ? nullptr : cmds.data();
        in.command_count = cmds.size();
        sup.step(in, ctl, out);
        in.commands = nullptr;
        in.command_count = 0;
        const double steady = out.throttle <= 48 ? 0.0 : 2.5 * (out.throttle - 48);
        box.step(box.wheel_rpm + (steady - box.wheel_rpm) / 0.8 * kBoxRigDt, kBoxRigDt);
        if (pinned) box.omega = 0.0;
        peak_tau_nm = std::fmax(peak_tau_nm, std::fabs(ctl.tau_cmd_nm()));
        if (out.flags & kFlagMomentumSaturated) saw_saturated_flag = true;
    }

    // Quiet ticks with an operator ping every 100 ms (the heartbeat).
    void run(int n)
    {
        for (int i = 0; i < n; ++i) tick(i % 50 == 0 ? std::vector<Command>{cmd(CommandKind::Ping)} : std::vector<Command>{});
    }

    void arm_to_ready()
    {
        tick({cmd(CommandKind::Arm)});
        for (int i = 0; i < 20 && out.mode != Mode::Ready; ++i) run(500);
        REQUIRE(out.mode == Mode::Ready);
        run(500);
    }

    void enable_compass()
    {
        MagConfig cfg;
        cfg.calibrated = true;
        est.set_mag_config(cfg);
        mag_on = true;
    }

    void release(double rate_dps)
    {
        pinned = false;
        box.omega = rate_dps * plant::kTwoPi / 360.0;
    }

    bool has_event_cause(const char *cause) const
    {
        for (std::size_t i = 0; i < out.events.count; ++i) {
            if (out.events.items[i].kind == Event::Kind::Mode && std::strcmp(out.events.items[i].cause, cause) == 0) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace harness
