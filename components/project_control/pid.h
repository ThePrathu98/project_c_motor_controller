#ifndef PID_H
#define PID_H

/*
 * pid.h
 *
 * Pure C PID controller interface.
 *
 * This file contains no ESP8266-specific APIs, so the controller math stays
 * portable and separate from hardware code.
 *
 * Important naming note:
 *   Do NOT name this type pid_t.
 *   pid_t is already a standard system typedef for process IDs.
 *   Used pid_controller_t to avoid conflict with ESP8266 SDK headers.
 */

typedef struct
{
    /* PID gains; kaw is the back-calculation anti-windup gain. */
    float kp;
    float ki;
    float kd;
    float kaw;

    /* Output clamp for the duty correction returned by pid_update(). */
    float out_min;
    float out_max;

    /* Internal state reset before a new independent motor command. */
    float integrator;
    float prev_error;
    int initialized;
} pid_controller_t;

/* Initialize gains, output limits, and internal state. */
void pid_init(pid_controller_t  *pid,
              float kp,
              float ki,
              float kd,
              float kaw,
              float out_min,
              float out_max);

/*
 * Clear integrator and previous error.
 * Used when stopping/disarming so old integral buildup does not restart motor.
 */
void pid_reset(pid_controller_t  *pid);

/* Run one update using desired RPM, measured RPM, and sample period in seconds. */
float pid_update(pid_controller_t  *pid, float setpoint, float measurement, float dt_sec);


/*
 * Return the current integrator term.
 * Day 9 uses this in STATUS/logs to prove anti-windup during a deliberate stall.
 */
float pid_get_integrator(const pid_controller_t *pid);

#endif
