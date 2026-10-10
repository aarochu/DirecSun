// DirecSun flight firmware entry point: the boot sequence (FD section 8.1).
//
//   1. NVS and the parameter set (FD section 16), then DShot 0 on the ESC pin.
//      The parameters come first because they fix the DShot rate, and the rate
//      is never switched while the ESC is listening [D]. This costs a few
//      milliseconds before the first zero frame; nothing is logged before it.
//   2. Banner, reset reason, sensors on the shared I2C bus, ADC.
//   3. The boot window: at least 1 s of DShot 0 (the ESC arms on zeros), used
//      to prime the battery average and to seed the gyro bias from a still box.
//   4. Background tasks on core 0, then the control task on core 1, which takes
//      the DShot output over from the zero sender.
//   5. Comms (R3.5) starts here once it exists. Wi-Fi never delays control.

#include <cinttypes>

#include "control_task.hpp"
#include "direcsun/core/estimator.hpp"
#include "direcsun/core/params.hpp"
#include "direcsun/drivers/adc_inputs.hpp"
#include "direcsun/drivers/baro.hpp"
#include "direcsun/drivers/dshot_rmt.hpp"
#include "direcsun/drivers/i2c_bus.hpp"
#include "direcsun/drivers/imu.hpp"
#include "direcsun/drivers/mag.hpp"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs_store.hpp"
#include "sdkconfig.h"
#include "services.hpp"
#include "shared.hpp"

namespace drv = direcsun::drivers;
using namespace direcsun;

namespace {

constexpr const char *TAG = "direcsun";

constexpr gpio_num_t kEscPin = GPIO_NUM_7;           // XIAO D8 (TD section 5.4)
constexpr int64_t kBootWindowUs = 1000000;           // at least 1 s of DShot 0 (TD A-22)
constexpr uint32_t kBootWindowFrames = 500;          // 1 s of frames at 2 ms
constexpr TickType_t kBootSamplePeriod = pdMS_TO_TICKS(2);

const char *chip_model_name(esp_chip_model_t model)
{
    return model == CHIP_ESP32S3 ? "ESP32-S3" : "unexpected chip";
}

uint32_t bytes_to_mb(uint32_t bytes)
{
    return bytes / (1024U * 1024U);
}

const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software (esp_restart)";
    case ESP_RST_PANIC: return "panic or abort";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB peripheral";
    case ESP_RST_JTAG: return "JTAG";
    default: return "unknown";
    }
}

void log_banner()
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    // The configured size comes from the image header (sdkconfig); the physical
    // size is read from the flash chip's JEDEC ID. Printing both shows when the
    // configuration claims more flash than the board has.
    uint32_t flash_configured = 0;
    uint32_t flash_physical = 0;
    const esp_err_t configured_err = esp_flash_get_size(nullptr, &flash_configured);
    const esp_err_t physical_err = esp_flash_get_physical_size(nullptr, &flash_physical);

    ESP_LOGI(TAG, "DirecSun flight firmware");
    ESP_LOGI(TAG, "firmware %s, ESP-IDF %s", app->version, esp_get_idf_version());
    ESP_LOGI(TAG, "target %s, chip %s rev v%d.%d, %d cores", CONFIG_IDF_TARGET, chip_model_name(chip.model),
             chip.revision / 100, chip.revision % 100, chip.cores);
    if (configured_err == ESP_OK && physical_err == ESP_OK) {
        ESP_LOGI(TAG, "flash %" PRIu32 "MB configured, %" PRIu32 "MB detected", bytes_to_mb(flash_configured),
                 bytes_to_mb(flash_physical));
    } else {
        ESP_LOGE(TAG, "flash size query failed: configured %s, physical %s", esp_err_to_name(configured_err),
                 esp_err_to_name(physical_err));
    }
    if (chip.model != CHIP_ESP32S3) {
        ESP_LOGE(TAG, "this firmware is built for the ESP32-S3 (XIAO ESP32-S3)");
    }
#if CONFIG_DIRECSUN_BENCH_STALL_TEST
    ESP_LOGW(TAG, "BENCH BUILD: the stall-watchdog test hook will block the control task %d s after it starts",
             CONFIG_DIRECSUN_BENCH_STALL_AFTER_S);
#endif
}

esp_err_t init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

}  // namespace

