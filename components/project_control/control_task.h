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

typedef enum
{
    CONTROL_DISARMED = 0,
    CONTROL_ARMED,
    CONTROL_RUNNING
} control_state_t;

typedef struct
{
    control_state_t state;
    int32_t cmd;
    int32_t target;
    int32_t actual;
    int32_t duty;
    int32_t error;
    int32_t delta;
    uint32_t ticks;
    uint32_t missed;
    int step_active;
} control_status_t;

void control_task_start(void);

void control_arm(void);
void control_disarm(void);
void control_stop(void);
int  control_set_speed(int32_t rpm);
void control_start_step_test(void);

control_status_t control_get_status(void);

const char *control_state_name(control_state_t state);

#endif