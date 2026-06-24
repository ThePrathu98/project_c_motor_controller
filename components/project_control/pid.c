#include "pid.h"

/*
 * Pure C PID helper used as a trim around feed-forward motor duty.
 * The controller stores its own integrator and previous-error state in the struct.
 */

/* File-local clamp helper for output limiting. */
static float clamp_float(float value, float min_value, float max_value)
{
    if (value > max_value)
    {
        return max_value;
    }

    if (value < min_value)
    {
        return min_value;
    }

    return value;
}

void pid_init(pid_controller_t *pid,
              float kp,
              float ki,
              float kd,
              float kaw,
              float out_min,
              float out_max)
{
    /* Caller owns the struct; this function only initializes its fields. */
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->kaw = kaw;
    pid->out_min = out_min;
    pid->out_max = out_max;

    pid_reset(pid);
}

/*
 * Clear accumulated state.
 *
 * This is called when stopping/disarming or before a new speed command so old
 * integral memory does not affect the next test.
 */
void pid_reset(pid_controller_t *pid)
{
    pid->integrator = 0.0f;
    pid->prev_error = 0.0f;
    pid->initialized = 0;
}

/* Run one PID sample and return a clamped duty correction. */
float pid_update(pid_controller_t *pid, float setpoint, float measurement, float dt_sec)
{
    /* Positive error means actual speed is below target speed. */
    float error = setpoint - measurement;

    /*
     * Derivative term:
     *   Uses error difference between samples.
     *   For first sample, derivative is forced to 0 to avoid a startup spike.
     */
    float derivative = 0.0f;
    if (pid->initialized)
    {
        derivative = (error - pid->prev_error) / dt_sec;
    }
    else
    {
        pid->initialized = 1;
    }

    /* PID output before clamp. */
    float unsat_output =
        (pid->kp * error) +
        pid->integrator +
        (pid->kd * derivative);

    /* Clamp correction to the duty-trim range passed to pid_init(). */
    float sat_output = clamp_float(unsat_output, pid->out_min, pid->out_max);

    /*
     * Anti-windup via back-calculation:
     *
     * If output is saturated, sat_output - unsat_output becomes nonzero.
     * That term pushes the integrator back toward a value that can produce
     * a valid output. This prevents runaway integral buildup during stall
     * or large speed steps.
     */
    pid->integrator +=
        ((pid->ki * error) + (pid->kaw * (sat_output - unsat_output))) * dt_sec;

    pid->prev_error = error;

    return sat_output;
}

float pid_get_integrator(const pid_controller_t *pid)
{
    if (pid == 0)
    {
        return 0.0f;
    }

    return pid->integrator;
}
