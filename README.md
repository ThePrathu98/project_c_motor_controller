# Project C — Days 3–4: Closed-loop Velocity PID

## Overview

This project implements a closed-loop brushed DC motor velocity controller using an ESP8266 NodeMCU/ESP-12E, ESP8266 RTOS SDK v3.4, a TI DRV8870EVM H-bridge motor driver, and a Pololu 25D encoder gearmotor.

The Day 3–4 goal is to command motor speed over Wi-Fi/TCP and hold the requested velocity using encoder feedback and PID control. The verified command speeds were:

- 1500 RPM
- 800 RPM
- 300 RPM

The final verified one-go test showed stable closed-loop operation at all three requested speeds with final steady-state average errors below 3%.

## Hardware Setup and Pin Connections

| Category | Signal / Wire | Connection | Purpose |
|---|---|---|---|
| ESP8266 to DRV8870 | D5 / GPIO14 | DRV8870 IN1 | PWM motor drive input |
| ESP8266 to DRV8870 | D6 / GPIO12 | DRV8870 IN2 | Direction / complementary drive input |
| Common Ground | ESP8266 GND | DRV8870 GND and bench supply negative | Shared logic/power reference |
| Motor Power | Bench supply + | DRV8870 VM | Motor supply input |
| Motor Power | Bench supply - | DRV8870 GND | Motor supply return |
| Motor Leads | Motor red / black | DRV8870 OUT1 / OUT2 | Motor output terminals |
| Encoder Power | Blue wire | ESP8266 3V3 | Encoder supply |
| Encoder Ground | Green wire | ESP8266 GND / common GND | Encoder ground |
| Encoder Feedback | Yellow wire / Encoder A | ESP8266 D1 / GPIO5 | Encoder phase A |
| Encoder Feedback | White wire / Encoder B | ESP8266 D2 / GPIO4 | Encoder phase B |
| Saleae D0 | Probe D0 | IN1 / GPIO14 | PWM observation |
| Saleae D1 | Probe D1 | IN2 / GPIO12 | Direction/complementary input observation |
| Saleae D2 | Probe D2 | Encoder A / GPIO5 | Encoder phase A observation |
| Saleae D3 | Probe D3 | Encoder B / GPIO4 | Encoder phase B observation |
| Saleae Ground | Saleae GND | Common GND | Logic analyzer reference |

Final test conditions:

- Bench supply: approximately 6.4 V
- Current limit: approximately 1.0 A
- DRV8870 VREF: high / approximately 100%
- Saleae channels: D0 = IN1, D1 = IN2, D2 = encoder A, D3 = encoder B

## Firmware Architecture

The firmware is split into HAL, control, and communication layers:

```text
main/
  app_main.c

components/
  project_hal/
    motor_hal.c
    motor_hal.h
    encoder_hal.c
    encoder_hal.h

  project_control/
    control_task.c
    control_task.h
    pid.c
    pid.h

  project_comm/
    command_server.c
    command_server.h
```

### `app_main.c`

`app_main.c` performs system bring-up:

```text
motor_hal_init()
encoder_hal_init()
control_task_start()
command_server_start()
```

This keeps the application entry point simple and leaves motor, encoder, control, and communication logic in separate modules.

### `motor_hal.c`

`motor_hal.c` drives the DRV8870EVM input pins using ESP8266 PWM.

Final positive-direction behavior:

- IN1 receives PWM from ESP8266 GPIO14.
- IN2 stays mostly low through ESP8266 GPIO12.
- Duty = 0 stops the motor.
- PWM period is 50 µs, or approximately 20 kHz.

This gives clean motor drive behavior visible on Saleae D0/D1.

### `encoder_hal.c`

`encoder_hal.c` handles encoder feedback.

Encoder B is interrupt-driven in the final version. The ISR samples the other encoder phase to determine direction and updates an encoder delta counter. The ISR stays short and does not run PID, print logs, allocate memory, or block.

### `control_task.c`

`control_task.c` is the main closed-loop controller.

It implements:

- 1 kHz hardware timer ISR
- Binary semaphore from ISR to task
- FreeRTOS control task
- Slew-rate-limited setpoint updates
- Encoder delta to RPM conversion
- Feed-forward duty estimate
- PID trim
- Duty clamping / hold limits
- Low-speed start/hold compensation
- Periodic telemetry logging

The control architecture is:

```text
1 kHz hardware timer ISR
    -> xSemaphoreGiveFromISR()
        -> FreeRTOS control task wakes
            -> update slew-limited target
            -> sample encoder delta
            -> calculate actual RPM
            -> calculate feed-forward duty
            -> calculate PID trim
            -> clamp duty
            -> command motor PWM
            -> log telemetry
```

