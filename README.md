# Project C Motor Controller

ESP8266 RTOS SDK v3.4 firmware for a closed-loop brushed DC motor controller using a DRV8870EVM H-bridge, Pololu 25D 6 V encoder motor, TCP commands, binary telemetry, safety fault handling, and a PyQt6/pyqtgraph host GUI.

## Current status

Project C is complete through Day 10 final review preparation. The firmware builds with ESP8266 RTOS SDK 
v3.4, runs a 1 kHz closed-loop motor-control task, accepts Wi-Fi TCP commands on port 5005, 
streams binary telemetry to the PyQt6 GUI on port 5006, detects STALL faults, supports CLEAR_FAULT 
recovery, and has Day 9 PID/soak evidence committed.

Final verified results:
- Final PID gains: Kp=0.0025, Ki=0.004, Kd=0.000, Kaw=0.200.
- Bench supply: 6.4 V, 1.0 A current limit.
- Official soak completed through 15 cycles of 500/1500 RPM.
- Final 500 RPM section: approximately 488 RPM.
- Final 1500 RPM section: approximately 1514 RPM.
- Peak current during soak: approximately 449 mA.
- Final missed control deadlines: 0.
- Final Wi-Fi reconnect count: 0.
- Final state after STOP/DISARM: IDLE, fault=NONE.
- STALL fault verified using a VM-off no-motion test, followed by CLEAR_FAULT, ARM, and speed-command recovery.


| Area | Status |
|---|---|
| Firmware build | ESP8266 RTOS SDK v3.4, pure C, `idf.py build` |
| Command socket | TCP text commands on port `5005` |
| Telemetry socket | 20-byte binary frames on port `5006` |
| GUI | `host_gui/project_c_motor_gui.py` using PyQt6 + pyqtgraph |
| Final Day 9 speeds | 500 RPM, 800 RPM recovery, 1500 RPM soak/step evidence |
| Safety/fault handling | STALL/OVERCURRENT software monitor, latched FAULT state, CLEAR_FAULT recovery |
| Evidence folders | `evidence/day9_pid_tuning_soak/` plus earlier day evidence |

## Hardware setup and wiring

### Power and motor path wiring

| Signal | Connection |
|---|---|
| Bench supply `+` | DRV8870EVM `VM` |
| Bench supply `-` | DRV8870EVM `GND` |
| Motor red/black | DRV8870EVM `OUT1` / `OUT2` |
| ESP8266 GND | DRV8870EVM `GND` / common ground |
| ESP8266 A0 | DRV8870EVM `ISEN` silver pad |

Normal Day 9 setting:

```text
Bench supply: 6.4 V
Current limit: 1.0 A
```

### ESP8266 and DRV8870 input path wiring

Final Day 9 testing used direct ESP8266 GPIO-to-DRV8870 input wiring. No external logic level shifter is required in the final wiring used for the Day 9 evidence.

| Signal | Connection |
|---|---|
| ESP8266 `D5 / GPIO14` | DRV8870EVM `IN1` |
| ESP8266 `D6 / GPIO12` | DRV8870EVM `IN2` |
| ESP8266 `GND` | DRV8870EVM `GND` / common ground |

### Encoder feedback wiring

| Motor encoder wire | Connection |
|---|---|
| Blue | ESP8266 `3V3` |
| Green | Common ground |
| Yellow / Encoder A | ESP8266 `D1 / GPIO5` |
| White / Encoder B | ESP8266 `D2 / GPIO4` |

### Saleae Logic Pro 8 channels wiring

| Saleae channel | Probe point |
|---|---|
| D0 | DRV8870 `IN1` / ESP8266 GPIO14 |
| D1 | DRV8870 `IN2` / ESP8266 GPIO12 |
| D2 | Encoder A / ESP8266 GPIO5 |
| D3 | Encoder B / ESP8266 GPIO4 |
| A4 | DRV8870EVM `ISEN` silver pad |
| GND | Common ground |

## Firmware architecture

| Folder | Role |
|---|---|
| `main/app_main.c` | Startup order and system bring-up |
| `components/project_hal/` | GPIO, PWM, encoder, ADC, LED hardware wrappers |
| `components/project_control/` | 1 kHz control loop, feed-forward + PI trim, safety monitor, state machine |
| `components/project_comm/` | Wi-Fi, command socket, binary telemetry socket |
| `host_gui/` | PyQt6 GUI and Python dependencies list |
| `docs/` | Day notes, measurements, and final documentation |

