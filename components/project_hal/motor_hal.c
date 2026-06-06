#include "motor_hal.h"

#include "driver/gpio.h"
#include "esp_log.h"

/*
 * ESP8266 NodeMCU pin mapping:
 *
 * NodeMCU D5 = ESP8266 GPIO14 -> DRV8870 IN1
 * NodeMCU D6 = ESP8266 GPIO12 -> DRV8870 IN2
 *
 * DRV8870 input logic used here:
 *   IN1 = 1, IN2 = 0 -> motor forward
 *   IN1 = 0, IN2 = 1 -> motor reverse
 *   IN1 = 0, IN2 = 0 -> coast/stop
 *
 * For this first Day 2 test, we use simple GPIO ON/OFF control.
 * Later, this file will be upgraded to PWM duty control.
 */
#define MOTOR_IN1_GPIO   14
#define MOTOR_IN2_GPIO   12

static const char *TAG = "motor_hal";

void motor_hal_init(void)
{
    /*
     * gpio_config_t describes how GPIO pins should behave.
     *
     * pin_bit_mask:
     *   Selects which pins are configured.
     *   (1ULL << GPIO_NUMBER) creates a bit mask for that pin.
     *
     * mode:
     *   GPIO_MODE_OUTPUT because IN1/IN2 are outputs from ESP8266
     *   to the DRV8870 motor driver.
     *
     * pull_up_en / pull_down_en:
     *   Disabled because these are actively driven output pins.
     *
     * intr_type:
     *   Disabled because motor output pins do not need interrupts.
     */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_IN1_GPIO) | (1ULL << MOTOR_IN2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    /*
     * Start in a safe state.
     *
     * Both inputs LOW means the motor should not be driven.
     * This prevents unexpected motion immediately after boot.
     */
    gpio_set_level(MOTOR_IN1_GPIO, 0);
    gpio_set_level(MOTOR_IN2_GPIO, 0);

    ESP_LOGI(TAG, "Motor HAL initialized: D5/GPIO14=IN1, D6/GPIO12=IN2");
}

void motor_hal_set_duty(int duty_percent)
{
    /*
     * This function name intentionally uses "duty_percent" because
     * the final project will control PWM duty cycle.
     *
     * For this first hardware bring-up step, we only use the sign:
     *
     *   positive -> forward
     *   negative -> reverse
     *   zero     -> stop/coast
     *
     * This proves the DRV8870 direction path before adding PWM.
     */
    if (duty_percent > 0) {
        /*
         * Forward:
         * IN1 HIGH, IN2 LOW.
         */
        gpio_set_level(MOTOR_IN1_GPIO, 1);
        gpio_set_level(MOTOR_IN2_GPIO, 0);
        ESP_LOGI(TAG, "Motor FORWARD command");
    } else if (duty_percent < 0) {
        /*
         * Reverse:
         * IN1 LOW, IN2 HIGH.
         */
        gpio_set_level(MOTOR_IN1_GPIO, 0);
        gpio_set_level(MOTOR_IN2_GPIO, 1);
        ESP_LOGI(TAG, "Motor REVERSE command");
    } else {
        /*
         * Stop/coast:
         * IN1 LOW, IN2 LOW.
         *
         * This does not actively brake the motor.
         * It lets the motor coast down.
         */
        gpio_set_level(MOTOR_IN1_GPIO, 0);
        gpio_set_level(MOTOR_IN2_GPIO, 0);
        ESP_LOGI(TAG, "Motor STOP command");
    }
}

void motor_hal_stop(void)
{
    /*
     * motor_hal_stop() gives higher-level code a clear stop API.
     * Internally it simply sends a zero command.
     */
    motor_hal_set_duty(0);
}