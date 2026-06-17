#include "control_task.h"

#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
 * control_task.c
 *
 * Closed-loop velocity controller plus safety state machine.
 *
 * Day 3-4 responsibility:
 *   - run the 1 kHz timer/semaphore control loop,
 *   - calculate actual RPM from encoder feedback,
 *   - slew the requested setpoint,
 *   - apply feed-forward + PID trim,
 *   - drive the DRV8870 motor HAL.
 *
 * Day 5-6 additions in this file:
 *   - CONTROL_FAULT state is part of the same state machine as IDLE/ARMED/RUNNING,
 *   - ADC current is sampled and included in STATUS/telemetry,
 *   - safety_monitor_update() is called from the 1 kHz task,
 *   - FAULT latches immediately disable PWM and reset PID memory,
 *   - GPIO2 LED patterns are updated from the same state machine.
 *
 * Important architecture:
 *   The timer ISR only releases a binary semaphore. The ISR does not run PID,
 *   print logs, allocate memory, read sockets, or perform ADC work. The FreeRTOS
 *   task that wakes on the semaphore owns the control/safety flow.
 *
 * The 1 kHz task is the single source of truth for IDLE/ARMED/RUNNING/FAULT.
 */

/* 1 kHz ISR -> binary semaphore -> task. ISR does no PID/logging. */
#define CONTROL_PERIOD_US       1000

/*
 * RPM is calculated every SPEED_SAMPLE_MS instead of every 1 ms.
 * A longer window reduces encoder quantization noise and makes low-speed
 * readings such as 300 RPM more stable.
 */
#define SPEED_SAMPLE_MS         250

#define NORMAL_TELEMETRY_MS     1000
#define STEP_TELEMETRY_MS       100

/*
 * ESP8266 Wi-Fi is sensitive to long/high-frequency ADC work.
 * Keep the 1 kHz control/state-machine tick, but only sample A0 current
 * every 10 ms. Safety logic still runs every 1 ms using the most recent
 * current sample, and telemetry still reports the latest current value.
 */
#define ADC_SAMPLE_MS           10

/* Calibrated encoder scale used for RPM conversion during final testing. */
#define ENCODER_TICKS_PER_REV   110

/* Slew limit: target changes by 2 RPM per 1 ms control tick, about 500 ms per 1000 RPM step. */
#define SLEW_RPM_PER_MS         2

/* Feed-forward base duty; PID adds a small trim. */
#define DUTY_STATIC_BOOST       45
#define DUTY_PER_RPM_NUM        30
#define DUTY_PER_RPM_DEN        1000

/*
 * One firmware for all Day 3-4 speeds.
 * Low-speed commands need a softer start assist so 300 RPM does not launch,
 * overspeed, and get cut to zero. Mid/high speeds keep the normal kick.
 */
#define LOW_SPEED_RPM_MAX           400
#define MID_SPEED_RPM_MAX           1000

#define LOW_START_KICK_DUTY         75
#define LOW_START_KICK_MS           600

#define MID_START_KICK_DUTY         90
#define MID_START_KICK_MS           400

#define NORMAL_START_KICK_DUTY      100
#define NORMAL_START_KICK_MS        500

#define LOW_HOLD_DUTY_MIN           50
#define LOW_HOLD_DUTY_MAX           58

#define MID_HOLD_DUTY_MIN           60
#define MID_HOLD_DUTY_MAX           85

#define HIGH_HOLD_DUTY_MIN          80
#define HIGH_HOLD_DUTY_MAX          100

#define REKICK_ZERO_MS              3000


#define STEP_BASELINE_MS        5000
#define STEP_RESPONSE_MS        10000

static const char *TAG = "control_task";

/* Semaphore released by timer_isr() and consumed by control_task(). */
static SemaphoreHandle_t s_sem;

/* Single PID instance used as a trim around feed-forward duty. */
static pid_controller_t s_pid;

