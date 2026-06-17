#include "safety_monitor.h"

#include "esp_log.h"

/*
 * safety_monitor.c
 *
 * Software safety monitor used by the 1 kHz control task.
 *
 * This module intentionally does not call motor_hal_set_duty(). It only latches
 * fault bits. control_task.c is the single owner of the motor output and state
 * machine, so the project has one clear place where FAULT disables PWM.
 *
 * Fault model:
 *
 *   OVERCURRENT:
 *     If current is above OVERCURRENT_TRIP_MA for OVERCURRENT_TICKS_MS while
 *     the bridge is actually being driven, latch SAFETY_FAULT_OVERCURRENT.
 *
 *   STALL:
 *     If a meaningful command is present, measured RPM remains near zero, and
 *     duty is above the torque-producing range for STALL_TICKS_MS, latch
 *     SAFETY_FAULT_STALL.
 *
 * DRV8870EVM note:
 *   The specific EVM used here exposed a usable ISEN current-sense test pad.
 *   A direct nFAULT GPIO path was not available in the same way as the project
 *   brief describes for newer DRV887x-style boards. Therefore this firmware
 *   implements current-derived OVERCURRENT and encoder-derived STALL in the
 *   software safety monitor, and documents nFAULT as a hardware limitation.
 */

/*
 * Final firmware thresholds.
 *
 * During bench validation, OVERCURRENT was temporarily reduced to prove the
 * fault path safely at no-load current levels. Before committing, the final
 * restored firmware was rebuilt from fullclean and verified with these values.
 */
#define OVERCURRENT_TRIP_MA       900
#define OVERCURRENT_CLEAR_MA      700
#define OVERCURRENT_TICKS_MS      5

#define STALL_CMD_RPM_MIN         200
#define STALL_ACTUAL_RPM_MAX      50
#define STALL_DUTY_MIN_PERCENT    50
#define STALL_TICKS_MS            500

static const char *TAG = "safety_monitor";

static uint8_t s_faults = SAFETY_FAULT_NONE;
static uint32_t s_overcurrent_ticks = 0;
static uint32_t s_stall_ticks = 0;

static int32_t abs_i32(int32_t v)
{
    return (v < 0) ? -v : v;
}

void safety_monitor_init(void)
{
    s_faults = SAFETY_FAULT_NONE;
    s_overcurrent_ticks = 0;
    s_stall_ticks = 0;
}

uint8_t safety_monitor_update(const safety_monitor_input_t *input)
{
    if (input == 0)
    {
        return s_faults;
    }

    /*
     * Faults are latched. Once any fault is active, keep returning the same
     * fault flags until command_server.c calls CLEAR_FAULT, which reaches
     * control_clear_fault() and then safety_monitor_clear_if_safe().
     */
    if (s_faults != SAFETY_FAULT_NONE)
    {
        return s_faults;
    }

    /*
     * OVERCURRENT debounce.
     *
     * The duty guard prevents ADC startup noise or idle A0 noise from becoming
     * an overcurrent fault when PWM is not being applied. Because this function
     * is called from the 1 kHz control task, one increment is approximately
     * one millisecond.
     */
    if ((abs_i32(input->duty_percent) > 5) &&
        (input->current_ma > OVERCURRENT_TRIP_MA))
    {
        if (s_overcurrent_ticks < OVERCURRENT_TICKS_MS)
        {
            s_overcurrent_ticks++;
        }
        else
        {
            s_faults |= SAFETY_FAULT_OVERCURRENT;
            ESP_LOGE(TAG,
                     "FAULT OVERCURRENT current_ma=%ld duty=%ld",
                     (long)input->current_ma,
                     (long)input->duty_percent);
        }
    }
    else
    {
        s_overcurrent_ticks = 0;
    }

    /*
     * STALL detection.
     *
     * A stall is not just "actual RPM is zero." The motor can be stopped in
     * IDLE/ARMED by design. The condition becomes a real stall only when:
     *   - command is above the minimum meaningful RPM,
     *   - encoder feedback remains below the actual-RPM threshold,
     *   - duty is high enough that the controller is trying to create torque,
     *   - the condition persists for STALL_TICKS_MS.
     */
    if ((abs_i32(input->cmd_rpm) > STALL_CMD_RPM_MIN) &&
        (abs_i32(input->actual_rpm) < STALL_ACTUAL_RPM_MAX) &&
        (abs_i32(input->duty_percent) > STALL_DUTY_MIN_PERCENT))
    {
        if (s_stall_ticks < STALL_TICKS_MS)
        {
            s_stall_ticks++;
        }
        else
        {
            s_faults |= SAFETY_FAULT_STALL;
            ESP_LOGE(TAG,
                     "FAULT STALL cmd=%ld actual=%ld duty=%ld",
                     (long)input->cmd_rpm,
                     (long)input->actual_rpm,
                     (long)input->duty_percent);
        }
    }
    else
    {
        s_stall_ticks = 0;
    }

    return s_faults;
}

uint8_t safety_monitor_get_faults(void)
{
    return s_faults;
}

int safety_monitor_clear_if_safe(int32_t current_ma)
{
    /*
     * Do not clear an overcurrent fault while current is still above the clear
     * threshold. This adds hysteresis: trip at 900 mA, allow recovery only after
     * falling below 700 mA.
     */
    if (current_ma > OVERCURRENT_CLEAR_MA)
    {
        return -1;
    }

    s_faults = SAFETY_FAULT_NONE;
    s_overcurrent_ticks = 0;
    s_stall_ticks = 0;
    return 0;
}

const char *safety_monitor_fault_name(uint8_t faults)
{
    /*
     * Human-readable name used in STATUS replies, ESP_LOGI telemetry, and
     * PowerShell/Git Bash evidence logs.
     */
    if (faults & SAFETY_FAULT_OVERCURRENT) return "OVERCURRENT";
    if (faults & SAFETY_FAULT_STALL)       return "STALL";
    if (faults & SAFETY_FAULT_DRIVER)      return "DRIVER";
    return "NONE";
}
