# Project C Measurements and Verification Notes

This document records the main hardware and firmware verification evidence for Project C: an ESP8266 closed-loop brushed DC motor controller using ESP8266 RTOS SDK v3.4, TI DRV8870EVM, and Pololu 25D gearmotor with encoder.

## Hardware / Tools

| Item | Details |
|---|---|
| MCU | ESP8266 NodeMCU / ESP-12E |
| Motor driver | TI DRV8870EVM H-bridge |
| Motor | Pololu 25D 6 V DC gearmotor with encoder |
| Power | Bench supply, final demo around 8.0 V / 1.0 A limit |
| Measurement tools | Multimeter, Saleae Logic Pro 8, Git Bash monitor, PowerShell TCP client |
| SDK | ESP8266 RTOS SDK v3.4 |

## Final Wiring

| Signal | Connection |
|---|---|
| ESP8266 D5 / GPIO14 | DRV8870EVM IN1 |
| ESP8266 D6 / GPIO12 | DRV8870EVM IN2 |
| ESP8266 GND | DRV8870EVM GND |
| Bench supply + / - | DRV8870EVM VM / GND |
| Motor red / black | DRV8870EVM OUT1 / OUT2 |
| Encoder A / yellow | ESP8266 D1 / GPIO5 |
| Encoder B / white | ESP8266 D2 / GPIO4 |
| Encoder VCC / GND | ESP8266 3V3 / GND |

Saleae channel labels:

| Channel | Signal |
|---|---|
| D0 | IN1_D5_GPIO14 |
| D1 | IN2_D6_GPIO12 |
| D2 | ENC_A_D1_GPIO5 |
| D3 | ENC_B_D2_GPIO4 |

## Day 1 - Toolchain

Verified ESP8266 RTOS SDK v3.4 build, flash, and monitor on COM5.

```bash
export IDF_PATH=/c/esp/ESP8266_RTOS_SDK
. $IDF_PATH/export.sh
idf.py build
idf.py -p COM5 flash monitor
```

Observed: ESP8266 detected, firmware flashed, bootloader and application output visible in serial monitor. Evidence saved under `evidence/day1/`.

## Day 2 - Open-loop Motor Bring-up

Verified motor and DRV8870EVM before closed-loop control.

Observations:

- Motor spun correctly when directly connected to bench supply.
- Motor spun through DRV8870EVM manual/onboard test path.
- Motor began spinning reliably above roughly 6.4 V in the driver setup; final tests used about 8.0 V / 1.0 A.
- ESP8266 firmware drove the sequence: forward -> stop -> reverse -> stop.
- Saleae showed IN1/IN2 command timing and encoder A/B activity during rotation.

Representative Day 2 behavior:

```text
Forward: IN1 active, IN2 low, OUT1-OUT2 positive, motor forward
Stop:    IN1 low, IN2 low, OUT1-OUT2 near zero, motor stops/coasts
Reverse: IN1 low, IN2 active, OUT1-OUT2 negative, motor reverse
```

Day 2 evidence saved under `evidence/day2_open_loop_motor/`.

## Days 3-4 - Closed-loop Velocity PID

Implemented and verified:

| Requirement | Result |
|---|---|
| 1 kHz FreeRTOS control task awakened by hardware timer ISR via binary semaphore | Implemented in `control_task.c`; boot log confirms loop startup. |
| PID in pure C with output clamp / anti-windup | Implemented in `pid.c`. |
| Slew-limited target RPM | Implemented in `control_task.c`. |
| TCP/BSD socket server via lwIP port 5005 | Implemented in `command_server.c`; verified from PowerShell. |
| Commands | `ARM`, `DISARM`, `STOP`, `SET_SPEED <rpm>`, `STATUS`, `STEP_TEST`. |
| Step test | `STEP_TEST` runs 500 -> 1500 RPM sequence and returns to safe stopped state. |

## Final Boot / Server Verification

Git Bash commands:

```bash
cd /c/Users/pckal/esp_projects/project_c_motor_controller
idf.py build
idf.py -p COM5 flash monitor
```

Expected and observed boot indicators:

```text
Project C Day 3-4 full firmware starting
Motor PWM initialized
Encoder A-edge counting enabled: A=GPIO5, B=GPIO4
1 kHz hw_timer -> binary semaphore control loop started
Wi-Fi connected, IP=192.168.1.105
TCP command server listening on port 5005
```

Motor supply was OFF during flash, then turned ON after the TCP server was listening.

## Final TCP Command Verification

PowerShell command sequence:

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

Representative final output:

```text
ARM -> OK ARM
SET_SPEED 300 -> OK SET_SPEED 300
STATUS -> OK STATUS state=RUNNING cmd=300 target=300 actual=0 duty=38 error=300 delta=0 step=0

SET_SPEED 800 -> OK SET_SPEED 800
STATUS -> OK STATUS state=RUNNING cmd=800 target=800 actual=853 duty=42 error=-53 delta=-37 step=0

SET_SPEED 1500 -> OK SET_SPEED 1500
STATUS -> OK STATUS state=RUNNING cmd=1500 target=1500 actual=1707 duty=50 error=-207 delta=-74 step=0

STOP -> OK STOP
STATUS -> OK STATUS state=ARMED cmd=0 target=0 actual=0 duty=0 error=0 delta=0 step=0

STEP_TEST -> OK STEP_TEST 500 1500
STATUS -> OK STATUS state=ARMED cmd=0 target=0 actual=0 duty=0 error=0 delta=0 step=0
```

## Interpretation

- TCP command path worked: laptop -> ESP8266 -> command server -> control state machine.
- `ARM`, `SET_SPEED`, `STOP`, and `STEP_TEST` all returned valid `OK` responses.
- 800 RPM and 1500 RPM regions produced reliable motion and encoder-derived velocity estimates.
- STOP returned command, target, duty, and actual speed to zero.
- STEP_TEST completed and returned to safe stopped/armed state.
- 300 RPM is near the low-speed motor/driver deadband. The command is accepted and PWM is applied, but encoder-derived RPM can be intermittent at that low speed. This limitation is documented honestly.

## Saleae Verification

Saleae Logic confirmed real hardware behavior:

- D0 / IN1 and D1 / IN2 showed motor-driver control signals.
- D2 / Encoder A and D3 / Encoder B showed feedback activity during motor movement.
- Encoder activity reduced/stopped after STOP.

Physical closed-loop path verified:

```text
PowerShell TCP command -> ESP8266 command server -> control task/PID -> motor HAL PWM -> DRV8870EVM -> motor -> encoder feedback
```

## Evidence Locations

```text
evidence/day1/
evidence/day2_open_loop_motor/
evidence/day_3_4_closed_loop_velocity_pid/
```

The Day 3-4 live demo video was uploaded separately to Google Drive because of file size.
