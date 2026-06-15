#include "esp_log.h"

#include "command_server.h"
#include "control_task.h"
#include "encoder_hal.h"
#include "motor_hal.h"

/*
 * app_main.c
 *
 * Top-level system bring-up for Project C Day 3-4.
 *
 * Structural flow:
 *   1. Initialize the DRV8870 PWM motor output layer.
 *   2. Initialize encoder GPIO/interrupt feedback.
 *   3. Start the closed-loop velocity control task.
 *   4. Start the Wi-Fi/TCP command server.
 *
 * Keeping app_main() this small is intentional: hardware details stay in the
 * HAL files, control logic stays in control_task.c, and command parsing stays
 * in command_server.c.
 */

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Project C Day 3-4 full firmware starting");

    /* Configure ESP8266 PWM channels that drive the DRV8870 IN1/IN2 pins. */
    motor_hal_init();

    /* Configure encoder A/B GPIOs and attach the encoder interrupt handler. */
    encoder_hal_init();

    /* Start the 1 kHz timer/semaphore closed-loop velocity control task. */
    control_task_start();

    /* Start Wi-Fi station mode and the TCP command server on port 5005. */
    command_server_start();

    ESP_LOGI(TAG, "System bring-up complete");
}