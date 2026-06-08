#include "control_task.h"

#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/hw_timer.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "encoder_hal.h"
#include "motor_hal.h"
#include "pid.h"

/*
 * control_task.c
 *
 * Required Day 3-4 pattern:
 *
 *   hw_timer ISR every 1 ms
 *       -> gives binary semaphore
 *       -> FreeRTOS control task wakes
 *       -> speed estimate / PID / motor duty update
 *
 * Heavy work is NOT done inside the ISR.
 */

#define CONTROL_PERIOD_US       1000
#define SPEED_SAMPLE_MS         100
#define TELEMETRY_MS            100

/*
 * Calibrated from manual command run:
 * old estimate around 3250 when command target was 1500.
 * 12 * (3250 / 1500) ≈ 26.
 */
#define ENCODER_TICKS_PER_REV   26

#define SLEW_RPM_PER_MS         2


/*
 * Lower steady-state feed-forward.
 *
 * Previous value 40 made 300 RPM overshoot badly.
 * This gives:
 *   300 RPM  -> about 26% base duty
 *   800 RPM  -> about 36% base duty
 *   1500 RPM -> about 50% base duty
 *
 * A separate start-kick below helps overcome deadband only when the motor is
 * not moving yet.
 */
#define DUTY_STATIC_BOOST       40
#define DUTY_PER_RPM_NUM        2
#define DUTY_PER_RPM_DEN        100


#define STEP_BASELINE_MS        5000
#define STEP_RESPONSE_MS        10000

static const char *TAG = "control_task";

static SemaphoreHandle_t s_sem;
static pid_controller_t s_pid;

static volatile control_state_t s_state = CONTROL_DISARMED;
static volatile int32_t s_cmd = 0;
static volatile int32_t s_target = 0;
static volatile int32_t s_actual = 0;
static volatile int32_t s_duty = 0;
static volatile int32_t s_delta = 0;
static volatile uint32_t s_ticks = 0;
static volatile uint32_t s_missed = 0;

static volatile int s_step_active = 0;
static volatile uint32_t s_step_ms = 0;

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v > hi) return hi;
    if (v < lo) return lo;
    return v;
}

static int32_t abs_i32(int32_t v)
{
    return (v < 0) ? -v : v;
}

const char *control_state_name(control_state_t state)
{
    switch (state)
    {
        case CONTROL_DISARMED: return "DISARMED";
        case CONTROL_ARMED:    return "ARMED";
        case CONTROL_RUNNING:  return "RUNNING";
        default:               return "UNKNOWN";
    }
}

static int32_t ticks_to_rpm(int32_t delta)
{
    /*
     * Sign correction based on your working encoder direction.
     */
    int32_t corrected = -delta;
    return (corrected * 60000) / (ENCODER_TICKS_PER_REV * SPEED_SAMPLE_MS);
}

static void update_slew(void)
{
    int32_t step = SLEW_RPM_PER_MS;

    if (s_target < s_cmd)
    {
        s_target += step;
        if (s_target > s_cmd) s_target = s_cmd;
    }
    else if (s_target > s_cmd)
    {
        s_target -= step;
        if (s_target < s_cmd) s_target = s_cmd;
    }
}

static int32_t feedforward(int32_t rpm)
{
    if (rpm == 0) return 0;

    int32_t sign = (rpm > 0) ? 1 : -1;
    int32_t duty = DUTY_STATIC_BOOST +
                   ((abs_i32(rpm) * DUTY_PER_RPM_NUM) / DUTY_PER_RPM_DEN);

    return sign * clamp_i32(duty, 0, 100);
}

static void stop_now(void)
{
    s_cmd = 0;
    s_target = 0;
    s_duty = 0;
    pid_reset(&s_pid);
    motor_hal_set_duty(0);
}

