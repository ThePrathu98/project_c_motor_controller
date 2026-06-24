#include "control_task.h"

#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_system.h"

#include "driver/hw_timer.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "adc_hal.h"
#include "encoder_hal.h"
#include "led_hal.h"
#include "motor_hal.h"
#include "pid.h"
#include "safety_monitor.h"

/*
 * Closed-loop velocity controller and safety state machine.
 *
 * The hardware timer ISR only releases a counting semaphore. All slower work
 * (encoder-to-RPM conversion, PID/feed-forward, ADC current, safety checks,
 * LED state, and PWM updates) runs in this FreeRTOS task. Keeping one task as
 * the owner of IDLE/ARMED/RUNNING/FAULT makes recovery behavior predictable.
 */

/* 1 kHz ISR -> counting semaphore -> task. ISR does no PID/logging. */
#define CONTROL_PERIOD_US       1000

/*
 * Small backlog absorbs brief ESP8266 Wi-Fi/RTOS jitter.
 * missed increments only if this queue overflows, meaning true sustained overload.
 */
#define CONTROL_SEM_DEPTH       8

/* Longer RPM window reduces encoder quantization noise at low speed. */
#define SPEED_SAMPLE_MS         250

/* Set nonzero only when serial PID logs are needed during tuning. */
#define NORMAL_TELEMETRY_MS     0

#define STEP_TELEMETRY_MS       100

/* ADC and LED run below 1 kHz; safety uses the latest sampled current. */
#define ADC_SAMPLE_MS           50
#define LED_UPDATE_MS           50

/* Calibrated encoder scale used for RPM conversion on this bench setup. */
#define ENCODER_TICKS_PER_REV   110

/* Slew limit: target changes by 2 RPM per 1 ms control tick, about 500 ms per 1000 RPM step. */
#define SLEW_RPM_PER_MS         2

/* Feed-forward base duty; PID adds a small trim. */
#define DUTY_STATIC_BOOST       45
#define DUTY_PER_RPM_NUM        30
#define DUTY_PER_RPM_DEN        1000

/* Start-assist and hold limits tuned for continuous 300-1500 RPM motion. */
#define LOW_SPEED_RPM_MAX           400
#define MID_SPEED_RPM_MAX           1000

#define LOW_START_KICK_DUTY         66
#define LOW_START_KICK_MS           400

#define MID_START_KICK_DUTY         75
#define MID_START_KICK_MS           300

#define NORMAL_START_KICK_DUTY      85
#define NORMAL_START_KICK_MS        300

#define LOW_HOLD_DUTY_MIN           50
#define LOW_HOLD_DUTY_MAX           62

#define MID_HOLD_DUTY_MIN           58
#define MID_HOLD_DUTY_MAX           78

#define HIGH_HOLD_DUTY_MIN          80
#define HIGH_HOLD_DUTY_MAX          100

#define REKICK_ZERO_MS              3000

#define STEP_BASELINE_MS        5000
#define STEP_RESPONSE_MS        10000

static const char *TAG = "control_task";

/* Counting semaphore released by timer_isr() and consumed by control_task(). */
static SemaphoreHandle_t s_sem;

/* Single PID instance used as a trim around feed-forward duty. */
static pid_controller_t s_pid;

/*
 * Shared controller state.
 *
 * volatile is used for variables touched by more than one task or ISR context;
 * the control task remains the only place that writes PWM duty.
 */
static volatile control_state_t s_state = CONTROL_IDLE;
static volatile int32_t s_cmd = 0;
static volatile int32_t s_target = 0;
static volatile int32_t s_actual = 0;
static volatile int32_t s_duty = 0;
static volatile int32_t s_delta = 0;
static volatile uint32_t s_ticks = 0;
static volatile uint32_t s_missed = 0;
static volatile int32_t s_current_ma = 0;
static volatile int32_t s_peak_current_ma = 0;
static volatile uint32_t s_heap_start = 0;
static volatile uint32_t s_heap_now = 0;
static volatile uint8_t s_fault_flags = SAFETY_FAULT_NONE;