## Build, flash, and monitor

From Git Bash:

```bash
cd /c/Users/pckal/esp_projects/project_c_motor_controller
export IDF_PATH=/c/esp/ESP8266_RTOS_SDK
. $IDF_PATH/export.sh
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Expected monitor lines:

```text
Wi-Fi connected, IP=<router-assigned ESP IP>
TCP command server listening on port 5005
binary telemetry listening on port 5006
System bring-up complete
```

Use the IP printed by the monitor in PowerShell and in the GUI. The Day 9 bench run used `192.168.1.105`.

## Run the GUI

Create the Python environment once:

```powershell
cd C:\Users\pckal\esp_projects\project_c_motor_controller
py -3 -m venv host_gui\.venv
host_gui\.venv\Scripts\activate
python -m pip install --upgrade pip
python -m pip install -r host_gui\requirements.txt
```

Start the GUI:

```powershell
.\host_gui\.venv\Scripts\python.exe .\host_gui\project_c_motor_gui.py --host 192.168.1.105
```

PowerShell socket checks:

```powershell
$ip="192.168.1.105"
Test-NetConnection $ip -Port 5005
Test-NetConnection $ip -Port 5006
```

Expected result for both ports:

```text
TcpTestSucceeded : True
```

Optional command helper:

```powershell
function Send-MotorCmd {
    param([string]$Cmd)
    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($ip, 5005)
    $stream = $client.GetStream()
    $writer = New-Object System.IO.StreamWriter($stream)
    $writer.AutoFlush = $true
    $reader = New-Object System.IO.StreamReader($stream)
    $writer.WriteLine($Cmd)
    $reply = $reader.ReadLine()
    $reader.Close(); $writer.Close(); $client.Close()
    Write-Host "CMD: $Cmd"
    Write-Host "RSP: $reply"
    Write-Host ""
}
```

## Useful command sequence

Start from a safe state:

```text
STOP -> DISARM -> STATUS
```

Step response:

```text
ARM -> STEP_TEST -> STATUS after completion
```

Manual speed run:

```text
ARM -> SET_SPEED 500 -> SET_SPEED 1500 -> STOP -> DISARM -> STATUS
```

Safe stall proof used for Day 9:

```text
ARM -> SET_SPEED 500
turn bench supply output OFF long enough to trigger STALL
turn bench supply output ON
CLEAR_FAULT -> ARM -> SET_SPEED 800 -> STOP -> DISARM -> STATUS
```

Do not hard-pinch the spinning shaft with fingers. For a repeatable stall/no-motion proof, use the bench supply output control or current limit so the motor energy stays low.

## Day 9 result summary

Final PI trim values:

```text
Kp  = 0.0025
Ki  = 0.004
Kd  = 0.000
Kaw = 0.200
PID trim clamp = -10% to +10% duty
```

Day 9 evidence showed:

- 500 -> 1500 RPM step-response evidence from `STEP_TEST` and GUI/Saleae captures.
- STALL fault latched during VM-off no-motion test.
- `pid_i_x1000=0` during FAULT, proving the integrator did not ramp while faulted.
- CLEAR_FAULT, ARM, and 800 RPM recovery worked after the stall test.
- Official soak reached cycle 15/15 and ended in IDLE with `fault_name=NONE`, `missed=0`, and `wifi_reconnects=0`.
- Peak current during the official soak was about 449 mA.

See:

```text
docs/day10_tuning_notes.md
docs/day10_measurements.md
evidence/day9_10_pid_tuning_soak/
```

## Final review files

- `README.md` — wiring, build/flash steps, GUI launch, safety warnings, gain values, and soak numbers.
- `safety_design.md` — fault triggers, control response, and recovery rules.
- `LIMITATIONS.md` — known limitations of the bench rig.
- `docs/day9_tuning_notes.md` — Day 9 PID tuning and soak evidence notes.
- `docs/day9_measurements.md` — Day 9 measured values.
- `docs/measurements_day10_final.md` — final review measurement summary.
- `docs/tuning_notes_day10_final.md` — final gain summary and tuning conclusion.
- `evidence/day9_pid_tuning_soak/` — logs, GUI screenshots, Saleae screenshots, and soak evidence.

## Final release

The final review release is tagged as `v1.0` after all Day 10 documentation is committed.

Tag command:

```bash
git tag -a v1.0 -m "Project C final review release"
git push origin v1.0
```
