#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>

/*
 * control_task.h
 *
 * Public API for the velocity controller and safety state machine.
 * Command-server and telemetry-server code call these functions instead of
 * touching motor/encoder/ADC HAL directly.
 */

typedef enum
{
    CONTROL_IDLE = 0,
    CONTROL_ARMED = 1,
    CONTROL_RUNNING = 2,
    CONTROL_FAULT = 3
} control_state_t;

typedef struct
{
    control_state_t state;  /* IDLE / ARMED / RUNNING / FAULT */
    int32_t cmd;            /* Requested RPM from TCP command. */
    int32_t target;         /* Slew-limited internal target RPM. */
    int32_t actual;         /* Encoder-measured RPM. */
    int32_t duty;           /* Signed motor duty command in percent. */
    int32_t error;          /* target - actual. */
    int32_t delta;          /* Encoder count delta from last sample. */
    int32_t current_ma;     /* DRV8870EVM ISEN-derived current estimate. */
    uint8_t fault_flags;    /* SAFETY_FAULT_* bitmask. */
    uint32_t ticks;         /* 1 ms control-task tick count / uptime_ms. */
    uint32_t missed;        /* Missed semaphore/timer events counter. */
    int step_active;        /* Nonzero when internal STEP_TEST is active. */
} control_status_t;

void control_task_start(void);

void control_arm(void);
void control_disarm(void);
void control_stop(void);
int  control_set_speed(int32_t rpm);
void control_start_step_test(void);
int  control_clear_fault(void);

control_status_t control_get_status(void);

const char *control_state_name(control_state_t state);
const char *control_fault_name(uint8_t fault_flags);

#endif /* CONTROL_TASK_H */
