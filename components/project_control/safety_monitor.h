#ifndef SAFETY_MONITOR_H
#define SAFETY_MONITOR_H

#include <stdint.h>

#define SAFETY_FAULT_NONE         0x00U
#define SAFETY_FAULT_OVERCURRENT  0x01U
#define SAFETY_FAULT_STALL        0x02U
#define SAFETY_FAULT_DRIVER       0x04U

typedef struct
{
    int32_t cmd_rpm;
    int32_t actual_rpm;
    int32_t duty_percent;
    int32_t current_ma;
} safety_monitor_input_t;

void safety_monitor_init(void);
uint8_t safety_monitor_update(const safety_monitor_input_t *input);
uint8_t safety_monitor_get_faults(void);
int safety_monitor_clear_if_safe(int32_t current_ma);
const char *safety_monitor_fault_name(uint8_t faults);

#endif /* SAFETY_MONITOR_H */