The ISR only signals work. PID and motor updates run in task context, which avoids slow or blocking operations inside interrupt context.

### `pid.c`

`pid.c` implements the pure C PID controller with:

- Proportional term
- Integral term
- Derivative term
- Output clamping
- Anti-windup / back-calculation behavior

Anti-windup is important because PWM duty is limited. Without anti-windup, the integral term can keep accumulating while the output is saturated, causing overshoot or slow recovery.

## Gain Choices and Tuning Notes

The final closed-loop velocity controller uses feed-forward duty, PID trim, slew-rate limiting, and per-speed hold limits. Pure PID alone was not enough for this motor because the brushed gearmotor has static friction and gearbox stiction, especially at low speed. During early tests, low duty values caused burst-stop-burst behavior: the motor would kick, overshoot, duty would collapse, the motor would stall, and then the firmware would re-kick. To fix this, the controller was tuned to keep a continuous feed-forward/hold-duty floor while the PID applies a smaller correction around that baseline.

The feed-forward term provides the approximate duty needed for each RPM range, while the PID corrects the remaining error. The PID gains were kept modest to avoid aggressive oscillation and overshoot. Slew-rate limiting prevents instant setpoint jumps and makes speed transitions more controlled. The low-speed hold duty is intentionally stronger than a simple linear duty estimate because 300 RPM needs enough torque to overcome gearbox friction without stalling. The final one-go test showed final steady-state average errors of 0.22% at 1500 RPM, 1.00% at 800 RPM, and 0.90% at 300 RPM.

## TCP Command Server

`command_server.c` implements a Wi-Fi TCP server using lwIP BSD sockets on port 5005.

Supported commands:

| Command | Meaning |
|---|---|
| `ARM` | Arms the controller |
| `SET_SPEED <rpm>` | Commands a target speed |
| `STATUS` | Reports controller state, command, target, actual RPM, duty, error, encoder delta, missed count |
| `STOP` | Stops motor while keeping system armed |
| `DISARM` | Disarms controller |

Example status:

```text
STATUS -> OK STATUS state=RUNNING cmd=300 target=300 actual=303 duty=56 error=-3 delta=-139 missed=1528 step=0
```

Meaning:

- `state=RUNNING`: controller is actively controlling speed.
- `cmd=300`: requested speed from TCP command.
- `target=300`: current internal target after slew limiting.
- `actual=303`: encoder-measured speed.
- `duty=56`: PWM duty command.
- `error=-3`: target minus actual.
- `delta=-139`: encoder delta count during the sample interval.
- `missed`: missed control/semaphore count.
- `step=0`: step-test mode is not active.

## Final One-Go Verification

The final one-go test commanded:

```text
ARM
SET_SPEED 1500
SET_SPEED 800
SET_SPEED 300
STOP
DISARM
```

Final steady-state summary:

| Target RPM | Final Steady Average | Average Error |
|---:|---:|---:|
| 1500 | 1503.3 RPM | 0.22% |
| 800 | 808.0 RPM | 1.00% |
| 300 | 302.7 RPM | 0.90% |

All final steady-state average errors are below the 3% requirement.

## Step Response Verification

A separate step-response test was run:

```text
warm-up at 300 RPM -> hold 500 RPM -> step to 1500 RPM
```

The 500 RPM section moved and approached target, with values around 473–497 RPM after settling. After stepping to 1500 RPM, the motor initially overshot and then settled near the commanded 1500 RPM. This provides the required 500→1500 RPM step-response evidence and data for the step-response plot.

## Evidence to Include

Recommended evidence folder:

```text
evidence/day_3_4_closed_loop_velocity_pid/
```

Include:

- Final one-go PowerShell client log
- Final one-go summary file
- Final Git Bash monitor log
- Final step-response client log
- Step-response CSV samples
- Step-response plot PNG
- Saleae wide screenshot showing final one-go speed regions
- Saleae zoomed PWM screenshot
- Saleae zoomed encoder quadrature screenshot
- Hardware setup photo
- Bench supply photo
- 16–20 minute demo video

## Final Demo Summary

This Day 3–4 firmware demonstrates closed-loop brushed DC motor velocity control on the ESP8266. A 1 kHz hardware timer ISR releases a binary semaphore, and a FreeRTOS task performs encoder speed calculation, slew-rate-limited target update, feed-forward plus PID control, and PWM output to the DRV8870 motor driver. Commands are sent using a TCP server on port 5005. Final evidence shows the motor holding 1500 RPM, 800 RPM, and 300 RPM with final steady-state average errors below 3%, plus a separate 500→1500 RPM step-response test.
