#ifndef MOTOR_HAL_H
#define MOTOR_HAL_H

/*
 * motor_hal.h
 *
 * Public interface for the motor hardware abstraction layer.
 *
 * Higher-level application/control code should call these functions
 * instead of directly touching GPIO registers.
 *
 * This keeps the project layered:
 *   app_main/control logic -> motor_hal -> ESP8266 GPIO/PWM driver
 */

void motor_hal_init(void);

/*
 * Set motor command.
 *
 * Current Day 2 bring-up behavior:
 *   duty_percent > 0  -> forward
 *   duty_percent < 0  -> reverse
 *   duty_percent == 0 -> stop/coast
 *
 * Later Day 2/Day 3 behavior:
 *   this same API will be upgraded to real PWM duty control.
 */
void motor_hal_set_duty(int duty_percent);

/*
 * Convenience wrapper for stopping the motor.
 */
void motor_hal_stop(void);

#endif