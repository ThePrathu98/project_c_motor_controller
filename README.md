# Project C — Closed-loop Brushed DC Motor Controller

## Overview

This project implements a closed-loop brushed DC motor controller using:

- ESP8266 NodeMCU / ESP-12E
- ESP8266 RTOS SDK v3.4
- TI DRV8870EVM H-bridge motor driver
- Pololu 25D 6 V encoder gearmotor
- Bench power supply
- Saleae Logic Pro 8
- Git Bash and PowerShell TCP test clients

Days 3–4 completed the closed-loop velocity PID portion: TCP commands on port `5005` command motor speed, encoder feedback is converted to RPM, and a 1 kHz FreeRTOS control loop holds the requested speed.

Days 5–6 extended the same firmware with a safety state machine and binary telemetry:

- `IDLE / ARMED / RUNNING / FAULT` state machine
- software OVERCURRENT fault based on DRV8870EVM ISEN current estimate
- software STALL fault based on command, measured RPM, and duty
- GPIO2 LED state indication
- packed 20-byte binary telemetry frame at 100 Hz on TCP port `5006`
- sequence number gap detection on the host side
- manual telemetry socket reconnect test

The final restored firmware uses normal overcurrent thresholds:

```c
#define OVERCURRENT_TRIP_MA   900
#define OVERCURRENT_CLEAR_MA  700
```

During testing, the overcurrent threshold was temporarily reduced to prove the fault path safely at the low current levels seen on this no-load bench setup. The final firmware was restored to `900/700`, rebuilt using `idf.py fullclean`, flashed, and verified again.

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
| Current Sense | DRV8870EVM ISEN test pad | ESP8266 A0 | Current estimate for telemetry and overcurrent monitor |
| Saleae D0 | Probe D0 | IN1 / GPIO14 | PWM observation |
| Saleae D1 | Probe D1 | IN2 / GPIO12 | Direction/complementary input observation |
| Saleae D2 | Probe D2 | Encoder A / GPIO5 | Encoder phase A observation |
| Saleae D3 | Probe D3 | Encoder B / GPIO4 | Encoder phase B observation |
| Saleae Ground | Saleae GND | Common GND | Logic analyzer reference |

Final test conditions:

- Bench supply: approximately `6.4 V`
- Current limit for normal tests: approximately `0.5 A`
- ESP8266 powered from USB
- ESP8266 STA mode connected to `NETGEAR38_EXT`
- ESP IP used during final tests: `192.168.1.102`

## Firmware Architecture

The firmware is split into HAL, control, and communication layers:

```text
main/
  app_main.c

components/
  project_hal/
    motor_hal.c / .h      -> wraps ESP8266 PWM for DRV8870 IN1/IN2
    encoder_hal.c / .h    -> wraps GPIO encoder inputs and ISR counting
    adc_hal.c / .h        -> wraps ESP8266 ADC for ISEN current estimate
    led_hal.c / .h        -> wraps GPIO2 LED state indication

  project_control/
    control_task.c / .h   -> 1 kHz loop, state machine, PID, safety integration
    pid.c / .h            -> platform-independent PID controller
    safety_monitor.c / .h -> OVERCURRENT and STALL detection

  project_comm/
    command_server.c / .h -> TCP command server, port 5005
    telemetry_server.c/.h -> binary telemetry server, port 5006
```

### `app_main.c`

`app_main.c` performs system bring-up only:

```text
motor_hal_init()
encoder_hal_init()
adc_hal_init()
led_hal_init()
command_server_start()
delay to allow STA Wi-Fi association
control_task_start()
telemetry_server_start()
```

The command server starts before the high-rate control task so the ESP8266 can join Wi-Fi cleanly before 1 kHz control and ADC sampling begin.

### `motor_hal.c`

`motor_hal.c` drives the DRV8870EVM input pins using ESP8266 PWM.

Final positive-direction behavior:

- IN1 receives PWM from ESP8266 GPIO14.
- IN2 stays low for the tested direction.
- Duty = 0 stops the motor.
- PWM period is 50 us, or approximately 20 kHz.

### `encoder_hal.c`

`encoder_hal.c` handles encoder feedback. The ISR samples encoder phases and updates an encoder delta counter. The ISR stays short and does not run PID, print logs, allocate memory, or block.

### `adc_hal.c`

`adc_hal.c` reads ESP8266 A0 and converts the DRV8870EVM ISEN voltage into an estimated current in mA. The normal measured values were low in this no-load setup, so overcurrent was demonstrated using a temporary safe demo threshold and then restored.

### `control_task.c`

`control_task.c` is the main real-time loop.

It implements:

- 1 kHz hardware timer ISR
- binary semaphore from ISR to task
- `IDLE / ARMED / RUNNING / FAULT` state machine
- slew-rate-limited setpoint update
- encoder delta to RPM conversion
- feed-forward duty estimate
- PID trim
- duty clamping and low-speed hold limits
- ADC current sampling every 10 ms
- safety monitor update
- GPIO2 LED state pattern update
- periodic human-readable telemetry logs

The control architecture is:

```text
1 kHz hardware timer ISR
    -> xSemaphoreGiveFromISR()
        -> FreeRTOS control task wakes
            -> update tick counters
            -> sample ADC every 10 ms
            -> update safety monitor
            -> latch FAULT and kill PWM if needed
            -> updated state LED pattern
            -> run speed/PID sample every 250 ms
            -> command motor PWM
            -> log status
```

The ISR only signals work. PID, ADC, safety logic, and motor updates run in task context.

### `safety_monitor.c`

`safety_monitor.c` detects fault conditions but does not directly drive PWM. It returns fault flags to `control_task.c`, and the control task latches the `FAULT` state and disables motor output.

