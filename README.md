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
| Final ESP IP | 192.168.1.104 during latest rerun; use the IP printed in Git Bash monitor |

## Requirement Check Against Day 5–6 Brief

| Brief item | Evidence / status |
|---|---|
| OVERCURRENT fault trips and recovers | Verified with a reduced safe demo threshold; final firmware restored to 900/700 mA afterward |
| STALL fault trips and recovers | Verified using controlled no-motion / VM-off test |
| 4-state machine | Verified through `IDLE -> ARMED -> RUNNING -> FAULT` and recovery through `CLEAR_FAULT` |
| GPIO2 LED patterns | Implemented and visually observed |
| Binary telemetry on port 5006 | Verified with 20-byte frames at 100 Hz and zero sequence gaps |
| Manual socket reconnect | Verified using two separate 60 s telemetry connections |
| PWM/encoder activity | Verified using Saleae during final long-spin motor run |

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

For the controlled STALL test only:

```text
Bench supply VM/output OFF
ESP8266 USB remains connected
Command speed while actual RPM stays zero
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
Wi-Fi connected, IP=<printed ESP IP>
TCP command server listening on port 5005
telemetry_server: binary telemetry listening on port 5006
System bring-up complete
```

The latest rerun used `192.168.1.104`. If the router assigns a different address, use the IP printed in the Git Bash monitor for PowerShell and telemetry tests.

## Git Bash Telemetry Evidence

Telemetry was tested from Git Bash by connecting to port `5006`, unpacking 20-byte frames, and checking sequence numbers.

Evidence:

```text
git_bash_terminal_day5_6_log_safety_state_machine_binary_telemetry.txt
```

| Test | Frames | Gaps |
|---|---:|---:|
| Telemetry run 1 | 5997 | 0 |
| Telemetry run 2 | 5994 | 0 |
| Final restored firmware check | about 5995–5996 | 0 |

This proves the 100 Hz binary telemetry stream and manual reconnect path.

## PowerShell Command Evidence

PowerShell was used for command/state verification on port `5005`.

Final long-spin command order:

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
powershell_day5_6_log_safety_state_machine_binary_telemetry.txt
day5_6_long_spin_plots_safety_state_machine_safety_telemetry.xlsx
```

Final command-test result after the motor-drive connection issue was fixed:

| Command point | Result |
|---|---|
| after `CLEAR_FAULT` | `state=IDLE`, `fault_name=NONE` |
| after `ARM` | `state=ARMED`, motor stopped |
| at `SET_SPEED 100` | `state=RUNNING`, actual RPM nonzero, current above idle, `fault_name=NONE` |
| at `SET_SPEED 300` | `state=RUNNING`, actual RPM nonzero, `fault_name=NONE` |
| at `SET_SPEED 800` | `state=RUNNING`, actual RPM near target, `fault_name=NONE` |
| after `STOP` | `state=ARMED`, actual RPM returned to 0, duty=0 |
| after `DISARM` | `state=IDLE`, actual RPM=0, duty=0 |

## Long Spin Status Sample Test

Evidence:

```text
powershell_day5_6_long_spin_status_samples_after_fix.txt
day5_6_long_spin_plots_safety_state_machine_safety_telemetry
saleae_logic_day5_6_pwm_encoder_screenshot.png
```

The final long-spin test held multiple speeds and collected repeated `STATUS` samples. This gives more evidence than a single status point because it shows actual RPM and current over time.

| Phase | Result |
|---|---|
| 100 RPM hold | motor ran continuously, actual RPM nonzero, `fault_name=NONE` |
| 300 RPM hold | motor ran continuously, actual RPM tracked the command, `fault_name=NONE` |
| 800 RPM hold | motor ran near target speed, `fault_name=NONE` |
| 300 RPM final hold | motor returned to lower speed command, `fault_name=NONE` |
| STOP | state returned to `ARMED`, actual RPM=0, duty=0 |
| DISARM | state returned to `IDLE`, actual RPM=0, duty=0 |

Representative long-spin samples:

| Phase | Target RPM | Example actual RPM | Example current |
|---|---:|---:|---:|
| Low-speed hold | 100 | 115–183 RPM | 107–149 mA |
| Mid-speed hold | 300 | 233–296 RPM | 121–226 mA |
| High-speed hold | 800 | 763–811 RPM | 212–266 mA |
| Stop / disarm | 0 | 0 RPM | about 34–37 mA |

Excel plots included:

| Plot | Purpose |
|---|---|
| Target RPM vs Actual RPM | shows speed tracking across 100 -> 300 -> 800 -> 300 -> stop |
| Current_mA vs Time | shows current during motor operation and confirms it stayed below the final overcurrent threshold |

## Current Measurements

| Condition | Current estimate |
|---|---:|
| IDLE | about 32–40 mA |
| 100 RPM long-spin hold | about 107–149 mA |
| 300 RPM long-spin hold | about 121–226 mA, with one transient higher sample retained in CSV |
| 800 RPM long-spin hold | about 212–266 mA |
| STOP / DISARM | about 34–37 mA |

Final firmware threshold:

```c
#define OVERCURRENT_TRIP_MA   900
#define OVERCURRENT_CLEAR_MA  700
```

The long-spin current stayed below the final overcurrent threshold during normal motor operation.

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

Observed:

- IN1/GPIO14 active during the long-spin run window.
- IN2/GPIO12 low for the tested direction.
- Encoder A/GPIO5 and Encoder B/GPIO4 active while the motor spun.
- Encoder activity stopped after `STOP`.

## Final Restore Verification

After fault tests and after the motor-drive connection issue was fixed:

```text
grep confirmed OVERCURRENT_TRIP_MA=900 and OVERCURRENT_CLEAR_MA=700
idf.py fullclean
idf.py build
idf.py -p COM5 flash
final PowerShell normal command test passed
final long-spin PowerShell test passed
final Git Bash telemetry test showed gaps=0
Saleae confirmed IN1 and encoder A/B activity during motor motion
```

## Summary

Day 5–6 evidence demonstrates command/state transitions, normal motor control at 100/300/800 RPM, long-spin repeated `STATUS` samples, binary telemetry on port 5006, zero sequence gaps, manual reconnect, overcurrent/stall fault recovery, GPIO2 LED observation, Saleae PWM/encoder activity, and Excel plots for target-vs-actual RPM and current-vs-time.
