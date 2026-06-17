# Day 5–6 Measurements and Evidence

## Test Setup

| Item | Value |
|---|---|
| Board | ESP8266 NodeMCU / ESP-12E |
| SDK | ESP8266 RTOS SDK v3.4 |
| Motor driver | TI DRV8870EVM |
| Motor | Pololu 25D 6 V encoder gearmotor |
| Motor supply | 6.4 V bench supply |
| Normal current limit | 0.5 A |
| Command socket | TCP port 5005 |
| Binary telemetry socket | TCP port 5006 |
| Telemetry frame | 20 bytes |
| Telemetry rate | 100 Hz |
| ESP IP used in final tests | 192.168.1.102 |

## Current Sense

DRV8870EVM ISEN was wired to ESP8266 A0.

Measured/observed current values from final logs:

| Condition | Current estimate |
|---|---:|
| IDLE | 37–40 mA |
| 300 RPM final command check | 136 mA |
| 800 RPM final command check | 282 mA |
| 800 RPM Saleae redo run | 299 mA |

The final firmware overcurrent threshold is:

```c
#define OVERCURRENT_TRIP_MA   900
#define OVERCURRENT_CLEAR_MA  700
```

A reduced safe demo threshold was used only to prove the overcurrent fault path because the no-load bench current was far below the final threshold. After proof, the threshold was restored and the firmware was rebuilt using `idf.py fullclean`.

## Normal Command Verification

Final restored firmware behavior:

| Command | Result |
|---|---|
| `STATUS` after clear | `state=IDLE`, `fault_name=NONE` |
| `ARM` | `OK ARM` |
| `SET_SPEED 300` | motor spins moderately |
| `STATUS` at 300 RPM | `state=RUNNING`, `actual=242`, `duty=52`, `current_ma=136`, `fault_name=NONE` |
| `SET_SPEED 800` | motor spins faster |
| `STATUS` at 800 RPM | `state=RUNNING`, `actual=787`, `duty=71`, `current_ma=282`, `fault_name=NONE` |
| `STOP` | motor stops/coasts and state returns to `ARMED` |
| `DISARM` | state returns to `IDLE` |

## Binary Telemetry Verification

Evidence files:

Day 5–6 final evidence is stored in:

```text
evidence/day5_6_safety_telemetry/
```

Important evidence files:

```text
evidence/day5_6_safety_telemetry/git_bash_terminal_day5_6_log_safety_state_machine_binary_telemetry.txt

evidence/day5_6_safety_telemetry/powershell_day5_6_log_safety_state_machine_binary_telemetry.txt

```

Results:

| Test | Frames | Gaps |
|---|---:|---:|
| Telemetry run 1 | 5997 | 0 |
| Telemetry run 2 | 5994 | 0 |
| Final restored firmware telemetry check | 5995 | 0 |

The second run opened a new telemetry socket after the first run closed, proving manual reconnect recovery. The sequence number stayed gap-free during each run.

## OVERCURRENT Fault Verification

Safe demo-threshold result:

```text
state=FAULT
fault=0x01
fault_name=OVERCURRENT
duty=0
```

Recovery:

```text
CLEAR_FAULT -> OK CLEAR_FAULT
STATUS -> state=IDLE fault=0x00 fault_name=NONE
```

Engineering note:

- The fault path was validated using a reduced threshold because the real no-load current was approximately 40–300 mA.
- The final firmware was restored to `900/700` and verified again with 300/800 RPM normal command tests.

## STALL Fault Verification

Controlled no-motion stall result with motor VM off:

Also, GPIO2 led patterns were observed. Visually observed and verified.

```text
SET_SPEED 1500 -> OK SET_SPEED 1500
STATUS -> state=FAULT cmd=1500 target=1420 actual=0 duty=0 current_ma=37 fault=0x02 fault_name=STALL
```

Recovery:

```text
CLEAR_FAULT -> OK CLEAR_FAULT
STATUS -> state=IDLE fault=0x00 fault_name=NONE
```

Engineering note:

The controlled no-motion method proves the STALL logic path without hand-grabbing the shaft. The motor is commanded to move, actual speed remains zero, and the software safety monitor latches `SAFETY_FAULT_STALL`.

## Saleae Logic Evidence

Evidence screenshot:

```text
evidence/day5_6_safety_telemetry/saleae_logic_day5_6_pwm_encoder_screenshot.png
```

Observed:

- IN1/GPIO14 active during the 800 RPM run window.
- IN2/GPIO12 remains low for the tested direction.
- Encoder A/GPIO5 and Encoder B/GPIO4 show activity during the motor-running interval.
- Encoder activity stops after `STOP`.

Related PowerShell log:

```text
evidence/day5_6_safety_telemetry/powershell_day5_6_log_safety_state_machine_binary_telemetry.txt
```

## Final Restore Verification

Evidence file:

```text
evidence/day5_6_safety_telemetry/git_bash_terminal_day5_6_log.txt

evidence/day5_6_safety_telemetry/powershell_day5_6_log_safety_state_machine_binary_telemetry.txt

```

Final checks performed:

```text
grep confirmed OVERCURRENT_TRIP_MA = 900
grep confirmed OVERCURRENT_CLEAR_MA = 700
idf.py fullclean
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Final restored firmware then passed:

- normal 300 RPM command test,
- normal 800 RPM command test,
- final 60 s telemetry check with `gaps=0`.

## Summary

Day 5–6 evidence demonstrates:

- command/state transitions,
- normal motor control after restoring thresholds,
- binary telemetry on port 5006,
- zero sequence gaps during telemetry checks,
- manual telemetry reconnect,
- overcurrent fault and recovery,
- stall fault and recovery,
- Saleae PWM/encoder activity during a normal 800 RPM run.
