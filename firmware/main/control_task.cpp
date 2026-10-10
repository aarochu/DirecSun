// The control task (FD sections 7.2, 9, 14.5). One tick every 2 ms:
//
//   1. IMU burst every tick, magnetometer poll on even ticks, ADC reads spread
//      over ticks 0, 3, and 6 of every 10, Hall ring drained.
//   2. Estimator update.
//   3. Supervisor step: commands, mode machine, controller, interlocks.
//   4. One DShot frame.
//   5. Acks and events posted to comms; a telemetry snapshot every 10th tick.
//   6. Timing: loop_us and the per-second TickStats.
//
// Tick source and stall watchdog: a gptimer alarm at 500 Hz notifies this task
// from its ISR. The ISR also counts alarms since the task last finished a
// tick; at 50 (100 ms) it aborts the system (CONTROL_STALL), and the boot path
// puts DShot 0 back on the wire. The supervisor's own CONTROL_STALL interlock
// trips earlier, at 25 consecutive missed ticks, while the task still runs.

#include "control_task.hpp"

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include "direcsun/core/angles.hpp"
#include "direcsun/core/control.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/modes.hpp"
#include "direcsun/core/snapshot.hpp"
#include "direcsun/core/wheel_speed.hpp"
#include "direcsun/drivers/hall.hpp"
#include "direcsun/drivers/imu.hpp"
#include "direcsun/drivers/mag.hpp"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "shared.hpp"

namespace drv = direcsun::drivers;

namespace direcsun::app {

namespace {

constexpr const char *TAG = "control";

constexpr uint32_t kTickPeriodUs = 1000000 / kTickHz;  // 2000
constexpr uint32_t kStallAbortTicks = 50;              // 100 ms without a finished tick
constexpr uint32_t kTicksPerSnapshot = 10;             // 50 Hz telemetry
constexpr std::size_t kHallBatch = 16;
constexpr UBaseType_t kControlPriority = configMAX_PRIORITIES - 2;
constexpr uint32_t kControlStackBytes = 6144;
constexpr uint32_t kStallMarkerMagic = 0xC57A11EDu;

// Survives a software reset (not a power cycle), so the next boot can say
// that the stall watchdog fired.
RTC_NOINIT_ATTR uint32_t s_stall_marker;

TaskHandle_t s_control_task = nullptr;
TaskHandle_t s_persist_task = nullptr;
gptimer_handle_t s_timer = nullptr;

// Alarms since the control task last finished a tick. Written by the ISR and
// reset by the task; 32-bit atomics are lock-free on the ESP32-S3.
std::atomic<uint32_t> s_alarms_pending{0};

ControlContext s_context;

// The gptimer ISR. Not placed in IRAM on purpose [D]: CONFIG_GPTIMER_ISR_CACHE_SAFE
// is off, so while a flash write disables the cache (parameter persistence,
// accepted only in SAFE) this ISR is held off together with the control task,
// and a long flash write cannot trip the stall watchdog.
bool on_tick_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *)
{
    const uint32_t pending = s_alarms_pending.fetch_add(1, std::memory_order_relaxed) + 1;
    if (pending >= kStallAbortTicks) {
        s_stall_marker = kStallMarkerMagic;
        esp_system_abort("CONTROL_STALL: no control tick finished for 100 ms");
    }
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_control_task, &woken);
    return woken == pdTRUE;
}

esp_err_t start_tick_timer()
{
    gptimer_config_t config = {};
    config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    config.direction = GPTIMER_COUNT_UP;
    config.resolution_hz = 1000000;  // 1 us per count
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&config, &s_timer), TAG, "gptimer_new_timer");

    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = on_tick_alarm;
    // The interrupt is allocated here, on the calling core: core 1, next to
    // this task, so Wi-Fi interrupts on core 0 cannot delay the tick.
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_timer, &callbacks, nullptr), TAG, "callbacks");

    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = kTickPeriodUs;
    alarm.reload_count = 0;
    alarm.flags.auto_reload_on_alarm = true;
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_timer, &alarm), TAG, "alarm");
    ESP_RETURN_ON_ERROR(gptimer_enable(s_timer), TAG, "enable");
    return gptimer_start(s_timer);
}

