# Day 9 PID Tuning Notes + 10-Minute Soak

## Goal

Day 9 goal: justify the final gains with step-response evidence and complete a 10-minute soak with current, heap, missed-deadline, Wi-Fi reconnect, and fault/recovery evidence.

## Firmware build used

```text
Branch: day7-8-gui-live-plot
Build command: idf.py build
Flash command: idf.py -p COM5 flash
Monitor command: idf.py -p COM5 monitor
Bench supply: 6.4 V, 1.0 A current limit
Command IP used during Day 9: 192.168.1.105
Command socket: TCP 5005
Telemetry socket: TCP 5006
```

Hardware path used for final Day 9 evidence:

```text
ESP8266 D5 / GPIO14 -> DRV8870EVM IN1
ESP8266 D6 / GPIO12 -> DRV8870EVM IN2
ESP8266 A0          -> DRV8870EVM ISEN silver pad
Encoder A/yellow   -> ESP8266 D1 / GPIO5
Encoder B/white    -> ESP8266 D2 / GPIO4
Encoder blue       -> ESP8266 3V3
Encoder green      -> common ground
Bench supply +     -> DRV8870EVM VM
Bench supply -     -> DRV8870EVM GND
```

## Controller structure

The final controller is feed-forward plus PI trim:

```text
base_duty = static boost + RPM-scaled feed-forward
pid_trim  = PI correction clamped from -10% to +10%
final duty = base_duty + pid_trim, then clamped by speed-range hold limits
```

This structure was kept because the feed-forward term provides most of the torque needed to keep the motor spinning continuously, while PI trim corrects the remaining RPM error.

## Final gain values

```text
Kp  = 0.0025
Ki  = 0.004
Kd  = 0.000
Kaw = 0.200
PID output clamp = -10% to +10% duty trim
```

Rationale:

```text
Kp 0.004 produced faster response but more high-speed overshoot. The final Kp was backed off to 0.0025. Ki 0.004 was kept to reduce steady-state error without making the 500 RPM and 1500 RPM soak unstable. Kd stayed at 0 because the encoder RPM estimate is quantized and derivative would amplify sample noise. Kaw 0.200 kept the integrator bounded during fault/clear tests.
```

## Step-response evidence

Evidence files:

```text
evidence/day9_pid_tuning_soak/day9_final_step_response_gui.png
evidence/day9_pid_tuning_soak/day9_final_step_response_saleae_logic.png
evidence/day9_pid_tuning_soak/day9_final_step_response_zoomed_in_17ms_saleae_logic.png
evidence/day9_pid_tuning_soak/day9_git_bash_terminal_log.txt
evidence/day9_pid_tuning_soak/day9_powershell_terminal_log.txt
```

Observed behavior:

```text
STEP_TEST command accepted.
500 RPM baseline ran first.
1500 RPM step followed.
Git Bash printed step_csv rows for target, internal target, actual RPM, and duty.
GUI showed target/actual RPM and current response.
Saleae showed PWM, encoder activity, and ISEN current activity.
```

The final step response still has startup overshoot at the 1500 RPM transition, but it settles and does not show sustained oscillation in the final soak.

## Anti-windup and stall verification

The clean stall proof used the bench supply output as the no-motion trigger while speed was commanded. This avoided hard pinching the spinning shaft.

Key result from the VM-off stall test:

```text
state=FAULT
cmd=500
target=500
actual=0
duty=0
current_ma=40
peak_current_ma=293
pid_i_x1000=0
fault=0x02
fault_name=STALL
missed=2
wifi_reconnects=0
```

Recovery result after CLEAR_FAULT:

```text
CLEAR_FAULT -> OK CLEAR_FAULT
ARM -> OK ARM
SET_SPEED 800 -> OK SET_SPEED 800
state=RUNNING
target=800
actual=800
duty=70
fault_name=NONE
missed=0
```

Conclusion: PASS. The controller latched STALL, commanded duty to zero, reset the integrator during FAULT, cleared only after the no-motion condition was removed, and recovered to 800 RPM.

Evidence files:

```text
evidence/day9_pid_tuning_soak/day9_stall_fault_gui.png
evidence/day9_pid_tuning_soak/day9_stall_recovery_gui.png
evidence/day9_pid_tuning_soak/day9_stall_vmoff_saleae.png
```

## 10-minute soak result

Official soak procedure:

```text
15 cycles total.
Each cycle: 500 RPM for 20 s, then 1500 RPM for 20 s.
Light load applied every third 500 RPM cycle.
One mid-session no-motion/stall event was triggered using bench supply output OFF/ON.
After recovery, soak resumed and reached SOAK CYCLE 15 / 15.
```

Selected final soak numbers:

| Checkpoint | State | Target RPM | Actual RPM | Duty % | Current mA | Peak mA | Fault | Missed | Wi-Fi reconnects |
|---|---|---:|---:|---:|---:|---:|---|---:|---:|
| Cycle 1 at 500 RPM | RUNNING | 500 | 499 | 62 | 257 | 350 | NONE | 0 | 0 |
| Cycle 1 at 1500 RPM | RUNNING | 1500 | 1518 | 82 | 311 | 397 | NONE | 0 | 0 |
| Cycle 15 at 500 RPM | RUNNING | 500 | 488 | 62 | 145 | 449 | NONE | 0 | 0 |
| Cycle 15 at 1500 RPM | RUNNING | 1500 | 1514 | 81 | 296 | 449 | NONE | 0 | 0 |
| Final after STOP/DISARM | IDLE | 0 | 0 | 0 | 40 | 449 | NONE | 0 | 0 |

Heap result from final STATUS:

```text
heap_start=57388
heap_now=55968
heap_delta=1420 bytes
```

Conclusion: PASS. The official soak reached cycle 15/15, recovered after the mid-session fault event, and ended in IDLE with `fault_name=NONE`, `missed=0`, and `wifi_reconnects=0`.

## Known notes

- The official soak mid-session fault/recovery sequence was not perfectly smooth because the first post-clear 800 RPM recovery attempt hit STALL once. The system then recovered and continued through cycles 10-15 with `fault_name=NONE` and `missed=0`.
- The clean standalone VM-off stall test is the primary anti-windup proof because it clearly shows `pid_i_x1000=0` while faulted and a clean recovery to 800 RPM.
- Current is logged from the ISEN/A0 estimate and should be treated as a bench estimate rather than a calibrated meter reading.

## Day 9 result

```text
PASS
```

Day 9 required items covered:

```text
Kp backed off from the overshooting test point.
Ki used for steady-state correction.
Anti-windup verified by faulted stall/no-motion test.
Step-response evidence captured in GUI, Saleae, and Git Bash step_csv logs.
10-minute soak completed through cycle 15/15.
One stall/no-motion event was triggered, cleared, and recovered.
Heap, missed deadlines, peak current, and Wi-Fi reconnect count were logged.
```
