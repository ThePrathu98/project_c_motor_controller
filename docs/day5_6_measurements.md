# Day 5–6 Measurements and Evidence

## Test Setup

| Item | Value |
|---|---|
| Board / SDK | ESP8266 NodeMCU, ESP8266 RTOS SDK v3.4 |
| Driver / motor | TI DRV8870EVM, Pololu 25D 6 V encoder gearmotor |
| Motor supply | 6.4 V bench supply, 0.5 A current limit |
| Command socket | TCP port 5005 |
| Telemetry socket | TCP port 5006 |
| Telemetry frame | 20 bytes at 100 Hz |
| Final ESP IP | 192.168.1.105 during latest rerun; use the IP printed in Git Bash monitor |

## Power and Safety Order

Before flashing or changing wiring:

```text
Bench supply output OFF
ESP8266 powered by USB
Common ground connected
```

Before normal motor command tests:

```text
Bench supply = 6.4 V, 0.5 A current limit
Output ON only during motor run tests
```

## Git Bash Commands and Reason

```bash
cd /c/Users/pckal/esp_projects/project_c_motor_controller
export IDF_PATH=/c/esp/ESP8266_RTOS_SDK
. $IDF_PATH/export.sh
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

| Command | Why it is used |
|---|---|
| `cd ...project_c_motor_controller` | enter the correct project folder |
| `export IDF_PATH=...` | point Git Bash to ESP8266 RTOS SDK v3.4 |
| `. $IDF_PATH/export.sh` | load Xtensa tools and IDF Python environment |
| `idf.py build` | compile firmware |
| `idf.py -p COM5 flash` | program ESP8266 on COM5 |
| `idf.py -p COM5 monitor` | verify boot, Wi-Fi, port 5005, and port 5006 |

Expected monitor proof:

```text
Wi-Fi connected, IP=192.168.1.102
TCP command server listening on port 5005
telemetry_server: binary telemetry listening on port 5006
System bring-up complete
```

## Git Bash Telemetry Evidence

Telemetry was tested from Git Bash by connecting to port `5006`, unpacking 20-byte frames, and checking sequence numbers.

Evidence:

```text
gitbash_day5_6_telemetry_2x60s_reconnect.txt
gitbash_day5_6_final_telemetry_check.txt
```

| Test | Frames | Gaps |
|---|---:|---:|
| Telemetry run 1 | 5997 | 0 |
| Telemetry run 2 | 5994 | 0 |
| Final restored firmware check | 5995 | 0 |

This proves the 100 Hz binary telemetry stream and manual reconnect path.

## PowerShell Command Evidence

PowerShell was used for command/state verification on port `5005`.

Command order:

```text
CLEAR_FAULT -> STATUS -> ARM -> STATUS
SET_SPEED 100 -> repeated STATUS samples
SET_SPEED 300 -> repeated STATUS samples
SET_SPEED 800 -> repeated STATUS samples
SET_SPEED 300 -> repeated STATUS samples
STOP -> STATUS
DISARM -> STATUS
```

Evidence:

```text
powershell_day5_6_long_spin_status_samples_after_fix.txt
```

Final restored firmware result:

| Command point | Result |
|---|---|
| after `CLEAR_FAULT` | `state=IDLE`, `fault_name=NONE` |
| after `ARM` | `state=ARMED`, motor stopped |
| at `SET_SPEED 300` | `state=RUNNING`, `actual=242`, `duty=52`, `current_ma=136`, `fault_name=NONE` |
| at `SET_SPEED 800` | `state=RUNNING`, `actual=787`, `duty=71`, `current_ma=282`, `fault_name=NONE` |
| after `STOP` | `state=ARMED`, motor stopped |
| after `DISARM` | `state=IDLE` |


## Update Current Measurements

| Condition | Current estimate |
|---|---:|
| IDLE | 37–40 mA |
| 300 RPM final check | 136 mA |
| 800 RPM final check | 282 mA |
| 800 RPM Saleae run | 299 mA |

Final firmware threshold:

```c
#define OVERCURRENT_TRIP_MA   900
#define OVERCURRENT_CLEAR_MA  700
```

## Fault Evidence

### OVERCURRENT

Evidence:

```text
powershell_day5_6_long_spin_status_samples_after_fix.txt
```

Observed:

```text
state=FAULT
fault=0x01
fault_name=OVERCURRENT
```

The fault was proven with a reduced safe demo threshold because no-load current was much lower than the final threshold. Final firmware was restored to `900/700` afterward.

### STALL

Evidence:

```text
powershell_day5_6_long_spin_status_samples_after_fix.txt
```

Observed:

```text
SET_SPEED 1500 -> OK SET_SPEED 1500
STATUS -> state=FAULT cmd=1500 target=1420 actual=0 duty=0 current_ma=37 fault=0x02 fault_name=STALL
```

The controlled no-motion method used VM off. The motor did not spin, actual RPM stayed zero, and the safety monitor latched `STALL`.

## Saleae Logic Evidence

Evidence screenshot:

```text
saleae_logic_day5_6_pwm_encoder_screenshot.png
```

Observed: IN1 active during the run window, IN2 low for the tested direction, encoder A/B activity while the motor spun, and encoder activity stopped after `STOP`.

## Final Restore Verification

After fault tests:

```text
grep confirmed OVERCURRENT_TRIP_MA=900 and OVERCURRENT_CLEAR_MA=700
idf.py fullclean
idf.py build
idf.py -p COM5 flash
final PowerShell command test passed
final Git Bash telemetry test showed gaps=0
```

## Summary

Day 5–6 evidence demonstrates command/state transitions, normal motor control at 300 and 800 RPM, binary telemetry on port 5006, zero sequence gaps, manual reconnect, overcurrent/stall fault recovery, GPIO2 LED observation, and Saleae PWM/encoder activity.
