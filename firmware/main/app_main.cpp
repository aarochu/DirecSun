// DirecSun flight firmware entry point.
//
// Skeleton (Set 1, R1.4): prints a boot banner and returns. The boot sequence
// of FD section 8 (DShot zeros first, sensors, control and comms tasks) lands
// in Set 3.

#include <cinttypes>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace {

constexpr const char *TAG = "direcsun";

const char *chip_model_name(esp_chip_model_t model)
{
    return model == CHIP_ESP32S3 ? "ESP32-S3" : "unexpected chip";
}

uint32_t bytes_to_mb(uint32_t bytes)
{
    return bytes / (1024U * 1024U);
}

}  // namespace

extern "C" void app_main(void)
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

    ESP_LOGI(TAG, "DirecSun skeleton");
    ESP_LOGI(TAG, "firmware %s, ESP-IDF %s", app->version, esp_get_idf_version());
    ESP_LOGI(TAG, "target %s, chip %s rev v%d.%d, %d cores",
             CONFIG_IDF_TARGET, chip_model_name(chip.model),
             chip.revision / 100, chip.revision % 100, chip.cores);
    if (configured_err == ESP_OK && physical_err == ESP_OK) {
        ESP_LOGI(TAG, "flash %" PRIu32 "MB configured, %" PRIu32 "MB detected",
                 bytes_to_mb(flash_configured), bytes_to_mb(flash_physical));
    } else {
        ESP_LOGE(TAG, "flash size query failed: configured %s, physical %s",
                 esp_err_to_name(configured_err), esp_err_to_name(physical_err));
    }
    if (chip.model != CHIP_ESP32S3) {
        ESP_LOGE(TAG, "this firmware is built for the ESP32-S3 (XIAO ESP32-S3)");
    }
}
