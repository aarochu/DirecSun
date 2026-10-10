#pragma once

// Closed-loop plant for the host tests (FD section 19.1, HT-5 and HT-6): the
// two-body box and wheel of TD section 3.1 about one vertical axis, Coulomb
// bearing friction, a wheel that follows its reference (either the R3.8 loop
// modelled as a 2 Hz first-order lag, or a throttle-driven wheel), and a gyro
// with the MPU-6050 DLPF delay and white noise. Internal frame throughout
// (FD section 2.3): counter-clockwise positive, wheel speed in its forward
// direction.
//
// It can find sign and logic errors; it cannot predict ESC dynamics, friction,
// or vibration (FD section 23.3).

#include <cmath>
#include <cstdint>
#include <deque>
#include <random>

namespace plant {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kRpmToRadPerS = kTwoPi / 60.0;

// Box and wheel about the spin axis (TD section 3.1):
//   (I_b + I_w) d(omega_b)/dt = -I_w alpha_rel + tau_ext
// where alpha_rel is the wheel's acceleration about +z relative to the box
// (s_w times the forward acceleration) and tau_ext is bearing friction.
struct Box {
    double i_b = 1.03e-3;          // kg m^2, TD section 3.2
    double i_w = 6.50e-5;          // kg m^2, baseline washer wheel
    int s_w = 1;                   // true sign of the wheel's spin about +z when running forward
    double friction_nm = 0.0;      // Coulomb friction magnitude
    double omega = 0.0;            // box rate, rad/s
    double wheel_rpm = 2400.0;     // wheel forward speed relative to the box

    // Moves the wheel to `new_wheel_rpm` over `dt` and integrates the box.
    void step(double new_wheel_rpm, double dt)
    {
        const double alpha_rel = s_w * (new_wheel_rpm - wheel_rpm) * kRpmToRadPerS / dt;
        const double drive = -i_w * alpha_rel;
        wheel_rpm = new_wheel_rpm;
        if (std::fabs(omega) < 1e-4 && std::fabs(drive) <= friction_nm) {
            omega = 0.0;  // stuck: the drive cannot break the static friction
            return;
        }
        const double direction = omega != 0.0 ? (omega > 0.0 ? 1.0 : -1.0) : (drive > 0.0 ? 1.0 : -1.0);
        const double next = omega + (drive - friction_nm * direction) / (i_b + i_w) * dt;
        // Friction stops the box; it never reverses it.
        omega = (omega != 0.0 && next * omega < 0.0 && std::fabs(drive) <= friction_nm) ? 0.0 : next;
    }

    double rate_dps() const { return omega * 360.0 / kTwoPi; }
};

// The closed wheel loop of R3.8 as a first-order lag at its bandwidth.
struct WheelLag {
    double bandwidth_hz = 2.0;
    double follow(double ref_rpm, double rpm, double dt) const
    {
        const double tau = 1.0 / (kTwoPi * bandwidth_hz);
        return rpm + (ref_rpm - rpm) / tau * dt;
    }
};

// MPU-6050 gyro after the DLPF: a pure delay of the filter's 8.3 ms (4 ticks
// at 2 ms) plus white noise, deterministic for a given seed.
struct Gyro {
    std::deque<double> line;
    std::size_t delay_ticks = 4;
    double noise_dps = 0.05;
    std::mt19937 rng{20261010u};
    std::normal_distribution<double> noise{0.0, 1.0};

    double read(double true_dps)
    {
        line.push_back(true_dps);
        while (line.size() > delay_ticks + 1) line.pop_front();
        return line.front() + noise_dps * noise(rng);
    }
};

}  // namespace plant
