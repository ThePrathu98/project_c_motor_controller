# Project C - ESP8266 Closed-loop Brushed DC Motor Controller

ESP8266 RTOS SDK v3.4 firmware for a brushed DC motor controller using an ESP8266 NodeMCU / ESP-12E, TI DRV8870EVM H-bridge driver, and Pololu 25D 6 V gearmotor with encoder feedback.

Current status: **Days 3-4 closed-loop velocity PID demo complete**. The final firmware includes a 1 kHz hardware-timer-driven FreeRTOS control task, encoder-based velocity feedback, PID output clamping / anti-windup, slew-limited RPM targets, and a TCP command server on port `5005`.

## Project Goals Covered

- ESP8266 RTOS SDK v3.4 build / flash / monitor workflow.
- HAL-style motor and encoder layers.
- DRV8870EVM IN1 / IN2 PWM direction control.
- Encoder feedback through GPIO5 / GPIO4.
- 1 kHz real-time control loop using `hw_timer` ISR -> binary semaphore -> FreeRTOS task.
- Pure C PID controller with output clamp and anti-windup behavior.
- Slew-limited commanded RPM targets.
- TCP/BSD socket command interface over Wi-Fi using lwIP on port `5005`.
- PowerShell command verification and Saleae Logic hardware evidence.

## System Flow

```text
PowerShell TCP client
        |
        v
ESP8266 TCP command server, port 5005
        |
        v
Control state machine: DISARMED / ARMED / RUNNING
        |
        v
1 kHz hw_timer ISR -> binary semaphore -> FreeRTOS control task
        |
        v
Slew-limited target -> PID update -> motor HAL duty command
        |
        v
DRV8870EVM -> Pololu 25D motor -> encoder feedback
```

## Hardware Used

- ESP8266 NodeMCU / ESP-12E
- TI DRV8870EVM motor driver
- Pololu 25D 6 V DC gearmotor with encoder
- Bench power supply
- Saleae Logic Pro 8
- Multimeter
- Laptop with Git Bash, VS Code, PowerShell, and ESP8266 RTOS SDK v3.4

## Wiring / Pin Map

| Signal | Connection |
|---|---|
| ESP8266 D5 / GPIO14 | DRV8870EVM IN1 |
| ESP8266 D6 / GPIO12 | DRV8870EVM IN2 |
| ESP8266 GND | DRV8870EVM GND |
| Bench supply + | DRV8870EVM VM |
| Bench supply - | DRV8870EVM GND |
| Motor red / black | DRV8870EVM OUT1 / OUT2 |
| Encoder power | ESP8266 3V3 / GND |
| Encoder A / yellow | ESP8266 D1 / GPIO5 |
| Encoder B / white | ESP8266 D2 / GPIO4 |

Saleae Logic labels:

| Channel | Signal |
|---|---|
| D0 | IN1_D5_GPIO14 |
| D1 | IN2_D6_GPIO12 |
| D2 | ENC_A_D1_GPIO5 |
| D3 | ENC_B_D2_GPIO4 |

## Project Structure

```text
project_c_motor_controller/
├── README.md
├── CMakeLists.txt
├── Makefile
├── sdkconfig
├── main/
│   ├── app_main.c
│   ├── CMakeLists.txt
│   ├── wifi_secrets.h              # local-only, ignored by Git
│   └── wifi_secrets.h.template     # committed template
├── components/
│   ├── project_hal/
│   │   ├── motor_hal.c/.h
│   │   ├── encoder_hal.c/.h
│   │   └── CMakeLists.txt
│   ├── project_control/
│   │   ├── control_task.c/.h
│   │   ├── pid.c/.h
│   │   └── CMakeLists.txt
│   └── project_comm/
│       ├── command_server.c/.h
│       └── CMakeLists.txt
├── docs/
│   ├── day1_toolchain_bringup.md
│   ├── day2_bringup_notes.md
│   ├── measurements_day2.md
│   └── measurements.md
└── evidence/
    ├── day1/
    ├── day2_open_loop_motor/
    └── day_3_4_closed_loop_velocity_pid/
```

## Firmware Architecture

| File | Purpose |
|---|---|
| `main/app_main.c` | Initializes motor HAL, encoder HAL, control task, and TCP command server. |
| `components/project_hal/motor_hal.c` | DRV8870EVM IN1 / IN2 PWM and direction control. |
| `components/project_hal/encoder_hal.c` | Encoder GPIO setup and interrupt-based delta counting. Final stable firmware uses Encoder A-edge counting and samples Encoder B for direction. |
| `components/project_control/control_task.c` | 1 kHz timer/semaphore control loop, RPM sampling, slew limiting, PID update, telemetry, and motor duty update. |
| `components/project_control/pid.c` | Pure C PID with output clamp and anti-windup behavior. |
| `components/project_comm/command_server.c` | Wi-Fi station setup and TCP command server on port `5005`. |

## Real-time Control Loop

