#pragma once

// The 500 Hz control task (FD sections 7.2 and 9): one owner for sensors,
// estimator, supervisor, and the DShot output, pinned to core 1.

#include <cstdint>

#include "direcsun/core/params.hpp"
#include "direcsun/core/snapshot.hpp"
#include "direcsun/drivers/adc_inputs.hpp"
#include "direcsun/drivers/dshot_rmt.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace direcsun::app {

struct ControlContext {
    drivers::DshotRmt *dshot = nullptr;  // owned by app_main; the zero sender is running on it
    Params params;                       // from load_params()
    bool params_reset = false;
    const char *params_reset_detail = "";
    drivers::BatteryAverage battery;     // primed during the boot window, so BATTERY_LOW starts from real readings
    bool gyro_bias_seeded = false;
    float gyro_bias_dps = 0.0f;
    bool imu_present = false;
    bool mag_present = false;
    bool baro_present = false;
};

// Creates the control task (core 1, priority configMAX_PRIORITIES - 2). The
// task initialises the Hall capture and the tick timer on core 1, takes the
// DShot output over from the zero sender, and runs forever. `persist_task`
// is notified whenever the supervisor changes the parameters.
void control_task_start(const ControlContext &context, TaskHandle_t persist_task);

// True once after a reset caused by the control-stall watchdog (the marker
// lives in RTC memory, which survives a software reset); clears the marker.
bool control_stall_marker_take();

#if CONFIG_DIRECSUN_BENCH_STAGE_TIMING
// Bench diagnostic (Kconfig DIRECSUN_BENCH_STAGE_TIMING): the longest time each
// stage of the control tick took during one second, and how many ticks ran it.
enum class TickStage : uint8_t {
    Imu,
    Mag,
    Adc,
    Hall,
    Estimator,
    Supervisor,
    Dshot,
    Outbound,
    Snapshot,
    Count,
};

struct StageTiming {
    uint32_t second = 0;
    uint32_t max_us[static_cast<int>(TickStage::Count)] = {};
    uint32_t calls[static_cast<int>(TickStage::Count)] = {};
    uint32_t sum_us[static_cast<int>(TickStage::Count)] = {};  // mean = sum_us / calls
};

const char *tick_stage_name(TickStage stage);
SeqlockBuffer<StageTiming> &stage_timing();  // writer: control, 1 Hz
#endif

}  // namespace direcsun::app
