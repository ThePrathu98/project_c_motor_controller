#include "pid.h"

/*
 * pid.c
 *
 * Pure C PID implementation used by control_task.c.
 *
 * The PID output is not the full motor duty. In the final Day 3-4 tuning,
 * feed-forward supplies the approximate duty and PID returns a small trim.
 */

/*
 * Local helper.
 *
 * static:
 *   File-local function. Other files cannot call this directly.
 */
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
    /*
     * pid is a pointer. The caller owns the actual struct memory.
     * We use pid->field syntax to write into that struct.
     */
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

/*
 * Execute one PID sample.
 *
 * Inputs use floating point because gains and dt are fractional. The caller
 * converts the returned correction back to integer duty percent.
 */
float pid_update(pid_controller_t *pid, float setpoint, float measurement, float dt_sec)
{
    /*
     * error:
     *   Positive error means actual speed is below target speed.
     */
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

    /*
     * Unsaturated PID output before clamp.
     */
    float unsat_output =
        (pid->kp * error) +
        pid->integrator +
        (pid->kd * derivative);

    /*
     * Saturated output.
     * This prevents commanding impossible duty corrections.
     */
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