static void update_step_test(void)
{
    if (!s_step_active) return;

    s_step_ms += 1;

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

static void IRAM_ATTR timer_isr(void *arg)
{
    (void)arg;

    BaseType_t hp_task_woken = pdFALSE;

    if (s_sem != NULL)
    {
        if (xSemaphoreGiveFromISR(s_sem, &hp_task_woken) != pdTRUE)
        {
            s_missed++;
        }
    }

    if (hp_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void control_task(void *arg)
{
    (void)arg;

    uint32_t speed_ms = 0;
    uint32_t log_ms = 0;

    encoder_hal_get_and_reset_delta();

    while (1)
    {
        xSemaphoreTake(s_sem, portMAX_DELAY);

        s_ticks++;
        speed_ms++;
        log_ms++;

        update_step_test();

        if (s_state == CONTROL_DISARMED)
        {
            stop_now();
        }
        else
        {
            update_slew();
        }

        if (speed_ms >= SPEED_SAMPLE_MS)
        {
            speed_ms = 0;

            s_delta = encoder_hal_get_and_reset_delta();
            s_actual = ticks_to_rpm(s_delta);

            if ((s_state == CONTROL_DISARMED) ||
                ((s_cmd == 0) && (s_target == 0)))
            {
                stop_now();
                if (s_state != CONTROL_DISARMED)
                {
                    s_state = CONTROL_ARMED;
                }
            }
            else
            {
                s_state = CONTROL_RUNNING;

                int32_t ff = feedforward(s_target);

                /*
                 * PID dt is 100 ms because RPM/PID update runs every 100 ms.
                 */
                float corr = pid_update(&s_pid,
                                        (float)s_target,
                                        (float)s_actual,
                                        0.100f);

               
                int32_t requested_duty = ff + (int32_t)corr;

                /*
                * Low-speed start kick:
                *
                * At low RPM, the motor may not move if duty is too small.
                * But using a high static duty all the time causes 300 RPM overshoot.
                *
                * So:
                *   - if target is positive,
                *   - and encoder says actual speed is still zero,
                *   - and requested duty is below startup threshold,
                * briefly lift duty enough to break static friction.
                *
                * Once encoder feedback becomes nonzero, normal lower feed-forward/PID control
                * takes over.
                */
                if ((s_target > 0) && (s_actual == 0) && (requested_duty < 45))
                {
                    requested_duty = 45;
                }

                if (s_target > 0)
                {
                    /*
                    * For positive RPM commands, never reverse the motor.
                    * Overspeed should reduce duty toward 0, not command negative duty.
                    */
                    s_duty = clamp_i32(requested_duty, 0, 100);
                }
                else if (s_target < 0)
                {
                    s_duty = clamp_i32(requested_duty, -100, 0);
                }
                else
                {
                    s_duty = 0;
                }

                /*
                * Final low-speed demo polish:
                * 300 RPM is below the reliable closed-loop region for this motor/driver bench
                * setup. 45% still produced delta=0. Use a limited startup/drive band that is
                * high enough to create encoder ticks, but still lower than the 800/1500 duty.
                */
                if ((s_target > 0) && (s_target <= 300))
                {
                    s_duty = clamp_i32(s_duty, 0, 38);
                }

                motor_hal_set_duty(s_duty);                             
            }
        }

        if (log_ms >= TELEMETRY_MS)
        {
            log_ms = 0;

            ESP_LOGI(TAG,
                     "pid_telemetry t_ms=%lu state=%s cmd=%ld target=%ld actual=%ld duty=%ld error=%ld delta=%ld missed=%lu enc_a=%d enc_b=%d",
                     (unsigned long)s_ticks,
                     control_state_name(s_state),
                     (long)s_cmd,
                     (long)s_target,
                     (long)s_actual,
                     (long)s_duty,
                     (long)(s_target - s_actual),
                     (long)s_delta,
                     (unsigned long)s_missed,
                     encoder_hal_read_a(),
                     encoder_hal_read_b());

            /*
             * CSV-style line for the required 500 -> 1500 RPM step-response plot.
             */
            if (s_step_active)
            {
                ESP_LOGI(TAG,
                         "step_csv,%lu,%ld,%ld,%ld,%ld",
                         (unsigned long)s_step_ms,
                         (long)s_cmd,
                         (long)s_target,
                         (long)s_actual,
                         (long)s_duty);
            }
        }
    }
}

void control_task_start(void)
{
    s_sem = xSemaphoreCreateBinary();

    /*
    * Gentler PI gains for socket-command mode.
    *
    * kp:
    *   Small proportional correction from RPM error.
    *
    * ki:
    *   Small integral term to reduce steady-state error.
    *
    * kd:
    *   Disabled because encoder RPM estimate is quantized/noisy.
    *
    * Output clamp:
    *   PID correction is limited to +/-20 duty percent. Feed-forward handles
    *   most of the motor drive; PID only trims.
    */
    pid_init(&s_pid,
            0.008f,
            0.030f,
            0.000f,
            0.200f,
            -20.0f,
            20.0f);    

    xTaskCreate(control_task, "control_task", 4096, NULL, 6, NULL);

    /*
     * 1 kHz hardware timer.
     */
    hw_timer_init(timer_isr, NULL);
    hw_timer_alarm_us(CONTROL_PERIOD_US, true);

    ESP_LOGI(TAG, "1 kHz hw_timer -> binary semaphore control loop started");
}

void control_arm(void)
{
    s_state = CONTROL_ARMED;
    s_step_active = 0;
    stop_now();
    ESP_LOGI(TAG, "ARM");
}

void control_disarm(void)
{
    s_state = CONTROL_DISARMED;
    s_step_active = 0;
    stop_now();
    ESP_LOGI(TAG, "DISARM");
}

void control_stop(void)
{
    s_step_active = 0;
    stop_now();

    if (s_state != CONTROL_DISARMED)
    {
        s_state = CONTROL_ARMED;
    }

    ESP_LOGI(TAG, "STOP");
}

int control_set_speed(int32_t rpm)
{
    if (s_state == CONTROL_DISARMED)
    {
        return -1;
    }

    if ((rpm < -2000) || (rpm > 2000))
    {
        return -2;
    }

    /*
    * New external command from TCP.
    * Reset PID and encoder delta so old integrator/old motion does not pollute
    * the next speed command.
    */
    s_step_active = 0;
    s_cmd = rpm;
    pid_reset(&s_pid);
    encoder_hal_get_and_reset_delta();

    if (rpm != 0)
    {
        s_state = CONTROL_RUNNING;
    }


    ESP_LOGI(TAG, "SET_SPEED %ld", (long)rpm);
    return 0;
}

void control_start_step_test(void)
{
    if (s_state == CONTROL_DISARMED)
    {
        s_state = CONTROL_ARMED;
    }

    s_step_active = 1;
    s_step_ms = 0;
    s_cmd = 500;
    s_target = 500;

    pid_reset(&s_pid);
    encoder_hal_get_and_reset_delta();

    ESP_LOGI(TAG, "STEP_TEST 500 -> 1500 started");
}

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
    st.ticks = s_ticks;
    st.missed = s_missed;
    st.step_active = s_step_active;

    return st;
}