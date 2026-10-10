// HT-8 (interlocks, FD section 14) and HT-10 (HALL_SILENT, finding V-11).

#include <random>

#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace harness;

namespace {

struct FaultCase {
    const char *name;
    Interlock interlock;
    void (*trip)(TickInputs &);      // the input at its threshold
    void (*inside)(TickInputs &);    // the input 1 % inside the threshold
};

const FaultCase kFaultCases[] = {
    {"OVERSPEED", Interlock::Overspeed,
     [](TickInputs &in) { in.wheel_rpm_safe = kOmegaCapMaxRpm + kOverspeedMarginRpm + 0.5f; },
     [](TickInputs &in) { in.wheel_rpm_safe = (kOmegaCapMaxRpm + kOverspeedMarginRpm) * 0.99f; }},
    {"BATTERY_LOW", Interlock::BatteryLow, [](TickInputs &in) { in.vbat_avg_v = kBatteryLowV - 0.01f; },
     [](TickInputs &in) { in.vbat_avg_v = kBatteryLowV * 1.01f; }},
    {"IMU_SILENT", Interlock::ImuSilent, [](TickInputs &in) { in.imu_invalid_streak = kImuSilentReads; },
     [](TickInputs &in) { in.imu_invalid_streak = kImuSilentReads - 1; }},
    {"TILT", Interlock::Tilt, [](TickInputs &in) { in.tilt_deg = kTiltMaxDeg + 0.01f; },
     [](TickInputs &in) { in.tilt_deg = kTiltMaxDeg * 0.99f; }},
    {"CONTROL_STALL", Interlock::ControlStall,
     [](TickInputs &in) { in.missed_tick_streak = kControlStallTicks; },
     [](TickInputs &in) { in.missed_tick_streak = kControlStallTicks - 1; }},
};

}  // namespace

TEST_CASE("HT-8 each instantaneous hard fault trips at its threshold and cuts throttle in that tick")
{
    for (const FaultCase &fc : kFaultCases) {
        const std::string fault_name = fc.name;
        CAPTURE(fault_name);
        Rig rig;
        rig.arm_to_ready();
        REQUIRE(rig.out.mode == Mode::Ready);
        REQUIRE(rig.out.throttle == 300);
        const int calls_before = rig.ctl.calls;
        fc.trip(rig.in);
        rig.tick();
        CHECK(rig.ctl.calls == calls_before + 1);  // the controller asked for 300 this very tick
        CHECK(rig.out.throttle == 0);
        CHECK(rig.out.mode == Mode::Safe);
        CHECK_FALSE(rig.out.ramping);
        CHECK((rig.out.latched & interlock_bit(fc.interlock)) != 0);
        CHECK(rig.has_event_cause(fc.name));
    }
}

TEST_CASE("HT-8 no hard fault trips 1 % inside its threshold")
{
    for (const FaultCase &fc : kFaultCases) {
        const std::string fault_name = fc.name;
        CAPTURE(fault_name);
        Rig rig;
        rig.arm_to_ready();
        fc.inside(rig.in);
        rig.run(1000);
        CHECK(rig.out.mode == Mode::Ready);
        CHECK(rig.out.latched == 0);
        CHECK(rig.out.throttle == 300);
    }
}

TEST_CASE("HT-8 UNDERSPEED trips after 200 ms below the low limit, not 2 ms sooner, not 1 % above")
{
    Rig rig;
    rig.arm_to_ready();
    const float low = rig.sup.params().omega_low_rpm;
    rig.in.wheel_rpm_safe = low * 1.01f;
    rig.run(1000);
    CHECK(rig.out.mode == Mode::Ready);
    rig.in.wheel_rpm_safe = low - 1.0f;
    rig.run(ticks_from_ms(kUnderspeedMs) - 1);
    CHECK(rig.out.mode == Mode::Ready);
    rig.tick();
    CHECK(rig.out.mode == Mode::Safe);
    CHECK(rig.out.throttle == 0);
    CHECK(rig.has_event_cause("UNDERSPEED"));
}

TEST_CASE("HT-8 UNDERSPEED never applies in SPINUP")
{
    Rig rig;
    rig.tick({cmd(CommandKind::Arm)});
    rig.in.wheel_rpm_safe = 0.0f;
    rig.run(2000);
    CHECK(rig.out.mode == Mode::Spinup);
    CHECK((rig.out.latched & interlock_bit(Interlock::Underspeed)) == 0);
}

