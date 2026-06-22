# Project C Motor Controller

ESP8266 RTOS SDK v3.4 firmware for a closed-loop brushed DC motor controller with safety state machine, binary telemetry, and a PyQt6/pyqtgraph live GUI.

This checkpoint focuses on **Days 7-8: PyQt6 GUI + pyqtgraph live plot**. The GUI controls the system from one host window, receives binary telemetry on a background thread, plots target/actual RPM and current, shows colored state/fault status, logs the last 10 events, and reconnects after telemetry socket drops.

## Current checkpoint status

| Area | Status |
|---|---|
| Firmware build | ESP8266 RTOS SDK v3.4, pure C, `idf.py build` project |
| Command socket | TCP text commands on port `5005` |
| Telemetry socket | 20-byte binary frames on port `5006` |
| GUI | `host_gui/project_c_motor_gui.py` using PyQt6 + pyqtgraph |
| Day 7-8 demo speeds | 300, 500, 800 RPM shown from GUI |
| Safety/fault display | STALL/OVERCURRENT faults shown in red GUI state block and history list |
| Evidence folder | `evidence/day7_8_PyQt6_GUI_pyqtgraph_live_plot/` |

## Hardware setup

### Power and motor path

| Signal | Connection |
|---|---|
| Bench supply `+` | DRV8870EVM `VM` |
| Bench supply `-` | DRV8870EVM `GND` |
| Motor red/black | DRV8870EVM `OUT1` / `OUT2` |
| ESP8266 GND | DRV8870EVM `GND` / common ground |
| ESP8266 A0 | DRV8870EVM `ISEN` silver pad |

Normal demo setting:

```text
Bench supply: 6.4 V
Current limit: 1.0 A
```

### ESP8266, TXS0108E, and DRV8870 inputs

The ESP8266 outputs 3.3 V logic. In final Day 7-8 testing, a TXS0108E level shifter made the DRV8870 input path reliable.

| Signal | Connection |
|---|---|
| TXS `VA` | ESP8266 `3V3` |
| TXS `OE` | ESP8266 `3V3` |
| TXS `GND` | Common ground / DRV8870 GND |
| TXS `VB` | DRV8870EVM `V5P0` |
| ESP8266 `D5 / GPIO14` | TXS `A1` |
| TXS `B1` | DRV8870EVM `IN1` |
| ESP8266 `D6 / GPIO12` | TXS `A2` |
| TXS `B2` | DRV8870EVM `IN2` |

### Encoder feedback

| Motor encoder wire | Connection |
|---|---|
| Blue | ESP8266 `3V3` |
| Green | Common ground |
| Yellow / Encoder A | ESP8266 `D1 / GPIO5` |
| White / Encoder B | ESP8266 `D2 / GPIO4` |

### Saleae logic analyzer channels

| Saleae channel | Probe point |
|---|---|
| D0 | DRV8870 `IN1` / TXS `B1` side |
| D1 | DRV8870 `IN2` / TXS `B2` side |
| D2 | Encoder A / ESP8266 GPIO5 |
| D3 | Encoder B / ESP8266 GPIO4 |
| GND | Common ground |

## Firmware architecture

| Folder | Role |
|---|---|
| `main/app_main.c` | startup order and system bring-up |
| `components/project_hal/` | GPIO, PWM, encoder, ADC, LED hardware wrappers |
| `components/project_control/` | 1 kHz control loop, PID, safety monitor, state machine |
| `components/project_comm/` | Wi-Fi, command socket, binary telemetry socket |
| `host_gui/` | PyQt6 GUI and Python dependencies |
| `docs/` | day-by-day implementation notes and measurements |

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

Use the IP printed by the monitor in the GUI. The latest bench runs used `192.168.1.104`.

## Run the Day 7-8 GUI

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
python host_gui\project_c_motor_gui.py --host 192.168.1.104
```

PowerShell socket checks:

```powershell
$ip="192.168.1.104"
Test-NetConnection $ip -Port 5005
Test-NetConnection $ip -Port 5006
```

Expected result for both ports:

```text
TcpTestSucceeded : True
```

Optional command helper:

```powershell
function Send-MotorCommand($cmd) {
    $ip="192.168.1.104"
    $c=New-Object Net.Sockets.TcpClient($ip,5005)
    $s=$c.GetStream()
    $b=[Text.Encoding]::ASCII.GetBytes($cmd + "`n")
    $s.Write($b,0,$b.Length)
    $r=New-Object byte[] 512
    $n=$s.Read($r,0,$r.Length)
    [Text.Encoding]::ASCII.GetString($r,0,$n)
    $c.Close()
}
```

## Day 7-8 GUI demo sequence

Start from a safe state:

```text
STOP -> CLEAR_FAULT -> DISARM -> STATUS
```

Main GUI run:

```text
START / ARM
SET_SPEED 300
SET_SPEED 500
SET_SPEED 800
STOP
DISARM
```

Fault/recovery proof:

```text
START / ARM
SET_SPEED 300
create safe no-motion condition or controlled load
observe red FAULT block and history event
CLEAR_FAULT
DISARM
STATUS
```

Do not stop the spinning shaft with fingers. For a safe stall-style demo, remove motor VM/output briefly or use a controlled, low-energy method.

## Day 7-8 evidence files

| Evidence | Purpose |
|---|---|
| `GUI_300rpm_1_working.png`, `GUI_300rpm_2_working.png` | actual RPM near 300 RPM after start/settling |
| `GUI_500rpm_working.png` | mixed-use speed change to 500 RPM |
| `GUI_800rpm_working.png` | mixed-use speed change to 800 RPM |
| `GUI_1500rpm_working.png` | high-speed run evidence, with tuning limitations noted |
| `GUI_stall.png` | red FAULT state and history list proof |
| `Saleae_logic_300rpm_working.png`, `Saleae_logic_800rpm_working.png`, `Saleae_logic_1500rpm_working.png` | driver input and encoder activity proof |
| `Motor_control_PyQt6_GUI_connection.png` | hardware/GUI connection evidence |
| `TXS0108E-8-Channel_logic_level_shifter.png` | final level shifter hardware evidence |

See:

```text
docs/day7_8_details.md
docs/day7_8_measurements.md
```

## Known limitations before Day 9-10

- The GUI checkpoint is complete enough for the Day 7-8 demo, but PID tuning is not final.
- 300/500/800 RPM are the cleanest demonstrated GUI speeds.
- 1500 RPM runs, but overshoot/ripple still needs Day 9 tuning.
- Sequence gaps are tracked within one continuous telemetry connection; reconnect events are logged separately.
