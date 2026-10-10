// HT-7 (mode machine, FD section 13.1) and HT-11 (MANUAL, section 13.5).

#include "doctest.h"
#include "supervisor_harness.hpp"

using namespace harness;

TEST_CASE("HT-7 SAFE to SPINUP on arm when every guard passes")
{
    Rig rig;
    rig.tick({cmd(CommandKind::Arm, kOperator, 41)});
    CHECK(rig.out.mode == Mode::Spinup);
    REQUIRE(rig.out.acks.count == 1);
    CHECK(rig.out.acks.items[0].ok);
    CHECK(rig.out.acks.items[0].seq == 41);
    CHECK(rig.out.acks.items[0].client == kOperator);
    CHECK(rig.out.controlling_client == kOperator);
    CHECK(rig.has_event_cause("arm"));
}

TEST_CASE("R4.2 zero_heading is accepted in SAFE and READY only")
{
    Rig rig;
    rig.tick({cmd(CommandKind::ZeroHeading)});
    CHECK(rig.last_ok());
    CHECK(rig.out.zero_heading_requested);
    rig.tick();
    CHECK_FALSE(rig.out.zero_heading_requested);

    rig.tick({cmd(CommandKind::Arm)});
    rig.tick({cmd(CommandKind::ZeroHeading)});
    CHECK_FALSE(rig.out.zero_heading_requested);
    CHECK_FALSE(rig.out.acks.items[0].ok);

    const float bias = rig.sup.params().omega_bias_rpm;
    rig.in.wheel_rpm_safe = bias;
    rig.run(ticks_from_ms(kSpinupDwellMs));
    REQUIRE(rig.out.mode == Mode::Ready);
    rig.tick({cmd(CommandKind::ZeroHeading)});
    CHECK(rig.last_ok());
    CHECK(rig.out.zero_heading_requested);
    CHECK((rig.out.flags & kFlagHeadingGyro) != 0);
}

TEST_CASE("HT-7 SPINUP to READY after 0.5 s within 5 % of the bias, not one tick sooner")
{
    Rig rig;
    rig.tick({cmd(CommandKind::Arm)});
    const float bias = rig.sup.params().omega_bias_rpm;
    rig.in.wheel_rpm_safe = bias * 1.049f;  // inside the band
    const uint32_t dwell = ticks_from_ms(kSpinupDwellMs);
    rig.run(dwell - 1);
    CHECK(rig.out.mode == Mode::Spinup);
    rig.tick();
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.has_event_cause("spinup_complete"));
}

TEST_CASE("HT-7 SPINUP dwell restarts when the wheel leaves the band")
{
    Rig rig;
    rig.tick({cmd(CommandKind::Arm)});
    const float bias = rig.sup.params().omega_bias_rpm;
    rig.in.wheel_rpm_safe = bias;
    rig.run(ticks_from_ms(kSpinupDwellMs) - 10);
    rig.in.wheel_rpm_safe = bias * 1.06f;  // outside the band for one tick
    rig.tick();
    rig.in.wheel_rpm_safe = bias;
    rig.run(ticks_from_ms(kSpinupDwellMs) - 1);
    CHECK(rig.out.mode == Mode::Spinup);
    rig.tick();
    CHECK(rig.out.mode == Mode::Ready);
}

TEST_CASE("HT-7 READY to MANUAL and back to READY")
{
    Rig rig;
    rig.arm_to_ready();
    REQUIRE(rig.out.mode == Mode::Ready);
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    CHECK(rig.out.mode == Mode::Manual);
    CHECK(rig.out.manual_input == ManualInput::Throttle);
    CHECK(rig.last_ok());
    rig.tick({mode_cmd(Mode::Ready)});
    CHECK(rig.out.mode == Mode::Ready);
    CHECK(rig.out.manual_input == ManualInput::None);
    CHECK(rig.has_event_cause("mode"));
}

