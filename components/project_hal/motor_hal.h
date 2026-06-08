#ifndef MOTOR_HAL_H
#define MOTOR_HAL_H

#include <stdint.h>

/*
 * motor_hal.h
 *
 * Public HAL interface for the DRV8870 motor driver.
 *
 * HAL rule:
 *   Higher-level code should call motor_hal_set_duty().
 *   Higher-level code should not directly call pwm_set_duty(),
 *   know GPIO numbers, or know DRV8870 IN1/IN2 implementation details.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MOTOR_HAL_OK = 0,
    MOTOR_HAL_ERR = -1
} motor_hal_status_t;

/*
 * Initialize ESP8266 PWM channels used for DRV8870 IN1/IN2.
 *
 * Return:
 *   MOTOR_HAL_OK  = PWM initialized successfully
 *   MOTOR_HAL_ERR = PWM driver setup failed
 */
motor_hal_status_t motor_hal_init(void);

/*
 * Set signed motor duty.
 *
 * duty_percent:
 *   +1 to +100  = forward PWM on IN1, IN2 low
 *   -1 to -100  = reverse PWM on IN2, IN1 low
 *   0           = both inputs low, motor coasts/stops
 *
 * Return:
 *   MOTOR_HAL_OK  = duty applied successfully
 *   MOTOR_HAL_ERR = PWM update failed
 */
motor_hal_status_t motor_hal_set_duty(int32_t duty_percent);

/*
 * Convenience stop helper.
 * Used when firmware wants to force both motor inputs low.
 */
void motor_hal_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MOTOR_HAL_H */