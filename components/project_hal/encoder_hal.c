#include "encoder_hal.h"

#include "driver/gpio.h"
#include "esp_log.h"

/*
 * Encoder wiring:
 *
 * Encoder yellow wire -> NodeMCU D1 -> GPIO5 -> Encoder channel A
 * Encoder white wire  -> NodeMCU D2 -> GPIO4 -> Encoder channel B
 *
 * Encoder blue wire   -> ESP8266 3V3
 * Encoder green wire  -> ESP8266 GND
 */
#define ENCODER_A_GPIO   5
#define ENCODER_B_GPIO   4

static const char *TAG = "encoder_hal";

void encoder_hal_init(void)
{
    /*
     * Configure encoder A/B pins as inputs.
     *
     * Pull-ups are enabled so the input has a defined logic level
     * if the encoder output is temporarily floating.
     *
     * Interrupts are disabled for this first test.
     * Later, we will enable edge interrupts for quadrature counting.
     */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Encoder HAL initialized: A=D1/GPIO5, B=D2/GPIO4");
}

int encoder_hal_read_a(void)
{
    /*
     * Return the instantaneous digital logic level on encoder channel A.
     *
     * Expected value:
     *   0 = low
     *   1 = high
     */
    return gpio_get_level(ENCODER_A_GPIO);
}

int encoder_hal_read_b(void)
{
    /*
     * Return the instantaneous digital logic level on encoder channel B.
     *
     * At this stage we are not counting pulses yet.
     * We are only confirming that the pin can be read.
     */
    return gpio_get_level(ENCODER_B_GPIO);
}