/* Start-assist state used to overcome static friction when starting from rest. */
static volatile uint32_t s_start_kick_ms = 0;
static volatile int32_t s_start_kick_duty = NORMAL_START_KICK_DUTY;
/* Tracks how long commanded motion has produced zero measured speed. */
static volatile uint32_t s_zero_speed_ms = 0;

/* Internal STEP_TEST state for 500 -> 1500 RPM telemetry. */
static volatile int s_step_active = 0;
static volatile uint32_t s_step_ms = 0;

/* Clamp helper used for safe duty and target limiting. */
static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v > hi) return hi;
    if (v < lo) return lo;
    return v;
}

/* Integer absolute value helper. */
static int32_t abs_i32(int32_t v)
{
    return (v < 0) ? -v : v;
}

/*
 * Free heap snapshot used by STATUS during soak testing.
 * heap_delta = heap_start - heap_now; near zero means no steady heap loss.
 */
static void update_heap_snapshot(void)
{
    s_heap_now = (uint32_t)esp_get_free_heap_size();

    if (s_heap_start == 0)
    {
        s_heap_start = s_heap_now;
    }
}

/* Forward declaration because run_speed_sample() uses this helper before its definition. */
static void hold_limits_for_target(int32_t target_rpm,
                                   int32_t *min_duty,
                                   int32_t *max_duty);

/* Convert enum state to text for STATUS replies and ESP_LOGI telemetry. */
const char *control_state_name(control_state_t state)
{
    switch (state)
    {
        case CONTROL_IDLE:    return "IDLE";
        case CONTROL_ARMED:   return "ARMED";
        case CONTROL_RUNNING: return "RUNNING";
        case CONTROL_FAULT:   return "FAULT";
        default:              return "UNKNOWN";
    }
}

const char *control_fault_name(uint8_t fault_flags)
{
    return safety_monitor_fault_name(fault_flags);
}

/*
 * Convert signed encoder edge count from one sample window into RPM.
 *
 * Formula idea:
 *   revolutions/sample = delta / ticks_per_rev
 *   samples/minute     = 60000 / SPEED_SAMPLE_MS
 *   rpm                = revolutions/sample * samples/minute
 */
static int32_t ticks_to_rpm(int32_t delta)
{
    int32_t corrected = -delta;     /* sign correction for current wiring */
    return (corrected * 60000) / (ENCODER_TICKS_PER_REV * SPEED_SAMPLE_MS);
}

/*
 * Slew-rate limiter for target RPM.
 *
 * s_cmd is the requested command from TCP. s_target is the internal value used
 * by PID. Moving s_target gradually avoids instant torque jumps.
 */
static void update_slew(void)
{
    if (s_target < s_cmd)
    {
        s_target += SLEW_RPM_PER_MS;
        if (s_target > s_cmd) s_target = s_cmd;
    }
    else if (s_target > s_cmd)
    {
        s_target -= SLEW_RPM_PER_MS;
        if (s_target < s_cmd) s_target = s_cmd;
    }
}

/*
 * Estimate the baseline duty needed to run at a requested speed.
 *
 * PID only adds a small trim around this value. This was important for final
 * tuning because pure PID at low speed caused burst-stop-burst behavior.
 */
static int32_t feedforward(int32_t rpm)
{
    if (rpm == 0) return 0;

    int32_t sign = (rpm > 0) ? 1 : -1;
    int32_t duty = DUTY_STATIC_BOOST +
                   ((abs_i32(rpm) * DUTY_PER_RPM_NUM) / DUTY_PER_RPM_DEN);

    return sign * clamp_i32(duty, 0, 100);
}