/*
 * Shared controller state.
 *
 * volatile is used because several values are touched from command-server task,
 * control task, and/or timer ISR context. It prevents the compiler from
 * caching these values in a way that would hide updates between contexts.
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
static volatile uint8_t s_fault_flags = SAFETY_FAULT_NONE;


/* Start-assist state used to overcome static friction when starting from rest. */
static volatile uint32_t s_start_kick_ms = 0;
static volatile int32_t s_start_kick_duty = NORMAL_START_KICK_DUTY;
static volatile uint32_t s_duty_tick = 0;


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
    s_duty_tick = 0;


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
 * IRAM_ATTR places the ISR in instruction RAM, which is standard practice for
 * interrupt handlers on ESP8266/ESP-IDF style platforms.
 *
 * The ISR only releases a semaphore and optionally requests a context switch.
 * It does not run PID or log messages.
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

    /*
    * If a nonzero speed is commanded but the encoder reports zero for several
    * consecutive speed samples, the motor is likely stuck below its starting
    * torque. Re-arm a short kick. This is still Day 3-4 start-assist logic,
    * not the Day 5-6 STALL fault yet.
    */
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


    int kick_active = ((s_target > 0) &&
                   (s_start_kick_ms > 0));


    if (s_target > 0)
    {
        if (kick_active)
        {
            /*
            * Startup kick is allowed to exceed the steady-state duty cap briefly.
            * The cap is for normal PID holding after the motor is already moving.
            */
            s_duty = clamp_i32(s_start_kick_duty, 0, 100);
        }

        else
        {
            int32_t min_duty = 0;
            int32_t max_duty = 0;

            hold_limits_for_target(s_target, &min_duty, &max_duty);

            /*
            * Continuous hold mode:
            * If a positive RPM is commanded, keep applying a small PWM floor.
            * This avoids burst-stop-burst behavior.
            */
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
    ESP_LOGI(TAG,
             "pid_telemetry t_ms=%lu state=%s cmd=%ld target=%ld actual=%ld duty=%ld current_ma=%ld fault=0x%02x fault_name=%s error=%ld delta=%ld missed=%lu enc_a=%d enc_b=%d",
             (unsigned long)s_ticks,
             control_state_name(s_state),
             (long)s_cmd,
             (long)s_target,
             (long)s_actual,
             (long)s_duty,
             (long)s_current_ma,
             (unsigned)s_fault_flags,
             safety_monitor_fault_name(s_fault_flags),
             (long)(s_target - s_actual),
             (long)s_delta,
             (unsigned long)s_missed,
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
 * This task is the real control loop. It blocks on the binary semaphore until
 * timer_isr() wakes it, then updates timing counters and performs scheduled
 * work at 1 ms, SPEED_SAMPLE_MS, and telemetry intervals.
 */
static void control_task(void *arg)
{
    (void)arg;

    uint32_t speed_ms = 0;
    uint32_t normal_log_ms = 0;
    uint32_t step_log_ms = 0;
    uint32_t adc_sample_ms = 0;

    encoder_hal_get_and_reset_delta();

    while (1)
    {
        xSemaphoreTake(s_sem, portMAX_DELAY);

        s_ticks++;
        speed_ms++;
        normal_log_ms++;
        step_log_ms++;
        adc_sample_ms++;


        if (adc_sample_ms >= ADC_SAMPLE_MS)
        {
            adc_sample_ms = 0;
            s_current_ma = adc_hal_read_current_ma();
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

        if (speed_ms >= SPEED_SAMPLE_MS)
        {
            speed_ms = 0;
            run_speed_sample();
        }

        if (normal_log_ms >= NORMAL_TELEMETRY_MS)
        {
            normal_log_ms = 0;
            log_status();
        }

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
    s_sem = xSemaphoreCreateBinary();
    if (s_sem == NULL)
    {
        ESP_LOGE(TAG, "xSemaphoreCreateBinary failed");
        return;
    }

    /* PI controller: no derivative because encoder RPM is quantized. */
    pid_init(&s_pid, 0.004f, 0.010f, 0.000f, 0.200f, -10.0f, 10.0f);

    safety_monitor_init();

    xTaskCreate(control_task, "control_task", 4096, NULL, 6, NULL);

    hw_timer_init(timer_isr, NULL);
    hw_timer_alarm_us(CONTROL_PERIOD_US, true);

    ESP_LOGI(TAG, "1 kHz control loop + Day 5-6 safety monitor started");
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
    s_missed = 0;
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
    s_duty_tick = 0;

    pid_reset(&s_pid);
    encoder_hal_get_and_reset_delta();

    if (rpm != 0)
    {
        s_state = CONTROL_RUNNING;

        if (from_stopped)
        {
            /*
            * Use start assist only when starting from rest.
            * For 1500 -> 800 -> 300 transitions, avoid re-kicking because it causes
            * overspeed spikes and unstable behavior.
            */
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
    s_duty_tick = 0;


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

    st.state = s_state;
    st.cmd = s_cmd;
    st.target = s_target;
    st.actual = s_actual;
    st.duty = s_duty;
    st.error = s_target - s_actual;
    st.delta = s_delta;
    st.current_ma = s_current_ma;
    st.fault_flags = s_fault_flags;
    st.ticks = s_ticks;
    st.missed = s_missed;
    st.step_active = s_step_active;

    return st;
}

/*
 * Select continuous hold-duty limits by speed range.
 *
 * These limits were tuned to prevent low-speed stalling and high-speed runaway.
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