Fault behavior:

| Fault | Condition |
|---|---|
| OVERCURRENT | current estimate above threshold for more than 5 ms while duty is being applied |
| STALL | commanded RPM > 200, actual RPM < 50, duty > 50%, for 500 ms |

### `telemetry_server.c`

`telemetry_server.c` sends a fixed 20-byte binary frame at 100 Hz on TCP port `5006`.

Host unpack format:

```python
<IhhhhBBHI
```

Frame layout:

| Bytes | Type | Field |
|---:|---|---|
| 0–3 | uint32 | sequence number |
| 4–5 | int16 | target RPM |
| 6–7 | int16 | actual RPM |
| 8–9 | int16 | duty permille |
| 10–11 | int16 | current mA |
| 12 | uint8 | state |
| 13 | uint8 | fault flags |
| 14–15 | uint16 | missed-deadline counter, low 16 bits |
| 16–19 | uint32 | control tick / uptime ms |

The sequence number is checked by the PC script to verify zero telemetry gaps.

## TCP Command Server

`command_server.c` implements a Wi-Fi TCP server using lwIP BSD sockets on port `5005`.

Supported commands:

| Command | Meaning |
|---|---|
| `STATUS` | Reports controller state, command, target, actual RPM, duty, current, fault, error, encoder delta, missed count |
| `CLEAR_FAULT` | Clears latched fault if current has fallen below the clear threshold |
| `ARM` | Arms the controller |
| `SET_SPEED <rpm>` | Commands a signed target speed |
| `STOP` | Stops motor while keeping system armed |
| `DISARM` | Disarms controller |
| `STEP_TEST` | Runs internal 500 -> 1500 RPM step test |

Example Day 5–6 status:

```text
OK STATUS state=RUNNING cmd=800 target=800 actual=787 duty=71 current_ma=282 fault=0x00 fault_name=NONE error=13 delta=-361 missed=143 step=0
```

Meaning:

- `state=RUNNING`: controller is actively controlling speed.
- `cmd=800`: requested speed from TCP command.
- `target=800`: internal slew-limited target.
- `actual=787`: encoder-measured speed.
- `duty=71`: PWM duty command.
- `current_ma=282`: current estimate from ISEN/A0.
- `fault=0x00`: no latched safety fault.
- `fault_name=NONE`: human-readable fault state.
- `missed`: missed control/semaphore count.
- `step=0`: step-test mode is not active.

## Day 3–4 Closed-loop PID Verification

The Day 3–4 one-go test commanded:

```text
ARM
SET_SPEED 1500
SET_SPEED 800
SET_SPEED 300
STOP
DISARM
```

Final steady-state summary from the Day 3–4 evidence:

| Target RPM | Final Steady Average | Average Error |
|---:|---:|---:|
| 1500 | 1503.3 RPM | 0.22% |
| 800 | 808.0 RPM | 1.00% |
| 300 | 302.7 RPM | 0.90% |

All final steady-state average errors were below the 3% requirement.

## Day 5–6 Safety and Telemetry Verification

Day 5–6 final evidence is stored in:

```text
evidence/day5_6_safety_telemetry/
```


Important evidence files:

```text
evidence/day5_6_safety_telemetry/git_bash_terminal_day5_6_log_safety_state_machine_binary_telemetry

evidence/day5_6_safety_telemetry/powershell_day5_6_log_safety_state_machine_binary_telemetry

```

Final verification highlights:

| Test | Result |
|---|---|
| Normal 300 RPM command | `state=RUNNING`, actual RPM nonzero, `fault_name=NONE` |
| Normal 800 RPM command | `state=RUNNING`, actual near 800 RPM, `fault_name=NONE` |
| Telemetry reconnect | two 60 s runs, both `gaps=0` |
| Final telemetry check | 5995 frames, `gaps=0` |
| Overcurrent fault | `fault=0x01`, `fault_name=OVERCURRENT`, recovery through `CLEAR_FAULT` |
| Stall fault | `fault=0x02`, `fault_name=STALL`, recovery through `CLEAR_FAULT` |
| Saleae capture | PWM/drive window and encoder activity visible during normal 800 RPM run |

## Notes and Limitations

- The DRV8870EVM setup used for this build exposed ISEN for current estimation. A direct nFAULT GPIO interrupt path was not wired on this board, so the Day 5–6 implementation documents this as a hardware limitation.
- Overcurrent was safely demonstrated using a reduced temporary threshold because no-load current was much lower than the final trip threshold.
- The final firmware was restored to `OVERCURRENT_TRIP_MA=900` and `OVERCURRENT_CLEAR_MA=700`, rebuilt using `idf.py fullclean`, flashed, and verified at 300/800 RPM with no fault.
- The telemetry evidence saved for this checkpoint is 2x60 s plus a final 60 s check. The same script can be extended to 300 s by changing the run duration for a strict 5-minute run.
- Day 7–8 will add the PyQt6 GUI and live pyqtgraph plot.

## Build / Flash / Monitor

From Git Bash:

```bash
cd /c/Users/pckal/esp_projects/project_c_motor_controller

idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Expected boot lines:

```text
Project C Day 5-6 safety + binary telemetry firmware starting
Wi-Fi connected, IP=192.168.1.102
TCP command server listening on port 5005
1 kHz control loop + Day 5-6 safety monitor started
telemetry_server: binary telemetry listening on port 5006
System bring-up complete
```

## Safety Reminder

Before flashing or changing wiring:

```text
Bench supply output OFF
ESP8266 USB connected
Common ground preserved
```

For normal motor runs:

```text
Bench supply: 6.4 V
Current limit: 0.5 A
Output ON only when running command tests
```
