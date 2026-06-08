#ifndef PID_H
#define PID_H

/*
 * pid.h
 *
 * Pure C PID controller interface.
 *
 * Important naming note:
 *   Do NOT name this type pid_t.
 *   pid_t is already a standard system typedef for process IDs.
 *   We use pid_controller_t to avoid conflict with ESP8266 SDK headers.
 */

typedef struct
{
    /*
     * PID gains:
     *   kp: proportional gain
     *   ki: integral gain
     *   kd: derivative gain
     *   kaw: anti-windup back-calculation gain
     */
    float kp;
    float ki;
    float kd;
    float kaw;

    /*
     * Output clamp.
     * For this project, output is a motor duty correction in percent.
     */
    float out_min;
    float out_max;

    /*
     * Internal state.
     * These are kept inside the struct so each PID instance owns its memory.
     */
    float integrator;
    float prev_error;
    int initialized;
} pid_controller_t;

/*
 * Initialize PID object.
 *
 * pid:
 *   Pointer to PID object owned by caller.
 *
 * kp/ki/kd/kaw:
 *   Tuning gains.
 *
 * out_min/out_max:
 *   Clamp range for controller output.
 */
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

/*
 * Run one PID update.
 *
 * setpoint:
 *   Desired RPM.
 *
 * measurement:
 *   Actual RPM.
 *
 * dt_sec:
 *   Control update period in seconds.
 */
float pid_update(pid_controller_t  *pid, float setpoint, float measurement, float dt_sec);

#endif