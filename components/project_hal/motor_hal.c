#include "motor_hal.h"

#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "driver/pwm.h"

/*
 * DRV8870 input pins:
 *
 *   ESP8266 D5 / GPIO14 -> DRV8870 IN1
 *   ESP8266 D6 / GPIO12 -> DRV8870 IN2
 */
#define MOTOR_IN1_GPIO       14U
#define MOTOR_IN2_GPIO       12U

/*
 * 50 us period = 20 kHz PWM.
 */
#define MOTOR_PWM_PERIOD_US  50U
#define MOTOR_PWM_CHANNELS   2U

#define MOTOR_CH_IN1         0U
#define MOTOR_CH_IN2         1U

static const char *TAG = "motor_hal";

/*
 * ESP8266 RTOS SDK pwm_init() expects GPIO numbers here.
 */
static const uint32_t s_pwm_pins[MOTOR_PWM_CHANNELS] =
{
    MOTOR_IN1_GPIO,
    MOTOR_IN2_GPIO
};

static uint32_t s_pwm_duties[MOTOR_PWM_CHANNELS] =
{
    0,
    0
};




motor_hal_status_t motor_hal_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing motor PWM");
    ESP_LOGI(TAG, "IN1 GPIO=%u, IN2 GPIO=%u, PWM period=%u us",
             MOTOR_IN1_GPIO,
             MOTOR_IN2_GPIO,
             MOTOR_PWM_PERIOD_US);

    s_pwm_duties[MOTOR_CH_IN1] = 0;
    s_pwm_duties[MOTOR_CH_IN2] = 0;

    err = pwm_init(MOTOR_PWM_PERIOD_US,
                   s_pwm_duties,
                   MOTOR_PWM_CHANNELS,
                   s_pwm_pins);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_init failed, err=%d", err);
        return MOTOR_HAL_ERR;
    }


    err = pwm_set_phase(MOTOR_CH_IN1, 0.0f);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_set_phase IN1 failed, err=%d", err);
        return MOTOR_HAL_ERR;
    }

    err = pwm_set_phase(MOTOR_CH_IN2, 0.0f);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_set_phase IN2 failed, err=%d", err);
        return MOTOR_HAL_ERR;
    }

    err = pwm_set_duty(MOTOR_CH_IN1, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_set_duty IN1 init failed, err=%d", err);
        return MOTOR_HAL_ERR;
    }

    err = pwm_set_duty(MOTOR_CH_IN2, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_set_duty IN2 init failed, err=%d", err);
        return MOTOR_HAL_ERR;
    }

    err = pwm_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_start init failed, err=%d", err);
        return MOTOR_HAL_ERR;
    }

    ESP_LOGI(TAG, "Motor PWM initialized");
    return MOTOR_HAL_OK;
}


motor_hal_status_t motor_hal_set_duty(int32_t duty_percent)
{
    /*
     * motor_hal_set_duty()
     *
     * This function is called frequently by the PID control task.
     * Because it can run every 10 ms, do NOT print ESP_LOGI here.
     * Frequent UART logging from a control path can overflow/delay the system.
     *
     * duty_percent:
     *   + value = drive IN1 PWM, IN2 low
     *   - value = drive IN1 low, IN2 PWM
     *   0       = both inputs low, motor coasts/stops
     */

    int32_t clamped = duty_percent;

    if (clamped > 100)
    {
        clamped = 100;
    }
    else if (clamped < -100)
    {
        clamped = -100;
    }

    /*
     * PWM period is 50 us.
     * So:
     *   100% duty = 50 us HIGH
     *   70% duty  = 35 us HIGH
     *   30% duty  = 15 us HIGH
     */
    uint32_t pwm_count = (uint32_t)((abs(clamped) * MOTOR_PWM_PERIOD_US) / 100);

    esp_err_t err;

    if (clamped > 0)
    {
        /*
         * Forward command:
         *   IN1 gets PWM.
         *   IN2 stays low.
         */
        err = pwm_set_duty(MOTOR_CH_IN1, pwm_count);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pwm_set_duty IN1 failed err=%d", err);
            return MOTOR_HAL_ERR;
        }

        err = pwm_set_duty(MOTOR_CH_IN2, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pwm_set_duty IN2 failed err=%d", err);
            return MOTOR_HAL_ERR;
        }
    }
    else if (clamped < 0)
    {
        /*
         * Reverse command:
         *   IN1 stays low.
         *   IN2 gets PWM.
         */
        err = pwm_set_duty(MOTOR_CH_IN1, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pwm_set_duty IN1 failed err=%d", err);
            return MOTOR_HAL_ERR;
        }

        err = pwm_set_duty(MOTOR_CH_IN2, pwm_count);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pwm_set_duty IN2 failed err=%d", err);
            return MOTOR_HAL_ERR;
        }
    }
    else
    {
        /*
         * Stop/coast command:
         *   Both DRV8870 inputs low.
         */
        err = pwm_set_duty(MOTOR_CH_IN1, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pwm_set_duty IN1 stop failed err=%d", err);
            return MOTOR_HAL_ERR;
        }

        err = pwm_set_duty(MOTOR_CH_IN2, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "pwm_set_duty IN2 stop failed err=%d", err);
            return MOTOR_HAL_ERR;
        }
    }

    /*
     * Apply PWM duty changes.
     * Keep only error logging here; no normal ESP_LOGI.
     */
    err = pwm_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pwm_start failed err=%d", err);
        return MOTOR_HAL_ERR;
    }

    return MOTOR_HAL_OK;
}


void motor_hal_stop(void)
{
    pwm_set_duty(MOTOR_CH_IN1, 0);
    pwm_set_duty(MOTOR_CH_IN2, 0);
    pwm_start();

    ESP_LOGI(TAG, "motor stopped");
}