TEST_CASE("HT-8 ramp-down reaches 0 at 5.0 s and follows throttle_0 x (1 - t / 5 s)")
{
    Rig rig;
    rig.ctl.throttle_value = 1000;
    rig.arm_to_ready();
    REQUIRE(rig.out.throttle == 1000);
    rig.tick({cmd(CommandKind::Estop)});
    CHECK(rig.out.mode == Mode::Safe);
    CHECK(rig.out.ramping);
    CHECK(rig.out.throttle == 1000);
    const uint32_t total = ticks_from_ms(kRampDownMs);
    int last = 1000;
    for (uint32_t k = 1; k < total; ++k) {
        rig.tick();
        const long expected_raw = 1000L * static_cast<long>(total - k) / static_cast<long>(total);
        const int expected = expected_raw < 48 ? 0 : static_cast<int>(expected_raw);
        REQUIRE(rig.out.throttle == expected);
        REQUIRE(rig.out.throttle <= last);
        REQUIRE(rig.out.ramping);
        last = rig.out.throttle;
    }
    rig.tick();  // k = total: 5.0 s
    CHECK_FALSE(rig.out.ramping);
    CHECK(rig.out.throttle == 0);
    rig.run(100);
    CHECK(rig.out.throttle == 0);
}

TEST_CASE("HT-8 a hard fault during the ramp ends it at once")
{
    Rig rig;
    rig.ctl.throttle_value = 1000;
    rig.arm_to_ready();
    rig.tick({cmd(CommandKind::Disarm)});
    rig.run(100);
    REQUIRE(rig.out.ramping);
    REQUIRE(rig.out.throttle > 0);
    rig.in.tilt_deg = 45.0f;
    rig.tick();
    CHECK(rig.out.throttle == 0);
    CHECK_FALSE(rig.out.ramping);
    CHECK((rig.out.latched & interlock_bit(Interlock::Tilt)) != 0);
}

TEST_CASE("HT-8 ESTOP is accepted in every state, from any client, by flag or command")
{
    for (int state = 0; state < 5; ++state) {
        for (int via_flag = 0; via_flag < 2; ++via_flag) {
            CAPTURE(state);
            CAPTURE(via_flag);
            Rig rig;
            if (state == 1) rig.tick({cmd(CommandKind::Arm)});
            if (state == 2) rig.arm_to_ready();
            if (state == 3) rig.enter_manual(ManualInput::Throttle);
            if (state == 4) {
                rig.arm_to_ready();
                rig.tick({cmd(CommandKind::Disarm)});
            }
            if (via_flag) {
                rig.tick({}, true);
            } else {
                rig.tick({cmd(CommandKind::Estop, kViewer, 99)});
                REQUIRE(rig.out.acks.count == 1);
                CHECK(rig.out.acks.items[0].ok);
                CHECK(rig.out.acks.items[0].seq == 99);
            }
            CHECK(rig.out.mode == Mode::Safe);
            CHECK((rig.out.latched & interlock_bit(Interlock::Estop)) != 0);
            CHECK(rig.has_event_cause("ESTOP"));
        }
    }
}

TEST_CASE("HT-8 HEARTBEAT_LOST at 1 s without the controlling client, not at 990 ms")
{
    Rig rig;
    rig.arm_to_ready();
    rig.tick({cmd(CommandKind::Ping)});  // last word from the operator
    const uint32_t timeout = ticks_from_ms(kHeartbeatTimeoutMs);
    for (uint32_t k = 1; k < timeout; ++k) {
        // A second client pings constantly; it must not keep the box alive.
        rig.tick({cmd(CommandKind::Ping, kViewer)});
        if (k == ticks_from_ms(990)) CHECK(rig.out.mode == Mode::Ready);
    }
    CHECK(rig.out.mode == Mode::Ready);
    rig.tick({cmd(CommandKind::Ping, kViewer)});
    CHECK(rig.out.mode == Mode::Safe);
    CHECK(rig.out.ramping);
    CHECK((rig.out.latched & interlock_bit(Interlock::HeartbeatLost)) != 0);
    CHECK(rig.has_event_cause("HEARTBEAT_LOST"));
}

TEST_CASE("HT-8 latch: no path leaves SAFE after a fault without a new arm")
{
    std::mt19937 rng(20261010u);
    const CommandKind kinds[] = {CommandKind::Disarm, CommandKind::Mode, CommandKind::Manual,
                                 CommandKind::Respin, CommandKind::ZeroHeading, CommandKind::WheelStep,
                                 CommandKind::Ping, CommandKind::SetParams};
    ParamUpdate harmless;
    add_number(harmless, "k_psi", 3.0);

    for (const FaultCase &fc : kFaultCases) {
        const std::string fault_name = fc.name;
        CAPTURE(fault_name);
        Rig rig;
        rig.arm_to_ready();
        fc.trip(rig.in);
        rig.tick();
        REQUIRE(rig.out.mode == Mode::Safe);

        // The physical condition is over, but the latch's release condition is
        // not: the wheel is still above the bias, a few IMU reads still fail,
        // the box is still tilted. CONTROL_STALL never releases.
        rig.healthy_inputs();
        if (fc.interlock == Interlock::Overspeed) rig.in.wheel_rpm_safe = rig.sup.params().omega_bias_rpm + 100.0f;
        if (fc.interlock == Interlock::ImuSilent) rig.in.imu_invalid_streak = 3;
        if (fc.interlock == Interlock::Tilt) rig.in.tilt_deg = kTiltMaxDeg + 5.0f;
        if (fc.interlock == Interlock::BatteryLow) rig.in.vbat_avg_v = 9.0f;  // above 8.8, not above 9.6

        // 2000 ticks of random commands from both clients, never `arm`.
        for (int i = 0; i < 2000; ++i) {
            Command c = cmd(kinds[rng() % (sizeof kinds / sizeof kinds[0])], (rng() & 1) ? kOperator : kViewer,
                            static_cast<uint32_t>(i));
            c.target_mode = static_cast<Mode>(rng() % 8);
            c.manual_input = static_cast<ManualInput>(rng() % 4);
            c.setpoint_input = static_cast<ManualInput>(rng() % 4);
            c.setpoint = static_cast<float>(rng() % 3000);
            c.params = &harmless;
            rig.tick({c});
            REQUIRE(rig.out.mode == Mode::Safe);
        }

        // Arm is refused while the release condition is unmet.
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.out.mode == Mode::Safe);
        CHECK_FALSE(rig.last_ok());
        if (fc.interlock == Interlock::BatteryLow) {
            CHECK(rig.last_reason() == "battery low");
        } else {
            CHECK(rig.last_reason() == std::string("latched fault: ") + fc.name);
        }
    }
}

