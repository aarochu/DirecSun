#pragma once

// The 500 Hz control task (FD sections 7.2 and 9): one owner for sensors,
// estimator, supervisor, and the DShot output, pinned to core 1.

#include <cstdint>

#include "direcsun/core/params.hpp"
#include "direcsun/drivers/adc_inputs.hpp"
#include "direcsun/drivers/dshot_rmt.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

}  // namespace direcsun::app
