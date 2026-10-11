#include "direcsun/drivers/adc_inputs.hpp"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

namespace direcsun::drivers {

namespace {

constexpr const char *TAG = "adc_inputs";
constexpr adc_atten_t kAtten = ADC_ATTEN_DB_12;

struct Input {
    const char *name;
    int gpio;
    adc_channel_t channel;
    adc_cali_handle_t cali;
};

// Order matches AdcInput. Channels are resolved from the GPIO at init, not typed
// here, so a wrong pin is caught by the driver instead of read silently.
Input s_inputs[] = {
    {"sun A", 1, ADC_CHANNEL_0, nullptr},
    {"sun B", 2, ADC_CHANNEL_0, nullptr},
    {"battery", 8, ADC_CHANNEL_0, nullptr},
};

adc_oneshot_unit_handle_t s_unit = nullptr;

}  // namespace

esp_err_t adc_inputs_init()
{
    adc_oneshot_unit_init_cfg_t unit_config = {};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &s_unit), TAG, "adc_oneshot_new_unit");

    for (Input &input : s_inputs) {
        adc_unit_t unit;
        ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(input.gpio, &unit, &input.channel), TAG,
                            "GPIO%d is not an ADC pin", input.gpio);
        if (unit != ADC_UNIT_1) {
            // ADC2 does not work while Wi-Fi is on.
            ESP_LOGE(TAG, "%s on GPIO%d is on ADC%d, not ADC1", input.name, input.gpio,
                     static_cast<int>(unit) + 1);
            return ESP_ERR_INVALID_ARG;
        }

        adc_oneshot_chan_cfg_t channel_config = {};
        channel_config.atten = kAtten;
        channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_unit, input.channel, &channel_config), TAG,
                            "config %s", input.name);

        adc_cali_curve_fitting_config_t cali_config = {};
        cali_config.unit_id = ADC_UNIT_1;
        cali_config.chan = input.channel;
        cali_config.atten = kAtten;
        cali_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        ESP_RETURN_ON_ERROR(adc_cali_create_scheme_curve_fitting(&cali_config, &input.cali), TAG,
                            "curve-fitting calibration for %s", input.name);

        ESP_LOGI(TAG, "%s: GPIO%d = ADC1 channel %d, 12 dB, curve-fitting calibration", input.name,
                 input.gpio, static_cast<int>(input.channel));
    }
    return ESP_OK;
}

esp_err_t adc_read_mv(AdcInput which, int *millivolts)
{
    const Input &input = s_inputs[static_cast<int>(which)];
    if (s_unit == nullptr || input.cali == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return adc_oneshot_get_calibrated_result(s_unit, input.cali, input.channel, millivolts);
}

}  // namespace direcsun::drivers
