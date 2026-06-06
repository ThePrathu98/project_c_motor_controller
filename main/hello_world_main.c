#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "motor_hal.h"
#include "encoder_hal.h"

/*
 * TAG is used by ESP_LOGI().
 * In monitor output, logs will appear like:
 *
 * I (1234) app_main: Project C Day 2 hardware bring-up starting
 *
 * This makes it easy to identify which module printed the message.
 */
static const char *TAG = "app_main";

/*
 * app_main() is the ESP8266 RTOS SDK application entry point.
 *
 * This replaces Arduino's setup()/loop().
 * The ESP-IDF/ESP8266 RTOS SDK bootloader starts FreeRTOS,
 * then calls app_main().
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Project C Day 2 hardware bring-up starting");

    /*
     * Initialize the low-level hardware abstraction layers.
     *
     * motor_hal_init():
     *   Configures D5/GPIO14 and D6/GPIO12 as motor-control outputs.
     *
     * encoder_hal_init():
     *   Configures D1/GPIO5 and D2/GPIO4 as encoder input pins.
     */
    motor_hal_init();
    encoder_hal_init();

    /*
     * Always command the motor OFF immediately after initialization.
     * This prevents the motor from moving unexpectedly during boot.
     */
    motor_hal_stop();

    /*
     * Safety delay:
     * The bench motor supply should still be OFF when the ESP8266 boots.
     * After this message appears in the monitor, turn ON the bench supply.
     *
     * The motor should remain stopped during this 10-second delay.
     */
    ESP_LOGI(TAG, "BENCH SUPPLY SHOULD STILL BE OFF.");
    ESP_LOGI(TAG, "Turn bench supply ON now.");
    ESP_LOGI(TAG, "Motor should remain stopped for 10 seconds.");

    /*
     * vTaskDelay() blocks the current FreeRTOS task for a time period.
     *
     * pdMS_TO_TICKS(10000) converts 10000 milliseconds into FreeRTOS ticks.
     * This is better than hardcoding tick counts because the tick rate can vary.
     */
    vTaskDelay(pdMS_TO_TICKS(10000));

    /*
     * Main Day 2 open-loop test loop.
     *
     * This is not closed-loop control yet.
     * We are only proving:
     *   1. ESP8266 can command DRV8870 IN1/IN2.
     *   2. Motor can spin forward.
     *   3. Motor can stop.
     *   4. Motor can spin reverse.
     *   5. Encoder input pins are readable.
     */
    while (1) {
        /*
         * Read raw encoder pin states.
         *
         * This is not yet quadrature decoding.
         * It only confirms that encoder A/B pins are electrically connected
         * and readable by the ESP8266.
         */
        ESP_LOGI(TAG, "Encoder pins: A=%d B=%d",
                 encoder_hal_read_a(),
                 encoder_hal_read_b());

        /*
         * Forward command.
         *
         * The argument is called duty_percent for future PWM support.
         * In the current first-bring-up version, motor_hal_set_duty(15)
         * simply treats positive value as "forward".
         */
        ESP_LOGI(TAG, "FORWARD TEST: 1 second");
        motor_hal_set_duty(15);
        vTaskDelay(pdMS_TO_TICKS(1000));

        /*
         * Stop/coast command.
         *
         * Both DRV8870 inputs are driven low.
         */
        ESP_LOGI(TAG, "STOP: 3 seconds");
        motor_hal_stop();
        vTaskDelay(pdMS_TO_TICKS(3000));

        /*
         * Reverse command.
         *
         * Negative value means reverse direction.
         */
        ESP_LOGI(TAG, "REVERSE TEST: 1 second");
        motor_hal_set_duty(-15);
        vTaskDelay(pdMS_TO_TICKS(1000));

        /*
         * Stop again before repeating the test cycle.
         */
        ESP_LOGI(TAG, "STOP: 5 seconds");
        motor_hal_stop();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}