TEST_CASE("HT-7 disarm from SPINUP, READY, and MANUAL ramps down to SAFE without a latch")
{
    for (int start = 0; start < 3; ++start) {
        Rig rig;
        if (start == 0) rig.tick({cmd(CommandKind::Arm)});
        if (start == 1) rig.arm_to_ready();
        if (start == 2) rig.enter_manual(ManualInput::Throttle);
        rig.tick({cmd(CommandKind::Disarm)});
        CHECK(rig.out.mode == Mode::Safe);
        CHECK(rig.out.ramping);
        CHECK(rig.last_ok());
        CHECK(rig.has_event_cause("disarm"));
        CHECK(rig.out.latched == 0);
    }
}

TEST_CASE("HT-7 refusals carry their exact reasons and leave the mode alone")
{
    SUBCASE("arm while armed")
    {
        Rig rig;
        rig.tick({cmd(CommandKind::Arm)});
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.last_reason() == "not in SAFE");
        CHECK(rig.out.mode == Mode::Spinup);
    }
    SUBCASE("set_params while armed")
    {
        Rig rig;
        rig.tick({cmd(CommandKind::Arm)});
        ParamUpdate u;
        add_number(u, "k_psi", 2.0);
        rig.tick({set_params_cmd(&u)});
        CHECK(rig.last_reason() == "not in SAFE");
    }
    SUBCASE("box not still: refused at 5 deg/s, accepted just under")
    {
        Rig rig;
        rig.in.rate_dps = -5.0f;
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.last_reason() == "box not still");
        CHECK(rig.out.mode == Mode::Safe);
        rig.in.rate_dps = 4.99f;
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.out.mode == Mode::Spinup);
    }
    SUBCASE("battery: refused at 9.6 V, accepted just above")
    {
        Rig rig;
        rig.in.vbat_avg_v = 9.6f;
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.last_reason() == "battery low");
        rig.in.vbat_avg_v = 9.61f;
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.out.mode == Mode::Spinup);
    }
    SUBCASE("latched fault")
    {
        Rig rig;
        rig.in.missed_tick_streak = kControlStallTicks;
        rig.tick();
        rig.in.missed_tick_streak = 0;
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.last_reason() == "latched fault: CONTROL_STALL");
        CHECK(rig.out.mode == Mode::Safe);
    }
    SUBCASE("not controlling client")
    {
        Rig rig;
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle, kViewer)});
        CHECK(rig.last_reason() == "not controlling client");
        CHECK(rig.out.mode == Mode::Ready);
        rig.tick({cmd(CommandKind::Disarm, kViewer)});
        CHECK(rig.last_reason() == "not controlling client");
        CHECK(rig.out.mode == Mode::Ready);
    }
    SUBCASE("input not available: unbuilt input")
    {
        Rig rig;
        rig.in.built_inputs = manual_input_bit(ManualInput::Throttle);
        rig.arm_to_ready();
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Rate)});
        CHECK(rig.last_reason() == "input not available");
        CHECK(rig.out.mode == Mode::Ready);
        rig.tick({mode_cmd(Mode::Manual, ManualInput::None)});
        CHECK(rig.last_reason() == "input not available");
    }
    SUBCASE("not implemented: later-set modes and commands")
    {
        Rig rig;
        rig.arm_to_ready();
        for (Mode m : {Mode::Detumble, Mode::SunPoint}) {
            rig.tick({mode_cmd(m)});
            CHECK(rig.last_reason() == "not implemented");
            CHECK(rig.out.mode == Mode::Ready);
        }
        // wheel_step is implemented from R3.8 (bench only); test_controllers.cpp pins it.
        for (CommandKind k : {CommandKind::Respin}) {
            rig.tick({cmd(k)});
            CHECK(rig.last_reason() == "not implemented");
        }
    }
    SUBCASE("MANUAL only from READY")
    {
        Rig rig;
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
        CHECK(rig.last_reason() == "not in READY");
        rig.tick({cmd(CommandKind::Arm)});
        rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
        CHECK(rig.last_reason() == "not in READY");
        CHECK(rig.out.mode == Mode::Spinup);
    }
    SUBCASE("mode READY during SPINUP and in SAFE")
    {
        Rig rig;
        rig.tick({mode_cmd(Mode::Ready)});
        CHECK(rig.last_reason() == "not armed");
        rig.tick({cmd(CommandKind::Arm)});
        rig.tick({mode_cmd(Mode::Ready)});
        CHECK(rig.last_reason() == "wheel not at bias");
    }
    SUBCASE("disarm in SAFE")
    {
        Rig rig;
        rig.tick({cmd(CommandKind::Disarm)});
        CHECK(rig.last_reason() == "not armed");
    }
    SUBCASE("arm during the ramp-down")
    {
        Rig rig;
        rig.arm_to_ready();
        rig.tick({cmd(CommandKind::Disarm)});
        REQUIRE(rig.out.ramping);
        rig.tick({cmd(CommandKind::Arm)});
        CHECK(rig.last_reason() == "ramp-down in progress");
        CHECK(rig.out.mode == Mode::Safe);
    }
}