extern "C" void app_main(void)
{
    // ---- 1. Parameters, then DShot 0 -------------------------------------
    const esp_err_t nvs_err = init_nvs();
    static app::NvsKvStore store;  // lives for the persistence task
    app::ControlContext ctx;
    const ParamsLoad load = load_params(store, ctx.params);
    ctx.params_reset = load.reset;
    ctx.params_reset_detail = load.detail;

    static drv::DshotRmt dshot;
    const drv::DshotRate rate = ctx.params.dshot_rate == 300 ? drv::DshotRate::kDshot300 : drv::DshotRate::kDshot150;
    ESP_ERROR_CHECK(dshot.init(kEscPin, rate));
    ESP_ERROR_CHECK(drv::dshot_zero_sender_start(dshot));
    const int64_t first_frame_us = esp_timer_get_time();
    ctx.dshot = &dshot;

    // ---- 2. Banner, reset reason, sensors --------------------------------
    log_banner();
    const esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "reset reason: %s (%d)", reset_reason_name(reason), static_cast<int>(reason));
    if (app::control_stall_marker_take()) {
        ESP_LOGW(TAG, "previous reset: CONTROL_STALL watchdog (a control tick did not finish for 100 ms)");
    }
    ESP_LOGI(TAG, "DShot%u zeros on GPIO%d from %" PRId64 " us after boot", static_cast<unsigned>(rate),
             static_cast<int>(kEscPin), first_frame_us);
    if (nvs_err != ESP_OK || !store.ok()) {
        ESP_LOGE(TAG, "NVS unavailable (%s): running on default parameters", esp_err_to_name(nvs_err));
    }
    if (load.reset) {
        ESP_LOGW(TAG, "parameters reset to defaults (%s)", load.detail);
    }

    app::shared_init();

    ESP_ERROR_CHECK(drv::i2c_bus_init());
    const esp_err_t imu_err = drv::imu_init();
    ctx.imu_present = imu_err == ESP_OK;
    ESP_LOGI(TAG, "IMU: %s (WHO_AM_I 0x%02x)%s", esp_err_to_name(imu_err), drv::imu_who_am_i(),
             ctx.imu_present ? "" : ", IMU_MISSING: the box stays in SAFE");
    const esp_err_t mag_err = drv::mag_init();
    ctx.mag_present = mag_err == ESP_OK;
    ESP_LOGI(TAG, "magnetometer: %s (chip ID 0x%02x)%s", esp_err_to_name(mag_err), drv::mag_chip_id(),
             ctx.mag_present ? "" : ", MAG_MISSING");
    const esp_err_t baro_err = drv::baro_init();
    ctx.baro_present = baro_err == ESP_OK;
    ESP_LOGI(TAG, "barometer: %s (chip ID 0x%02x)", esp_err_to_name(baro_err), drv::baro_chip_id());
    const esp_err_t adc_err = drv::adc_inputs_init();
    if (adc_err != ESP_OK) {
        ESP_LOGE(TAG, "ADC: %s; the battery average stays at 0 V, so BATTERY_LOW latches", esp_err_to_name(adc_err));
    }

    // ---- 3. Boot window ---------------------------------------------------
    // The control task's interlocks must start from real readings: a 0 V
    // battery average would latch BATTERY_LOW, and a gyro offset larger than
    // the 2 deg/s gate would never be learned (GyroBiasSeed).
    GyroBiasSeed seed;
    TickType_t wake = xTaskGetTickCount();
    uint32_t samples = 0;
    while (esp_timer_get_time() - first_frame_us < kBootWindowUs || dshot.sent() < kBootWindowFrames) {
        vTaskDelayUntil(&wake, kBootSamplePeriod);
        drv::ImuSample imu;
        if (ctx.imu_present && drv::imu_read(imu)) {
            const float accel[3] = {drv::imu_accel_g(imu.accel[0]), drv::imu_accel_g(imu.accel[1]),
                                    drv::imu_accel_g(imu.accel[2])};
            seed.add(drv::imu_gyro_dps(imu.gyro[2]), accel);
        }
        if (samples++ % 10 == 0) {  // 50 Hz, the battery interlock's rate
            int mv = 0;
            if (adc_err == ESP_OK && drv::adc_read_mv(drv::AdcInput::kBattery, &mv) == ESP_OK) {
                ctx.battery.add(drv::battery_volts_from_pin_mv(mv));
            }
        }
    }
    ctx.gyro_bias_seeded = seed.result(ctx.gyro_bias_dps);
    if (ctx.gyro_bias_seeded) {
        // The window's mean norm is what "still" means for this accelerometer
        // from now on (FD 11.1 as amended).
        ctx.accel_reference_g = seed.reference_norm_g();
        ESP_LOGI(TAG,
                 "gyro z bias seeded at %.3f deg/s from %u still samples (range %.3f); "
                 "accel reference %.3f g (norm %.3f to %.3f)",
                 static_cast<double>(ctx.gyro_bias_dps), static_cast<unsigned>(seed.samples()),
                 static_cast<double>(seed.range_dps()), static_cast<double>(ctx.accel_reference_g),
                 static_cast<double>(seed.norm_min_g()), static_cast<double>(seed.norm_max_g()));
    } else {
        ESP_LOGW(TAG,
                 "gyro z bias not seeded: %s (%u samples, gyro range %.3f deg/s, accel norm %.3f g, %.3f to %.3f)",
                 seed.failure(), static_cast<unsigned>(seed.samples()), static_cast<double>(seed.range_dps()),
                 static_cast<double>(seed.reference_norm_g()), static_cast<double>(seed.norm_min_g()),
                 static_cast<double>(seed.norm_max_g()));
    }
    ESP_LOGI(TAG, "boot window done: %" PRIu32 " DShot frames sent, battery %.2f V over %u readings", dshot.sent(),
             static_cast<double>(ctx.battery.average()), static_cast<unsigned>(ctx.battery.count()));

    // ---- 4. Tasks ---------------------------------------------------------
    if (ctx.baro_present) {
        app::baro_task_start();  // baro_read() retries for about 0.3 s per call when nothing answers
    }
    TaskHandle_t persist = app::persist_task_start(store);
    app::status_task_start();
    app::control_task_start(ctx, persist);

    // ---- 5. Comms (R3.5) ----------------------------------------------------
}