```text
hw_timer ISR every 1 ms
    -> xSemaphoreGiveFromISR()
    -> FreeRTOS control task wakes
    -> update slew-limited target
    -> sample encoder delta every 100 ms
    -> estimate RPM
    -> run PID update
    -> clamp output duty
    -> motor_hal_set_duty()
```

The ISR remains short. Encoder sampling, PID calculation, logging, and motor updates run in task context.

## TCP Command Interface

The ESP8266 connects to a 2.4 GHz Wi-Fi network and starts a TCP server on port `5005`.

| Command | Behavior |
|---|---|
| `STATUS` | Returns state, command RPM, target RPM, actual RPM estimate, duty, error, delta, missed count, and step-test state. |
| `ARM` | Arms the controller. Motor remains stopped until a speed command is sent. |
| `DISARM` | Disarms controller and forces duty to zero. |
| `STOP` | Stops/coasts motor and clears command/target/duty while remaining armed. |
| `SET_SPEED <rpm>` | Sets commanded speed. Tested at 300, 800, 1500, and 500 RPM regions. |
| `STEP_TEST` | Runs automatic 500 -> 1500 RPM step test and returns to safe stopped state. |

Example PowerShell client:

```powershell
@'
import socket, time

ip = "192.168.1.105"
port = 5005

def send(cmd):
    with socket.create_connection((ip, port), timeout=5) as s:
        s.sendall((cmd + "\n").encode())
        resp = s.recv(256).decode(errors="ignore").strip()
        print(f"{cmd} -> {resp}")

send("ARM")
send("SET_SPEED 300");  time.sleep(8);  send("STATUS")
send("SET_SPEED 800");  time.sleep(8);  send("STATUS")
send("SET_SPEED 1500"); time.sleep(10); send("STATUS")
send("STOP");           time.sleep(5);  send("STATUS")
send("STEP_TEST");      time.sleep(25); send("STATUS")
'@ | python -
```

## Build / Flash / Monitor

Use Git Bash with the ESP8266 RTOS SDK environment loaded:

```bash
export IDF_PATH=/c/esp/ESP8266_RTOS_SDK
. $IDF_PATH/export.sh
cd /c/Users/pckal/esp_projects/project_c_motor_controller
idf.py build
idf.py -p COM5 flash monitor
```

Final demo procedure:

1. Keep the bench motor supply OFF during flashing.
2. Run `idf.py -p COM5 flash monitor`.
3. Wait for Wi-Fi and TCP server startup.
4. Turn bench supply ON at about `8.0 V`, `1.0 A` current limit.
5. Leave Git Bash monitor running while PowerShell sends TCP commands.

Expected boot indicators:

```text
Project C Day 3-4 full firmware starting
Motor PWM initialized
Encoder A-edge counting enabled: A=GPIO5, B=GPIO4
1 kHz hw_timer -> binary semaphore control loop started
Wi-Fi connected, IP=192.168.1.105
TCP command server listening on port 5005
```

## Final Verification Summary

Representative final command results:

```text
ARM -> OK ARM
SET_SPEED 300 -> OK SET_SPEED 300
STATUS -> ... cmd=300 target=300 actual=0 duty=38 ... step=0

SET_SPEED 800 -> OK SET_SPEED 800
STATUS -> ... cmd=800 target=800 actual=853 duty=42 error=-53 ... step=0

SET_SPEED 1500 -> OK SET_SPEED 1500
STATUS -> ... cmd=1500 target=1500 actual=1707 duty=50 error=-207 ... step=0

STOP -> OK STOP
STATUS -> ... state=ARMED cmd=0 target=0 actual=0 duty=0 ... step=0

STEP_TEST -> OK STEP_TEST 500 1500
STATUS -> ... state=ARMED cmd=0 target=0 actual=0 duty=0 ... step=0
```

Interpretation:

- TCP command server and state machine were verified from PowerShell.
- 800 RPM and 1500 RPM regions produced reliable tracking and visible motor-speed changes.
- STOP returned command, target, duty, and actual speed to zero.
- STEP_TEST ran the 500 -> 1500 sequence and returned to safe stopped/armed state.
- 300 RPM is close to the motor/driver low-speed deadband. The command is accepted and PWM is applied, but encoder-derived RPM can be intermittent at that low speed. This limitation is documented rather than hidden.

## Evidence

Evidence files are stored under:

```text
evidence/day_3_4_closed_loop_velocity_pid/
```

Recommended evidence captured:

- Git Bash boot/monitor log showing Wi-Fi and TCP server startup.
- PowerShell command responses showing `OK` command handling.
- Saleae Logic capture showing IN1/IN2 motor-driver signals and encoder A/B feedback.
- Hardware setup photo.
- Bench power supply photo.
- Recorded Day 3-4 live demo video uploaded separately to Google Drive because of file size.

## Safety / Notes

- Motor supply is kept OFF during flashing.
- Controller boots in `DISARMED` state.
- Motor does not run until `ARM` and `SET_SPEED` are received.
- `STOP` and `DISARM` force duty to zero.
- `main/wifi_secrets.h` contains local Wi-Fi credentials and must not be committed. Commit only `main/wifi_secrets.h.template`.