TEST_CASE("HT-7 FR-2: a command takes effect in the tick it is dequeued")
{
    Rig rig;
    rig.arm_to_ready();
    const auto &out = rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    CHECK(out.mode == Mode::Manual);
    CHECK(rig.ctl.last_demand.mode == Mode::Manual);  // the controller saw it the same tick
    rig.tick({cmd(CommandKind::Disarm)});
    CHECK(rig.out.mode == Mode::Safe);
}

TEST_CASE("HT-7 every command except ping gets exactly one ack echoing its seq")
{
    Rig rig;
    rig.tick({cmd(CommandKind::Ping, kOperator, 5), cmd(CommandKind::Arm, kOperator, 6),
              cmd(CommandKind::Arm, kOperator, 7), cmd(CommandKind::Estop, kViewer, 8)});
    REQUIRE(rig.out.acks.count == 3);
    CHECK(rig.out.acks.items[0].seq == 6);
    CHECK(rig.out.acks.items[0].ok);
    CHECK(rig.out.acks.items[1].seq == 7);
    CHECK(std::string(rig.out.acks.items[1].reason) == "not in SAFE");
    CHECK(rig.out.acks.items[2].seq == 8);
    CHECK(rig.out.acks.items[2].client == kViewer);
    CHECK(rig.out.acks.items[2].ok);
    CHECK(rig.out.mode == Mode::Safe);  // the e-stop in the same tick wins
}

TEST_CASE("HT-11 MANUAL setpoints clamp to their limits")
{
    SUBCASE("throttle to the manual band")
    {
        Rig rig;
        rig.enter_manual(ManualInput::Throttle);
        rig.tick({manual_cmd(ManualInput::Throttle, 5000.0f)});
        CHECK(rig.last_ok());
        CHECK(rig.ctl.last_demand.manual_throttle == rig.sup.params().manual_throttle_max);
        rig.run(30);
        rig.tick({manual_cmd(ManualInput::Throttle, 0.0f)});
        CHECK(rig.ctl.last_demand.manual_throttle == rig.sup.params().manual_throttle_min);
    }
    SUBCASE("wheel to [omega_low, omega_cap]")
    {
        Rig rig;
        rig.enter_manual(ManualInput::Wheel);
        rig.tick({manual_cmd(ManualInput::Wheel, 9000.0f)});
        CHECK(rig.ctl.last_demand.manual_wheel_rpm == doctest::Approx(rig.sup.params().omega_cap_rpm));
        rig.run(30);
        rig.tick({manual_cmd(ManualInput::Wheel, 10.0f)});
        CHECK(rig.ctl.last_demand.manual_wheel_rpm == doctest::Approx(rig.sup.params().omega_low_rpm));
    }
    SUBCASE("rate to +-90 deg/s")
    {
        Rig rig;
        rig.enter_manual(ManualInput::Rate);
        rig.tick({manual_cmd(ManualInput::Rate, 500.0f)});
        CHECK(rig.ctl.last_demand.manual_rate_dps == doctest::Approx(90.0f));
        rig.run(30);
        rig.tick({manual_cmd(ManualInput::Rate, -500.0f)});
        CHECK(rig.ctl.last_demand.manual_rate_dps == doctest::Approx(-90.0f));
    }
}