/*
 * Choose a short startup kick based on requested speed.
 *
 * Low speed needs enough torque to break static friction, but not such a large
 * kick that 300 RPM overshoots badly. Mid/high speeds can tolerate stronger
 * start assist.
 */
static void arm_start_kick_for_rpm(int32_t rpm)
{
    int32_t rpm_abs = abs_i32(rpm);

    if (rpm_abs <= LOW_SPEED_RPM_MAX)
    {
        s_start_kick_duty = LOW_START_KICK_DUTY;
        s_start_kick_ms = LOW_START_KICK_MS;
    }
    else if (rpm_abs <= MID_SPEED_RPM_MAX)
    {
        s_start_kick_duty = MID_START_KICK_DUTY;
        s_start_kick_ms = MID_START_KICK_MS;
    }
    else
    {
        s_start_kick_duty = NORMAL_START_KICK_DUTY;
        s_start_kick_ms = NORMAL_START_KICK_MS;
    }
}

/*
 * Force the controller and motor command back to a safe stopped state.
 *
 * This resets setpoints, duty, start-assist state, PID memory, and finally
 * commands 0% PWM through the motor HAL.
 */
static void stop_now(void)
{
    s_cmd = 0;
    s_target = 0;
    s_duty = 0;

    s_start_kick_ms = 0;
    s_start_kick_duty = NORMAL_START_KICK_DUTY;
    s_zero_speed_ms = 0;
    pid_reset(&s_pid);
    motor_hal_set_duty(0);
}

/*
 * Latch a safety fault into the controller state machine.
 *
 * safety_monitor.c only detects fault conditions and returns fault bits.
 * This function is where the control layer reacts:
 *   1. save the fault flags for STATUS and telemetry,
 *   2. move to CONTROL_FAULT,
 *   3. force duty to zero,
 *   4. immediately command motor_hal_set_duty(0),
 *   5. reset PID so recovery starts cleanly after CLEAR_FAULT.
 */
static void latch_fault_if_needed(uint8_t faults)
{
    if ((faults != SAFETY_FAULT_NONE) && (s_state != CONTROL_FAULT))
    {
        s_fault_flags = faults;
        s_state = CONTROL_FAULT;
        s_duty = 0;
        motor_hal_set_duty(0);
        pid_reset(&s_pid);

        ESP_LOGE(TAG,
                 "FAULT latched flags=0x%02x first=%s current_ma=%ld actual=%ld duty=%ld",
                 (unsigned)s_fault_flags,
                 safety_monitor_fault_name(s_fault_flags),
                 (long)s_current_ma,
                 (long)s_actual,
                 (long)s_duty);
    }
}

/*
 * Internal step-test sequencer used for the required 500 -> 1500 RPM evidence.
 *
 * It first commands 500 RPM for STEP_BASELINE_MS, then commands 1500 RPM for
 * STEP_RESPONSE_MS, then stops the motor automatically.
 */
static void update_step_test(void)
{
    if (!s_step_active) return;

    s_step_ms++;

    if (s_step_ms < STEP_BASELINE_MS)
    {
        s_cmd = 500;
    }
    else if (s_step_ms < (STEP_BASELINE_MS + STEP_RESPONSE_MS))
    {
        s_cmd = 1500;
    }
    else
    {
        s_step_active = 0;
        control_stop();
        ESP_LOGI(TAG, "step_test complete");
    }
}

/*
 * 1 kHz hardware timer ISR.
 *
 * IRAM_ATTR keeps the ISR callable from instruction RAM. The ISR only releases
 * one semaphore token; all PID, ADC, safety, and logging work stays in task context.
 */
