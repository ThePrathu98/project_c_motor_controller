#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>

/*
 * control_task.h
 *
 * Public API for the Day 3-4 velocity controller.
 * Command-server code calls these functions instead of touching motor/encoder
 * HAL directly.
 */

/*
 * Controller state machine:
 *
 * DISARMED: SET_SPEED is rejected and duty is forced to zero.
 * ARMED:    Controller is enabled, but no nonzero speed is currently running.
 * RUNNING:  A nonzero target is active and motor duty is being updated.
 */
typedef enum
{
    CONTROL_DISARMED = 0,
    CONTROL_ARMED,
    CONTROL_RUNNING
} control_state_t;

/*
 * Snapshot returned by control_get_status().
 *
 * This is what command_server.c formats into the PowerShell STATUS response.
 */
typedef struct
{
    control_state_t state;  /* DISARMED / ARMED / RUNNING */
    int32_t cmd;            /* Requested RPM from TCP command. */
    int32_t target;         /* Slew-limited internal target RPM. */
    int32_t actual;         /* Encoder-measured RPM. */
    int32_t duty;           /* Signed motor duty command in percent. */
    int32_t error;          /* target - actual. */
    int32_t delta;          /* Encoder count delta from last sample. */
    uint32_t ticks;         /* 1 ms control-task tick count. */
    uint32_t missed;        /* Missed semaphore/timer events counter. */
    int step_active;        /* Nonzero when internal STEP_TEST is active. */
} control_status_t;

/* Start the FreeRTOS control task and 1 kHz hardware timer. */
void control_task_start(void);

/* Command API used by command_server.c. */
void control_arm(void);
void control_disarm(void);
void control_stop(void);
int  control_set_speed(int32_t rpm);
void control_start_step_test(void);

/* Read current control state for STATUS responses. */
control_status_t control_get_status(void);

/* Convert control_state_t to a printable string. */
const char *control_state_name(control_state_t state);

#endif