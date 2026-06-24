#include "motor_hal.h"

#include <stdlib.h>

#include "esp_err.h"
#include "esp_log.h"
#include "driver/pwm.h"

/*
 * DRV8870EVM motor HAL.
 *
 * The control task sends a signed duty percentage; this file maps it to
 * ESP8266 PWM channels on IN1/IN2.
 */

/*
 * Final Day 9 wiring:
 *
 *   ESP8266 D5 / GPIO14 -> DRV8870 IN1
 *   ESP8266 D6 / GPIO12 -> DRV8870 IN2
 */
#define MOTOR_IN1_GPIO       14U
#define MOTOR_IN2_GPIO       12U

/* 50 us period = 20 kHz PWM. */
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

/*
 * Initialize ESP8266 PWM for both DRV8870 input pins.
 *
 * pwm_init() receives the period, initial duty array, number of channels, and
 * GPIO pin array. The channels are then started at 0% duty so the motor is safe
 * until ARM/SET_SPEED commands are received.
 */
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

/*
 * Apply a signed duty command.
 *
 * Positive duty: IN1 PWM, IN2 LOW.
 * Negative duty: IN1 LOW, IN2 PWM.
 * Zero duty:     IN1 LOW, IN2 LOW.
 */
motor_hal_status_t motor_hal_set_duty(int32_t duty_percent)
{
    int32_t clamped = duty_percent;

    if (clamped > 100)
    {
        clamped = 100;
    }
    else if (clamped < -100)
    {
        clamped = -100;
    }

    /* ESP8266 PWM driver expects duty in timer counts, not percent. */
    uint32_t duty_count = (uint32_t)((abs(clamped) * MOTOR_PWM_PERIOD_US) / 100);
    esp_err_t err;

    if (clamped > 0)
    {
        /* Forward: PWM on IN1, IN2 low. */
        err = pwm_set_duty(MOTOR_CH_IN1, duty_count);
        if (err != ESP_OK) return MOTOR_HAL_ERR;

        err = pwm_set_duty(MOTOR_CH_IN2, 0);
        if (err != ESP_OK) return MOTOR_HAL_ERR;
    }
    else if (clamped < 0)
    {
        /* Reverse: IN1 low, PWM on IN2. */
        err = pwm_set_duty(MOTOR_CH_IN1, 0);
        if (err != ESP_OK) return MOTOR_HAL_ERR;

        err = pwm_set_duty(MOTOR_CH_IN2, duty_count);
        if (err != ESP_OK) return MOTOR_HAL_ERR;
    }
    else
    {
        /* Stop/coast: both driver inputs low. */
        err = pwm_set_duty(MOTOR_CH_IN1, 0);
        if (err != ESP_OK) return MOTOR_HAL_ERR;

        err = pwm_set_duty(MOTOR_CH_IN2, 0);
        if (err != ESP_OK) return MOTOR_HAL_ERR;
    }

    /* Apply the updated duty values to the PWM hardware. */
    err = pwm_start();
    if (err != ESP_OK) return MOTOR_HAL_ERR;

    return MOTOR_HAL_OK;
}