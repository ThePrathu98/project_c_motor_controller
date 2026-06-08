#include "esp_log.h"

#include "command_server.h"
#include "control_task.h"
#include "encoder_hal.h"
#include "motor_hal.h"

/*
 * app_main.c
 *
 * System bring-up only.
 * Control logic is in control_task.c.
 * TCP commands are in command_server.c.
 */

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Project C Day 3-4 full firmware starting");

    motor_hal_init();
    encoder_hal_init();

    control_task_start();
    command_server_start();

    ESP_LOGI(TAG, "System bring-up complete");
}