TEST_CASE("HT-11 MANUAL refuses the wrong input, other clients, and setpoints under 50 ms apart")
{
    Rig rig;
    rig.enter_manual(ManualInput::Throttle);
    rig.tick({manual_cmd(ManualInput::Rate, 10.0f)});
    CHECK(rig.last_reason() == "input mismatch");
    rig.tick({manual_cmd(ManualInput::Throttle, 300.0f, kViewer)});
    CHECK(rig.last_reason() == "not controlling client");
    rig.tick({manual_cmd(ManualInput::Throttle, 300.0f)});
    CHECK(rig.last_ok());
    rig.run(ticks_from_ms(kManualMinIntervalMs) - 2);  // 48 ms after the accepted one
    rig.tick({manual_cmd(ManualInput::Throttle, 310.0f)});
    CHECK(rig.last_reason() == "too fast");
    rig.tick({manual_cmd(ManualInput::Throttle, 310.0f)});  // 50 ms
    CHECK(rig.last_ok());
    Rig idle;
    idle.arm_to_ready();
    idle.tick({manual_cmd(ManualInput::Throttle, 300.0f)});
    CHECK(idle.last_reason() == "not in MANUAL");
}

TEST_CASE("HT-11 MANUAL staleness: rate falls to 0, throttle and wheel hold")
{
    const uint32_t stale = ticks_from_ms(kManualStaleMs);
    SUBCASE("rate")
    {
        Rig rig;
        rig.enter_manual(ManualInput::Rate);
        rig.tick({manual_cmd(ManualInput::Rate, 30.0f)});
        rig.run(stale - 2);
        CHECK(rig.ctl.last_demand.manual_rate_dps == doctest::Approx(30.0f));
        CHECK((rig.out.flags & kFlagManualStale) == 0);
        rig.run(2);
        CHECK(rig.ctl.last_demand.manual_rate_dps == 0.0f);
        CHECK((rig.out.flags & kFlagManualStale) != 0);
        rig.tick({manual_cmd(ManualInput::Rate, 20.0f)});
        CHECK((rig.out.flags & kFlagManualStale) == 0);
    }
    SUBCASE("throttle holds")
    {
        Rig rig;
        rig.enter_manual(ManualInput::Throttle);
        rig.tick({manual_cmd(ManualInput::Throttle, 333.0f)});
        rig.run(stale * 3);
        CHECK(rig.ctl.last_demand.manual_throttle == 333);
        CHECK((rig.out.flags & kFlagManualStale) != 0);
        CHECK(rig.out.mode == Mode::Manual);
    }
    SUBCASE("wheel holds")
    {
        Rig rig;
        rig.enter_manual(ManualInput::Wheel);
        rig.tick({manual_cmd(ManualInput::Wheel, 2700.0f)});
        rig.run(stale * 3);
        CHECK(rig.ctl.last_demand.manual_wheel_rpm == doctest::Approx(2700.0f));
    }
}

TEST_CASE("HT-11 accepted setpoints keep the heartbeat alive without pings")
{
    Rig rig;
    rig.enter_manual(ManualInput::Throttle);
    for (int i = 0; i < 30; ++i) {  // 3 s of setpoints every 100 ms, no pings
        rig.tick({manual_cmd(ManualInput::Throttle, 300.0f)});
        rig.run(ticks_from_ms(100) - 1, false);
    }
    CHECK(rig.out.mode == Mode::Manual);
    CHECK((rig.out.latched & interlock_bit(Interlock::HeartbeatLost)) == 0);
}

TEST_CASE("HT-11 entering MANUAL throttle starts from the current throttle")
{
    Rig rig;
    rig.ctl.throttle_value = 260;
    rig.arm_to_ready();
    rig.tick({mode_cmd(Mode::Manual, ManualInput::Throttle)});
    CHECK(rig.ctl.last_demand.manual_throttle == 260);
}