#if CONFIG_DIRECSUN_BENCH_STALL_TEST
// Bench build only (Kconfig DIRECSUN_BENCH_STALL_TEST, default off): blocks the
// control task long enough that the stall watchdog must reset the board (BC-5).
__attribute__((noinline)) void bench_stall_test_block()
{
    ESP_LOGW(TAG, "BENCH STALL TEST: blocking the control task for %d ms; the stall watchdog must reset the board",
             CONFIG_DIRECSUN_BENCH_STALL_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_DIRECSUN_BENCH_STALL_MS));
    ESP_LOGE(TAG, "BENCH STALL TEST: still running after the block; the stall watchdog did NOT fire");
}
#endif

uint32_t saturating_add(uint32_t a, uint32_t b)
{
    const uint32_t sum = a + b;
    return sum < a ? UINT32_MAX : sum;
}

void control_task(void *)
{
    const ControlContext &ctx = s_context;
    drv::DshotRmt &dshot = *ctx.dshot;
    // The tick ISR notifies this handle; set it here so it is valid before the
    // timer starts, whatever xTaskCreatePinnedToCore has written so far.
    s_control_task = xTaskGetCurrentTaskHandle();

    // Hall capture's GPIO interrupt is allocated on the calling core (core 1).
    const esp_err_t hall_err = drv::hall_init();
    if (hall_err != ESP_OK) {
        ESP_LOGE(TAG, "hall_init: %s", esp_err_to_name(hall_err));
    }

    static Supervisor supervisor(ctx.params, ctx.params_reset, ctx.params_reset_detail);
    static Estimator estimator;
    static WheelSpeed wheel;
    // Rung 1 (R3.7) until the Hall and the T-5 feed-forward table exist, then
    // the closed wheel loop (R3.8). The supervisor clamps and the interlocks
    // override whatever it proposes.
    static CascadeController controller;
    static TickOutputs out;
    static Command commands[kCommandQueueDepth];
    static TelemetrySnapshot snap;
    static TickStats stats;

    drv::BatteryAverage battery = ctx.battery;
    if (ctx.gyro_bias_seeded) {
        estimator.seed_bias(ctx.gyro_bias_dps);
    }

    ESP_ERROR_CHECK(start_tick_timer());

    uint32_t tick = 0;
    uint32_t imu_invalid_streak = 0;
    uint32_t missed_streak = 0;
    uint32_t missed_total = 0;
    uint32_t imu_invalid_second = 0;
    uint32_t missed_second = 0;
    uint32_t outbox_dropped_total = 0;
    uint32_t loop_us_window = 0;
    uint32_t loop_us_second = 0;
    int64_t prev_tick_start = 0;
    int64_t second_start = 0;
    uint32_t second = 0;
    drv::ImuSample imu = {};
    drv::MagSample mag = {};
    bool mag_fresh = false;
    int sun_a_mv = 0;
    int sun_b_mv = 0;
    int64_t hall_edges[kHallBatch];
#if CONFIG_DIRECSUN_BENCH_STALL_TEST
    bool stall_test_done = false;
#endif

    for (;;) {
        // Blocks until the next alarm. More than one pending notification means
        // the previous tick overran and ticks were missed.
        const uint32_t notifications = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int64_t t0 = esp_timer_get_time();
        ++tick;

        if (tick == 1) {
            // Take the DShot output over from the zero sender. stop() returns
            // once no zero frame is in flight; this task's first frame follows
            // immediately, so the gap is at most one zero-sender period.
            drv::dshot_zero_sender_stop();
            dshot.send(0);
            second_start = t0;
            ESP_LOGI(TAG, "control task running on core %d; DShot output taken over from the zero sender",
                     xPortGetCoreID());
        }

        const uint32_t missed = notifications > 1 ? notifications - 1 : 0;
        if (missed > 0) {
            missed_streak = saturating_add(missed_streak, missed);
            missed_total = saturating_add(missed_total, missed);
            missed_second = saturating_add(missed_second, missed);
        } else {
            missed_streak = 0;
        }
        if (prev_tick_start != 0) {
            stats.add_period(t0 - prev_tick_start, kTickPeriodUs);
        }
        prev_tick_start = t0;
        ++stats.ticks;

        // ---- 1. Sensors -----------------------------------------------------
        bool imu_valid = false;
        if (ctx.imu_present) {
            imu_valid = drv::imu_read(imu);
        }
        if (imu_valid) {
            imu_invalid_streak = 0;
        } else {
            imu_invalid_streak = saturating_add(imu_invalid_streak, 1);
            ++imu_invalid_second;
        }
        if (ctx.mag_present && (tick % 2) == 0 && drv::mag_poll(mag)) {
            mag_fresh = true;
        }
        switch (tick % 10) {
        case 0: {
            int mv = 0;
            if (drv::adc_read_mv(drv::AdcInput::kBattery, &mv) == ESP_OK) {
                battery.add(drv::battery_volts_from_pin_mv(mv));
            }
            break;
        }
        case 3:
            drv::adc_read_mv(drv::AdcInput::kSunA, &sun_a_mv);
            break;
        case 6:
            drv::adc_read_mv(drv::AdcInput::kSunB, &sun_b_mv);
            break;
        default:
            break;
        }
        for (;;) {
            const drv::HallDrain drained = drv::hall_drain(hall_edges, kHallBatch);
            for (std::size_t i = 0; i < drained.count; ++i) {
                wheel.add_edge(hall_edges[i]);
            }
            if (drained.count < kHallBatch) {
                break;
            }
        }
        const int64_t now = esp_timer_get_time();
        const float wheel_safe = wheel.speed_safe_rpm(now);
        const float wheel_fast = wheel.speed_fast_rpm(now);
        const int64_t edge_age_us = wheel.last_edge_age_us(now);

        // ---- 2. Estimator ---------------------------------------------------
        const Params &params = supervisor.params();
        if (imu_valid) {
            EstimatorInputs est_in;
            est_in.gyro_z_dps = drv::imu_gyro_dps(imu.gyro[2]);
            for (int axis = 0; axis < 3; ++axis) {
                est_in.accel_g[axis] = drv::imu_accel_g(imu.accel[axis]);
            }
            est_in.safe_mode = supervisor.mode() == Mode::Safe;
            est_in.wheel_known = params.hall_installed;
            est_in.wheel_rpm = wheel_safe;
            est_in.s_gyro = params.s_gyro;
            estimator.update(est_in);
        }

        // ---- 3. Supervisor --------------------------------------------------
        TickInputs in;
        in.tick = tick;
        in.rate_dps = estimator.rate_dps();
        in.heading_deg = estimator.heading_unwrapped_deg();
        in.wheel_rpm_safe = wheel_safe;
        in.wheel_rpm_fast = wheel_fast;
        in.hall_edge_age_ms = edge_age_us < 0 ? UINT32_MAX : static_cast<uint32_t>(edge_age_us / 1000);
        in.vbat_avg_v = battery.average();
        in.tilt_deg = estimator.tilt_deg();
        in.imu_invalid_streak = imu_invalid_streak;
        in.missed_tick_streak = missed_streak;
        in.estop_requested = shared_take_estop();
        in.command_count = shared_drain_commands(commands, kCommandQueueDepth);
        in.commands = commands;
        in.built_inputs = kRungOneBuiltInputs;  // MANUAL throttle; wheel and rate come later
        supervisor.step(in, controller, out);
        outbox_dropped_total = saturating_add(outbox_dropped_total, out.acks.dropped + out.events.dropped);

        // ---- 4. DShot -------------------------------------------------------
        dshot.send(static_cast<uint16_t>(out.throttle));

        // ---- 5. Outbound and snapshot --------------------------------------
        for (std::size_t i = 0; i < out.acks.count; ++i) {
            Outbound item;
            item.kind = Outbound::Kind::Ack;
            item.ack = out.acks.items[i];
            shared_post_outbound(item);
        }
        for (std::size_t i = 0; i < out.events.count; ++i) {
            Outbound item;
            item.kind = Outbound::Kind::Event;
            item.event = out.events.items[i];
            shared_post_outbound(item);
        }
        if (out.params_changed) {
            params_to_persist().write(supervisor.params());
            if (s_persist_task != nullptr) {
                xTaskNotifyGive(s_persist_task);
            }
        }

        if (tick % kTicksPerSnapshot == 0) {
            snap.tick = tick;
            snap.t_us = t0;
            snap.mode = out.mode;
            snap.manual_input = out.manual_input;
            snap.ramping = out.ramping;
            snap.psi_deg = wrap_deg180(out.heading_deg);  // relative to the last zero_heading (R4.2)
            snap.rate_dps = estimator.rate_dps();
            snap.gyro_bias_dps = estimator.bias_dps();
            snap.tilt_deg = estimator.tilt_deg();
            snap.wheel_rpm = wheel_safe;
            snap.wheel_rpm_fast = wheel_fast;
            snap.wheel_ref_rpm = controller.wheel_ref_rpm();
            snap.throttle = out.throttle;
            const ThrottleBand band = manual_throttle_band(supervisor.params());
            snap.throttle_band_lo = band.lo;
            snap.throttle_band_hi = band.hi;
            snap.tau_cmd_mNm = controller.tau_cmd_nm() * 1000.0f;
            snap.vbat_v = battery.average();
            snap.loop_us = loop_us_window;
            snap.flags = out.flags;
            snap.latched = out.latched;
            snap.controlling_client = out.controlling_client;
            snap.bench_mode = supervisor.params().bench_mode;
            snap.imu_present = ctx.imu_present;
            snap.mag_present = ctx.mag_present;
            snap.baro_present = ctx.baro_present;
            for (int axis = 0; axis < 3; ++axis) {
                snap.gyro_raw[axis] = imu.gyro[axis];
                snap.accel_raw[axis] = imu.accel[axis];
                snap.mag_raw[axis] = mag.raw[axis];
            }
            snap.mag_fresh = mag_fresh;
            snap.sun_a_mv = sun_a_mv;
            snap.sun_b_mv = sun_b_mv;
            BaroReading baro;
            if (baro_reading().read(baro, 1)) {
                snap.baro_valid = baro.valid;
                snap.baro_t_c = baro.t_c;
                snap.baro_p_pa = baro.p_pa;
            }
            snap.dropped_events = saturating_add(shared_dropped_outbound(), outbox_dropped_total);
            snap.imu_invalid_streak = imu_invalid_streak;
            snap.missed_ticks_total = missed_total;
            snap.hall_edges = wheel.edge_count();
            snap.hall_glitches = wheel.glitch_count();
            telemetry().write(snap);
            loop_us_window = 0;
            mag_fresh = false;
        }

#if CONFIG_DIRECSUN_BENCH_STALL_TEST
        if (!stall_test_done && second >= CONFIG_DIRECSUN_BENCH_STALL_AFTER_S) {
            stall_test_done = true;
            bench_stall_test_block();
        }
#endif

        // ---- 6. Timing ------------------------------------------------------
        const uint32_t loop_us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        loop_us_window = loop_us > loop_us_window ? loop_us : loop_us_window;
        loop_us_second = loop_us > loop_us_second ? loop_us : loop_us_second;

        if (t0 - second_start >= 1000000) {
            stats.second = ++second;
            stats.missed = missed_second;
            stats.missed_total = missed_total;
            stats.loop_us_max = loop_us_second;
            stats.dshot_sent = dshot.sent();
            stats.dshot_dropped = dshot.dropped();
            stats.imu_invalid = imu_invalid_second;
            tick_stats().write(stats);
            stats = TickStats{};
            second_start = t0;
            missed_second = 0;
            loop_us_second = 0;
            imu_invalid_second = 0;
        }

        // This tick is finished: the stall watchdog starts counting again.
        s_alarms_pending.store(0, std::memory_order_relaxed);
    }
}

}  // namespace

bool control_stall_marker_take()
{
    const bool fired = s_stall_marker == kStallMarkerMagic;
    s_stall_marker = 0;
    return fired;
}

void control_task_start(const ControlContext &context, TaskHandle_t persist_task)
{
    s_context = context;
    s_persist_task = persist_task;
    const BaseType_t ok = xTaskCreatePinnedToCore(control_task, "control", kControlStackBytes, nullptr,
                                                  kControlPriority, &s_control_task, 1);
    configASSERT(ok == pdPASS);
}

}  // namespace direcsun::app
