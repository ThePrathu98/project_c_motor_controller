# Measurements

Project C bench measurements and final Day 9 evidence summary.

## Hardware setup used for final Day 9 evidence

| Item | Value / connection |
|---|---|
| MCU | ESP8266 NodeMCU / ESP-12E |
| Motor driver | TI DRV8870EVM |
| Motor | Pololu 25D 6 V encoder motor |
| Motor supply | 6.4 V bench supply |
| Current limit | 1.0 A for normal Day 9 runs |
| IN1 | ESP8266 D5 / GPIO14 -> DRV8870EVM IN1 |
| IN2 | ESP8266 D6 / GPIO12 -> DRV8870EVM IN2 |
| Current sense | DRV8870EVM ISEN silver pad -> ESP8266 A0 |
| Encoder A | Yellow wire -> ESP8266 D1 / GPIO5 |
| Encoder B | White wire -> ESP8266 D2 / GPIO4 |
| Encoder power | Blue -> 3V3, Green -> common GND |
| Saleae D0 | IN1 / GPIO14 |
| Saleae D1 | IN2 / GPIO12 |
| Saleae D2 | Encoder A / GPIO5 |
| Saleae D3 | Encoder B / GPIO4 |
| Saleae A4 | ISEN / A0 |

## Day 9 final controller values

| Parameter | Value |
|---|---:|
| Kp | 0.0025 |
| Ki | 0.004 |
| Kd | 0.000 |
| Kaw | 0.200 |
| PID trim clamp | -10% to +10% duty |
| Encoder ticks/rev used in firmware | 110 |
| Speed sample period | 250 ms |
| Control task period | 1 ms |
| Telemetry frame period | 40 ms |

## Startup / idle check

| Metric | Value |
|---|---:|
| State | IDLE |
| Target RPM | 0 |
| Actual RPM | 0 |
| Duty | 0% |
| Current estimate | 40 mA |
| Peak current estimate | 47 mA |
| Fault | NONE |
| Missed deadlines | 0 |
| Wi-Fi reconnects | 0 |

Evidence: `evidence/day9_pid_tuning_soak/day9_powershell_terminal_log.txt`

## Step response

| Test | Command | Observed result |
|---|---|---|
| Final step test | `STEP_TEST` | 500 RPM baseline followed by 1500 RPM step |
| Git Bash evidence | `step_csv` rows | target, internal target, actual RPM, and duty logged |
| GUI evidence | RPM/current plots | step response and current transient visible |
| Saleae evidence | PWM, encoder, ISEN | driver input, encoder feedback, and current activity visible |
| Peak current during step test | STATUS log | about 467 mA |

Evidence files:

```text
evidence/day9_pid_tuning_soak/day9_final_step_response_gui.png
evidence/day9_pid_tuning_soak/day9_final_step_response_saleae_logic.png
evidence/day9_pid_tuning_soak/day9_final_step_response_zoomed_in_17ms_saleae_logic.png
```

## Standalone stall / anti-windup proof

| Metric | During STALL | After recovery |
|---|---:|---:|
| State | FAULT | RUNNING |
| Command RPM | 500 | 800 |
| Target RPM | 500 | 800 |
| Actual RPM | 0 | 800 |
| Duty | 0% | 70% |
| Current estimate | 40 mA | 275 mA |
| Peak current estimate | 293 mA | 331 mA |
| PID integrator x1000 | 0 | 1003 |
| Fault | STALL | NONE |
| Missed deadlines | 2 during fault | 0 after recovery |
| Wi-Fi reconnects | 0 | 0 |

Conclusion: the no-motion fault latched correctly, PWM was forced to zero, the integrator was reset while faulted, and the system recovered after CLEAR_FAULT + ARM + SET_SPEED 800.

Evidence files:

```text
evidence/day9_pid_tuning_soak/day9_stall_fault_gui.png
evidence/day9_pid_tuning_soak/day9_stall_recovery_gui.png
evidence/day9_pid_tuning_soak/day9_stall_vmoff_saleae.png
```

## Official 10-minute soak

| Checkpoint | State | Target RPM | Actual RPM | Duty % | Current mA | Peak mA | Fault | Missed | Wi-Fi reconnects |
|---|---|---:|---:|---:|---:|---:|---|---:|---:|
| Cycle 1, 500 RPM | RUNNING | 500 | 499 | 62 | 257 | 350 | NONE | 0 | 0 |
| Cycle 1, 1500 RPM | RUNNING | 1500 | 1518 | 82 | 311 | 397 | NONE | 0 | 0 |
| Cycle 15, 500 RPM | RUNNING | 500 | 488 | 62 | 145 | 449 | NONE | 0 | 0 |
| Cycle 15, 1500 RPM | RUNNING | 1500 | 1514 | 81 | 296 | 449 | NONE | 0 | 0 |
| Final after STOP/DISARM | IDLE | 0 | 0 | 0 | 40 | 449 | NONE | 0 | 0 |

Heap at final STATUS:

```text
heap_start=57388
heap_now=55968
heap_delta=1420 bytes
```

Bench supply observation during soak:

```text
About 0.1 A at 500 RPM
About 0.3 A at 1500 RPM
About 0.2 A around part of the soak sequence
```

Conclusion: official soak reached cycle 15/15 and ended safely in IDLE with no active fault, `missed=0`, and `wifi_reconnects=0`.

Evidence files:

```text
evidence/day9_pid_tuning_soak/day9_powershell_terminal_log.txt
evidence/day9_pid_tuning_soak/day9_git_bash_terminal_log.txt
evidence/day9_pid_tuning_soak/day9_soak_mid_session_stall_gui.png
evidence/day9_pid_tuning_soak/day9_soak_recovery_500rpm_gui.png
evidence/day9_pid_tuning_soak/day9_soak_1500rpm_running_gui.png
evidence/day9_pid_tuning_soak/day9_soak_saleae_vmoff_stall_overview.png
evidence/day9_pid_tuning_soak/day9_soak_saleae_pwm_encoder_current.png
```

## Notes for final review

- Current values are firmware estimates from ISEN/A0, not calibrated meter readings.
- The controller is tuned for the listed wiring, supply voltage, motor, driver, and encoder scale.
- nFAULT is not wired as an ESP8266 input in this rig; fault detection uses the software safety monitor with current and encoder feedback.