TEST_CASE("HT-8 latches release on arm once their condition clears, except CONTROL_STALL")
{
    for (const FaultCase &fc : kFaultCases) {
        const std::string fault_name = fc.name;
        CAPTURE(fault_name);
        Rig rig;
        rig.arm_to_ready();
        fc.trip(rig.in);
        rig.tick();
        rig.healthy_inputs();
        rig.tick({cmd(CommandKind::Arm)});
        if (fc.interlock == Interlock::ControlStall) {
            CHECK(rig.out.mode == Mode::Safe);
            CHECK(rig.last_reason() == "latched fault: CONTROL_STALL");
        } else {
            CHECK(rig.out.mode == Mode::Spinup);
            CHECK(rig.out.latched == 0);
        }
    }
}

TEST_CASE("HT-8 MOMENTUM_NEAR_LIMIT is a flag only")
{
    Rig rig;
    rig.arm_to_ready();
    const Params &p = rig.sup.params();
    rig.in.wheel_rpm_safe = p.omega_cap_rpm - kMomentumWarnRpm + 1.0f;
    rig.tick();
    CHECK((rig.out.flags & kFlagMomentumNearLimit) != 0);
    CHECK(rig.out.mode == Mode::Ready);
    rig.in.wheel_rpm_safe = p.omega_cap_rpm - kMomentumWarnRpm - 1.0f;
    rig.tick();
    CHECK((rig.out.flags & kFlagMomentumOk) != 0);
    rig.in.wheel_rpm_safe = p.omega_low_rpm + kMomentumWarnRpm - 1.0f;
    rig.tick();
    CHECK((rig.out.flags & kFlagMomentumNearLimit) != 0);
}

TEST_CASE("HT-10 HALL_SILENT trips in SPINUP after 500 ms of high throttle with no edge, not at 495 ms")
{
    Rig rig;
    rig.ctl.throttle_value = 300;  // above hall_expect_throttle (200)
    rig.in.hall_edge_age_ms = UINT32_MAX;
    rig.tick({cmd(CommandKind::Arm)});  // high-throttle tick 1
    REQUIRE(rig.out.mode == Mode::Spinup);
    const uint32_t trip_ticks = ticks_from_ms(kHallSilentMs);
    // High throttle for 494 ms, then 496 ms (the 2 ms ticks straddle 495 ms).
    rig.run(ticks_from_ms(494) - 1);
    CHECK(rig.out.mode == Mode::Spinup);
    rig.tick();
    CHECK(rig.out.mode == Mode::Spinup);  // 496 ms
    rig.run(trip_ticks - ticks_from_ms(496) - 1);
    CHECK(rig.out.mode == Mode::Spinup);  // 498 ms
    rig.tick();                            // 500 ms
    CHECK(rig.out.mode == Mode::Safe);
    CHECK(rig.out.throttle == 0);
    CHECK(rig.has_event_cause("HALL_SILENT"));
}

TEST_CASE("HT-10 HALL_SILENT stays quiet with edges arriving or with throttle at the expect level")
{
    SUBCASE("edges arriving")
    {
        Rig rig;
        rig.in.hall_edge_age_ms = 10;
        rig.tick({cmd(CommandKind::Arm)});
        rig.run(2000);
        CHECK(rig.out.mode == Mode::Spinup);
    }
    SUBCASE("throttle not above hall_expect_throttle")
    {
        Rig rig;
        rig.ctl.throttle_value = rig.sup.params().hall_expect_throttle;
        rig.in.hall_edge_age_ms = UINT32_MAX;
        rig.tick({cmd(CommandKind::Arm)});
        rig.run(2000);
        CHECK(rig.out.mode == Mode::Spinup);
    }
}