static void IRAM_ATTR timer_isr(void *arg)
{
    (void)arg;

    BaseType_t hp_task_woken = pdFALSE;

    if ((s_sem != NULL) &&
        (xSemaphoreGiveFromISR(s_sem, &hp_task_woken) != pdTRUE))
    {
        s_missed++;
    }

    if (hp_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

/*
 * Run one lower-rate velocity-control sample.
 *
 * This function is called from the FreeRTOS control task every
 * SPEED_SAMPLE_MS. It is where encoder counts become RPM and RPM error becomes
 * a PWM duty command.
 */
static void run_speed_sample(void)
{
    if (s_start_kick_ms >= SPEED_SAMPLE_MS)
    {
        s_start_kick_ms -= SPEED_SAMPLE_MS;
    }
    else
    {
        s_start_kick_ms = 0;
    }

    /* Read encoder edges accumulated since the previous speed sample. */
    s_delta = encoder_hal_get_and_reset_delta();

    /* Convert encoder delta into signed RPM for feedback control. */
    s_actual = ticks_to_rpm(s_delta);

    /* Re-kick only after sustained zero-speed feedback while RUNNING. */
    if ((s_state == CONTROL_RUNNING) && (s_target > 0) && (s_actual == 0))
    {
        if (s_zero_speed_ms < REKICK_ZERO_MS)
        {
            s_zero_speed_ms += SPEED_SAMPLE_MS;
        }
        else
        {
            arm_start_kick_for_rpm(s_target);
            s_zero_speed_ms = 0;
            pid_reset(&s_pid);
        }
    }
    else
    {
        s_zero_speed_ms = 0;
    }

    if ((s_state == CONTROL_IDLE) || (s_state == CONTROL_FAULT) || ((s_cmd == 0) && (s_target == 0)))
    {
        if (s_state == CONTROL_FAULT)
        {
            s_duty = 0;
            motor_hal_set_duty(0);
        }
        else
        {
            stop_now();
            if (s_state != CONTROL_IDLE) s_state = CONTROL_ARMED;
        }
        return;
    }

    s_state = CONTROL_RUNNING;

    /*
     * Requested duty = feed-forward estimate + PID correction.
     *
     * Feed-forward supplies most of the running torque. PID only trims the
     * remaining error, which prevents aggressive on/off behavior.
     */
    int32_t requested = feedforward(s_target) +
                        (int32_t)pid_update(&s_pid,
                                            (float)s_target,
                                            (float)s_actual,
                                            ((float)SPEED_SAMPLE_MS / 1000.0f));

    int kick_active = ((s_target > 0) && (s_start_kick_ms > 0));

    if (s_target > 0)
    {
        if (kick_active)
        {
            /* Startup kick can exceed the steady-state hold cap briefly. */
            s_duty = clamp_i32(s_start_kick_duty, 0, 100);
        }

        else
        {
            int32_t min_duty = 0;
            int32_t max_duty = 0;

            hold_limits_for_target(s_target, &min_duty, &max_duty);

            /* Continuous hold mode avoids burst-stop-burst low-speed motion. */
            s_duty = clamp_i32(requested, min_duty, max_duty);
        }
    }

    else if (s_target < 0)
    {
        s_duty = clamp_i32(requested, -HIGH_HOLD_DUTY_MAX, 0);
    }

    else
    {
        s_duty = 0;
    }

    motor_hal_set_duty(s_duty);
}

/* Human-readable telemetry used by Git Bash serial logging. */
static void log_status(void)
{
    update_heap_snapshot();

    ESP_LOGI(TAG,
             "pid_telemetry t_ms=%lu state=%s cmd=%ld target=%ld actual=%ld duty=%ld current_ma=%ld peak_current_ma=%ld pid_i_x1000=%ld fault=0x%02x fault_name=%s error=%ld delta=%ld missed=%lu heap_start=%lu heap_now=%lu heap_delta=%ld enc_a=%d enc_b=%d",
             (unsigned long)s_ticks,
             control_state_name(s_state),
             (long)s_cmd,
             (long)s_target,
             (long)s_actual,
             (long)s_duty,
             (long)s_current_ma,
             (long)s_peak_current_ma,
             (long)(pid_get_integrator(&s_pid) * 1000.0f),
             (unsigned)s_fault_flags,
             safety_monitor_fault_name(s_fault_flags),
             (long)(s_target - s_actual),
             (long)s_delta,
             (unsigned long)s_missed,
             (unsigned long)s_heap_start,
             (unsigned long)s_heap_now,
             (long)((int32_t)s_heap_start - (int32_t)s_heap_now),
             encoder_hal_read_a(),
             encoder_hal_read_b());
}

/* CSV-style step-test telemetry for plotting target/actual/duty vs time. */
static void log_step_csv(void)
{
    ESP_LOGI(TAG,
             "step_csv,%lu,%ld,%ld,%ld,%ld",
             (unsigned long)s_step_ms,
             (long)s_cmd,
             (long)s_target,
             (long)s_actual,
             (long)s_duty);
}

/*
 * FreeRTOS control task.
 *
 * This task owns the control loop. It drains one timer token per iteration,
 * updates the state machine, and runs lower-rate work using simple counters.
 */
static void control_task(void *arg)
{
    (void)arg;

    uint32_t speed_ms = 0;
    uint32_t normal_log_ms = 0;
    uint32_t step_log_ms = 0;
    uint32_t adc_sample_ms = 0;

    uint32_t led_ms = 0;

    encoder_hal_get_and_reset_delta();

    while (1)
    {
        xSemaphoreTake(s_sem, portMAX_DELAY);

        s_ticks++;
        speed_ms++;
        normal_log_ms++;
        step_log_ms++;
        adc_sample_ms++;
        led_ms++;


        if (adc_sample_ms >= ADC_SAMPLE_MS)
        {
            adc_sample_ms = 0;
            s_current_ma = adc_hal_read_current_ma();

            if (s_current_ma > s_peak_current_ma)
            {
                s_peak_current_ma = s_current_ma;
            }
        }
        

        /*
         * Build the safety monitor input from the latest control state.
         *
         * This is deliberately done inside the 1 kHz task so fault detection and
         * state transitions are synchronized with motor-control updates. The
         * monitor returns latched fault bits; latch_fault_if_needed() converts
         * those bits into CONTROL_FAULT and kills PWM.
         */
        safety_monitor_input_t safety_in = {
            .cmd_rpm = s_cmd,
            .actual_rpm = s_actual,
            .duty_percent = s_duty,
            .current_ma = s_current_ma
        };

        latch_fault_if_needed(safety_monitor_update(&safety_in));

        if (s_state == CONTROL_FAULT)
        {
            /*
             * Keep the bridge disabled every millisecond while in FAULT. This
             * makes FAULT a safe holding state, not a one-time stop command.
             */
            s_duty = 0;
            motor_hal_set_duty(0);
        }
        else
        {
            update_step_test();

            if (s_state == CONTROL_IDLE)
            {
                stop_now();
            }
            else
            {
                update_slew();
            }
        }

        /*
         * LED pattern follows the same state machine used by motor control:
         *   IDLE    -> off
         *   ARMED   -> slow blink
         *   RUNNING -> solid
         *   FAULT   -> fast blink
         *
         * This gives a hardware-visible state indication even when the host
         * terminal or GUI is not connected.
         */

        if (led_ms >= LED_UPDATE_MS)
        {
            led_ms = 0;

            switch (s_state)
            {
                case CONTROL_IDLE:
                    led_hal_update(LED_HAL_OFF, s_ticks);
                    break;

                case CONTROL_ARMED:
                    led_hal_update(LED_HAL_SLOW_BLINK, s_ticks);
                    break;

                case CONTROL_RUNNING:
                    led_hal_update(LED_HAL_SOLID, s_ticks);
                    break;

                case CONTROL_FAULT:
                    led_hal_update(LED_HAL_FAST_BLINK, s_ticks);
                    break;

                default:
                    led_hal_update(LED_HAL_OFF, s_ticks);
                    break;
            }
        }

        if (speed_ms >= SPEED_SAMPLE_MS)
        {
            speed_ms = 0;
            run_speed_sample();
        }


        #if NORMAL_TELEMETRY_MS > 0
        if (normal_log_ms >= NORMAL_TELEMETRY_MS)
        {
            normal_log_ms = 0;
            log_status();
        }
        #endif

        if (s_step_active && (step_log_ms >= STEP_TELEMETRY_MS))
        {
            step_log_ms = 0;
            log_step_csv();
        }
    }
}

/*
 * Create the semaphore/task, initialize PID, and start the 1 kHz hardware timer.
 * Called once from app_main().
 */
void control_task_start(void)
{
    s_sem = xSemaphoreCreateCounting(CONTROL_SEM_DEPTH, 0);

    if (s_sem == NULL)
    {
        ESP_LOGE(TAG, "xSemaphoreCreateCounting failed");
        return;
    }

    /*
     * Final Day 9 PI trim around feed-forward duty.
     * Kp was backed off from the overshooting 0.004 test point; Ki removes most
     * steady-state error. Kd stays zero because the encoder RPM estimate is quantized.
     */
    pid_init(&s_pid, 0.0025f, 0.004f, 0.000f, 0.200f, -10.0f, 10.0f);

    update_heap_snapshot();
    safety_monitor_init();

    xTaskCreate(control_task, "control_task", 4096, NULL, 8, NULL);

    hw_timer_init(timer_isr, NULL);
    hw_timer_alarm_us(CONTROL_PERIOD_US, true);

    ESP_LOGI(TAG, "1 kHz control loop + safety monitor started");
}

/* Public command API: enable the controller but keep motor stopped. */
void control_arm(void)
{
    if (s_state == CONTROL_FAULT)
    {
        ESP_LOGW(TAG, "ARM rejected while FAULT is latched");
        return;
    }

    s_state = CONTROL_ARMED;
    s_step_active = 0;
    stop_now();

    /*
     * Day 9 soak counters start fresh at ARM so STATUS after the 10-minute run
     * gives direct evidence for missed deadlines, peak current, and heap delta.
     */
    s_missed = 0;
    s_peak_current_ma = 0;

    s_heap_start = (uint32_t)esp_get_free_heap_size();
    s_heap_now = s_heap_start;

    ESP_LOGI(TAG, "ARM");
}

/* Public command API: disable the controller and force motor stopped. */
void control_disarm(void)
{
    if (s_state != CONTROL_FAULT)
    {
        s_state = CONTROL_IDLE;
    }

    s_step_active = 0;
    if (s_state != CONTROL_FAULT)
    {
        stop_now();
    }
    else
    {
        s_duty = 0;
        motor_hal_set_duty(0);
    }
    ESP_LOGI(TAG, "DISARM");
}

/* Public command API: stop motor but remain in ARMED state. */
void control_stop(void)
{
    s_step_active = 0;
    stop_now();

    if ((s_state != CONTROL_IDLE) && (s_state != CONTROL_FAULT))
    {
        s_state = CONTROL_ARMED;
    }

    ESP_LOGI(TAG, "STOP");
}

/*
 * Public command API: request a signed RPM target.
 *
 * Return values are intentionally small integers because command_server.c maps
 * them directly into text responses.
 */
int control_set_speed(int32_t rpm)
{
    if (s_state == CONTROL_FAULT) return -3;
    if (s_state == CONTROL_IDLE) return -1;
    if ((rpm < -2000) || (rpm > 2000)) return -2;

    s_step_active = 0;

    int from_stopped = ((s_cmd == 0) && (s_target == 0));

    s_cmd = rpm;
    pid_reset(&s_pid);
    encoder_hal_get_and_reset_delta();

    if (rpm != 0)
    {
        s_state = CONTROL_RUNNING;

        if (from_stopped)
        {
            /* Kick only from rest; speed-to-speed changes should not re-launch. */
            arm_start_kick_for_rpm(rpm);
        }
        else
        {
            s_start_kick_ms = 0;
        }
    }
    else
    {
        s_start_kick_ms = 0;
    }

    ESP_LOGI(TAG, "SET_SPEED %ld", (long)rpm);
    return 0;
}

/* Public command API: start internal 500 -> 1500 RPM step test. */
void control_start_step_test(void)
{
    if (s_state == CONTROL_FAULT)
    {
        ESP_LOGW(TAG, "STEP_TEST rejected while FAULT is latched");
        return;
    }

    if (s_state == CONTROL_IDLE)
    {
        s_state = CONTROL_ARMED;
    }

    s_step_active = 1;
    s_step_ms = 0;
    s_cmd = 500;
    s_target = 500;

    arm_start_kick_for_rpm(s_cmd);
    pid_reset(&s_pid);
    encoder_hal_get_and_reset_delta();

    ESP_LOGI(TAG, "STEP_TEST 500 -> 1500 started");
}

int control_clear_fault(void)
{
    /*
     * Re-read current at the moment of CLEAR_FAULT instead of relying only on
     * the last 10 ms sampled value. This makes the recovery decision use fresh
     * ADC data before the state machine returns to IDLE.
     */
    s_current_ma = adc_hal_read_current_ma();

    if (safety_monitor_clear_if_safe(s_current_ma) != 0)
    {
        ESP_LOGW(TAG, "CLEAR_FAULT rejected current_ma=%ld", (long)s_current_ma);
        return -1;
    }

    s_fault_flags = SAFETY_FAULT_NONE;
    s_state = CONTROL_IDLE;
    s_step_active = 0;
    stop_now();

    ESP_LOGI(TAG, "CLEAR_FAULT");
    return 0;
}

/*
 * Snapshot current controller variables for STATUS replies.
 *
 * The struct copy gives command_server.c one consistent group of values without
 * exposing the static globals directly.
 */
control_status_t control_get_status(void)
{
    control_status_t st;

    update_heap_snapshot();

    st.state = s_state;
    st.cmd = s_cmd;
    st.target = s_target;
    st.actual = s_actual;
    st.duty = s_duty;
    st.error = s_target - s_actual;
    st.delta = s_delta;
    st.current_ma = s_current_ma;
    st.peak_current_ma = s_peak_current_ma;
    st.pid_i_x1000 = (int32_t)(pid_get_integrator(&s_pid) * 1000.0f);
    st.fault_flags = s_fault_flags;
    st.ticks = s_ticks;
    st.missed = s_missed;
    st.heap_start = s_heap_start;
    st.heap_now = s_heap_now;
    st.heap_delta = (int32_t)s_heap_start - (int32_t)s_heap_now;
    st.step_active = s_step_active;

    return st;
}

/*
 * Select continuous hold-duty limits by speed range.
 *
 * These limits are tuned to prevent low-speed stalling and high-speed runaway.
 * They are the key reason the final motor behavior became continuous instead
 * of burst-stop-burst.
 */
static void hold_limits_for_target(int32_t target_rpm,
                                   int32_t *min_duty,
                                   int32_t *max_duty)
{
    int32_t rpm_abs = abs_i32(target_rpm);

    if (rpm_abs <= LOW_SPEED_RPM_MAX)
    {
        *min_duty = LOW_HOLD_DUTY_MIN;
        *max_duty = LOW_HOLD_DUTY_MAX;
    }
    else if (rpm_abs <= MID_SPEED_RPM_MAX)
    {
        *min_duty = MID_HOLD_DUTY_MIN;
        *max_duty = MID_HOLD_DUTY_MAX;
    }
    else
    {
        *min_duty = HIGH_HOLD_DUTY_MIN;
        *max_duty = HIGH_HOLD_DUTY_MAX;
    }
}