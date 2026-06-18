# Day 5–6 Details — Safety State Machine and Binary Telemetry

This file holds the longer Day 5–6 explanation so the root `README.md` stays short.

## What Changed from Days 3–4

Days 3–4 already had:

- ESP8266 RTOS SDK build/flash/monitor flow.
- Motor HAL for DRV8870 PWM.
- Encoder feedback converted to RPM.
- Closed-loop PID speed control.
- TCP command server on port `5005`.

Days 5–6 added:

- `IDLE / ARMED / RUNNING / FAULT` safety state machine.
- `CLEAR_FAULT` handling.
- Current estimate from DRV8870EVM ISEN through ESP8266 A0.
- Software `OVERCURRENT` and `STALL` fault checks.
- GPIO2 LED state/fault patterns.
- TCP binary telemetry server on port `5006`.
- 20-byte telemetry frame at 100 Hz.
- Sequence-number gap detection from the host script.
- Manual telemetry socket reconnect verification.

## System Flow

The startup order is intentional:

```text
app_main()
  -> initialize motor, encoder, ADC, LED
  -> start Wi-Fi command server
  -> wait for STA Wi-Fi connection
  -> start 1 kHz control task
  -> start 100 Hz telemetry server
```

The Wi-Fi server starts before the control task because the ESP8266 connection was more stable when Wi-Fi association happened before high-rate control and ADC sampling.

## Control Loop Flow

The 1 kHz timer ISR does not run PID directly. It gives a FreeRTOS semaphore. The control task wakes and performs the real work:

```text
hardware timer ISR
  -> xSemaphoreGiveFromISR()
    -> control task wakes
      -> update tick counters
      -> sample ADC current every 10 ms
      -> update safety monitor
      -> latch FAULT if needed
      -> update LED pattern
      -> update speed/PID sample
      -> command motor duty
      -> publish telemetry snapshot
```

This keeps interrupt work short and moves logging, PID, ADC, and motor-control decisions into task context.

## Safety Monitor Concept

`safety_monitor.c` only detects fault conditions and returns flags. It does not directly drive the motor. `control_task.c` owns the state machine and disables PWM when a fault is latched.

Final thresholds:

```c
#define OVERCURRENT_TRIP_MA   900
#define OVERCURRENT_CLEAR_MA  700
```

Fault conditions:

- `OVERCURRENT`: current is above the trip threshold while duty is being applied.
- `STALL`: command is above 200 RPM, measured speed is below 50 RPM, and duty is above 50% long enough to count as a stall.

Overcurrent was tested with a reduced safe threshold because normal no-load current was much lower than 900 mA. After the test, the final threshold was restored, `idf.py fullclean` was run, and normal 300/800 RPM operation was verified again.

## Telemetry Frame

`telemetry_server.c` streams a fixed binary packet at 100 Hz on TCP port `5006`.

Host unpack format:

```python
<IhhhhBBHI
```

Frame fields:

| Field | Type | Meaning |
|---|---|---|
| `seq` | `uint32` | increases every frame; used for gap detection |
| `target_rpm` | `int16` | slew-limited target RPM |
| `actual_rpm` | `int16` | encoder speed |
| `duty_permille` | `int16` | duty scaled for host display |
| `current_ma` | `int16` | current estimate |
| `state` | `uint8` | state machine value |
| `fault_flags` | `uint8` | active fault bits |
| `missed_deadlines` | `uint16` | low 16 bits of missed counter |
| `uptime_ms` | `uint32` | control tick / uptime |

The host script checks `seq`. If the current sequence number is not exactly previous + 1, it counts a gap.

## Command Server Concept

`command_server.c` uses lwIP/BSD sockets on TCP port `5005`. The host opens a TCP connection, sends one text command, receives one response, and closes the connection. This matched the Day 3–4 testing style and made PowerShell/Git Bash testing simple.

Important commands:

```text
STATUS
CLEAR_FAULT
ARM
SET_SPEED <rpm>
STOP
DISARM
```

## Test Philosophy

Day 5–6 was verified through saved logs and Saleae screenshots instead of video:

- Git Bash logs prove build/flash/boot and binary telemetry.
- PowerShell transcripts prove command/state/fault behavior.
- Saleae screenshots prove PWM and encoder activity during motor running.
- Measurements document safe deviations and limitations.

A final long-spin test was also added after fixing the motor-drive connection issue. This test collected repeated STATUS samples while holding 100 RPM, 300 RPM, 800 RPM, and then 300 RPM again. The motor remained in RUNNING state with fault_name=NONE during the motion intervals, and STOP/DISARM returned actual RPM and duty to zero. The CSV was also plotted in Excel as target-vs-actual RPM and current-vs-time evidence.