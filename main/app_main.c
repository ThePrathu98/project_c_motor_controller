#include "esp_log.h"

#include "adc_hal.h"
#include "command_server.h"
#include "control_task.h"
#include "encoder_hal.h"
#include "led_hal.h"
#include "motor_hal.h"
#include "telemetry_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * app_main.c
 *
 * Top-level system bring-up for Project C.
 *
 * Day 3-4 already had the core motor/encoder/control/command pieces:
 *   motor HAL -> encoder HAL -> 1 kHz control task -> TCP command server.
 *
 * Later checkpoints add:
 *   - ADC current-sense HAL for DRV8870EVM ISEN -> ESP8266 A0.
 *   - LED HAL on GPIO2 for visible state/fault feedback.
 *   - Safety monitor inside the 1 kHz control loop.
 *   - Binary telemetry TCP server on port 5006.
 *
 * Keep this file simple: it should only define the startup order. Hardware
 * details stay in HAL files, control logic stays in control_task.c, safety
 * thresholds stay in safety_monitor.c, and sockets stay in comm files.
 */

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Project C motor-control firmware starting");

    /*
     * HAL bring-up.
     *
     * These functions configure only their own hardware blocks:
     *   motor_hal_init()   -> PWM GPIOs for DRV8870 IN1/IN2.
     *   encoder_hal_init() -> encoder A/B GPIO inputs and interrupt counting.
     *   adc_hal_init()     -> ESP8266 ADC input used for ISEN current estimate.
     *   led_hal_init()     -> GPIO2 onboard LED used for state/fault patterns.
     */
    motor_hal_init();
    encoder_hal_init();
    adc_hal_init();
    led_hal_init();

    /*
     * Start Wi-Fi/command handling before the high-rate control task.
     *
     * During testing, STA association was more reliable when the ESP8266 joined
     * the router first, then started the 1 kHz control/safety work. The command
     * server itself blocks until it has a valid IP, then opens TCP port 5005.
     */
    command_server_start();

    ESP_LOGI(TAG, "Waiting 15 s for STA Wi-Fi before starting 1 kHz control task");
    vTaskDelay(pdMS_TO_TICKS(15000));

    /*
     * Start real-time control/safety first, then the observer-only telemetry
     * stream. telemetry_server.c only reads snapshots from control_task.c; it
     * never directly changes motor state.
     */
    control_task_start();
    telemetry_server_start();

    ESP_LOGI(TAG, "System bring-up complete");
}
