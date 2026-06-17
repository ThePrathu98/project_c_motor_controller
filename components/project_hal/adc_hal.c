#include "adc_hal.h"

#include <stdint.h>

#include "driver/adc.h"
#include "esp_err.h"
#include "esp_log.h"

/*
 * adc_hal.c
 *
 * ESP8266 RTOS SDK ADC wrapper for Day 5-6 current telemetry/fault logic.
 *
 * NodeMCU A0 is used as the external ADC input. The project wiring sends the
 * DRV8870EVM ISEN test-point voltage to A0 after multimeter verification.
 */

#define ADC_RAW_MAX                 1023

/* NodeMCU A0 external pin is normally scaled for approximately 0-3.3 V. */
#define ADC_EXTERNAL_FULL_SCALE_MV  3300

/* DRV8870EVM sense resistor: R150 = 0.150 ohm = 150 milliohm. */
#define DRV8870EVM_ISEN_MOHM        150

static const char *TAG = "adc_hal";
static int32_t s_filtered_current_ma = 0;

void adc_hal_init(void)
{
    adc_config_t config = {
        .mode = ADC_READ_TOUT_MODE,
        .clk_div = 8
    };

    esp_err_t err = adc_init(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc_init failed: err=%d", err);
        return;
    }

    s_filtered_current_ma = 0;

    ESP_LOGI(TAG,
             "ADC current estimate enabled: A0 <- DRV8870EVM ISEN, full_scale=%d mV, Rsense=%d mohm",
             ADC_EXTERNAL_FULL_SCALE_MV,
             DRV8870EVM_ISEN_MOHM);
}

uint16_t adc_hal_read_raw(void)
{
    uint16_t raw = 0;
    esp_err_t err = adc_read(&raw);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "adc_read failed: err=%d", err);
        return 0;
    }

    if (raw > ADC_RAW_MAX)
    {
        raw = ADC_RAW_MAX;
    }

    return raw;
}

uint16_t adc_hal_read_mv(void)
{
    uint32_t raw = adc_hal_read_raw();
    uint32_t mv = (raw * ADC_EXTERNAL_FULL_SCALE_MV) / ADC_RAW_MAX;

    return (uint16_t)mv;
}

int32_t adc_hal_read_current_ma(void)
{
    uint32_t mv = adc_hal_read_mv();
    int32_t current_ma = (int32_t)((mv * 1000U) / DRV8870EVM_ISEN_MOHM);

    /* Light IIR filter to make STATUS/telemetry easier to read. */
    s_filtered_current_ma = ((s_filtered_current_ma * 3) + current_ma) / 4;

    return s_filtered_current_ma;
}
