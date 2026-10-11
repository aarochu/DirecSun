#pragma once

// Low-priority tasks on core 0 that keep slow or blocking work off the control
// task (FD sections 7.2, 10.3, 16):
//
//   baro      BMP280 at 1 Hz. baro_read() can block for about 0.3 s on a bus
//             error, so it never runs on the control task (R3.2).
//   persist   Writes the parameter set to NVS when the control task signals a
//             change. Flash writes stall the cache on both cores.
//   status    One log line per second with the control loop's timing, for the
//             bench checks (BC-3 jitter, F-4 budget, DShot never gaps).

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace direcsun::app {

class NvsKvStore;

void baro_task_start();
// Returns the persistence task's handle for control_task_start().
TaskHandle_t persist_task_start(NvsKvStore &store);
void status_task_start();

}  // namespace direcsun::app
