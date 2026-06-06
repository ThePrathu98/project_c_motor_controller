# Project C - ESP8266 Brushed DC Motor Controller

ESP8266 RTOS SDK v3.4 firmware for a brushed DC motor controller using a NodeMCU / ESP-12E, TI DRV8870EVM H-bridge, and Pololu 25D 6 V gearmotor with encoder.

Current status: **Day 2 core bring-up complete**. ESP8266 firmware controls the motor forward, stop, reverse, and stop through the DRV8870EVM.

## System Flow

```text
ESP8266 RTOS SDK firmware -> motor HAL -> DRV8870EVM -> Pololu 25D DC motor
```

## Hardware Used

- ESP8266 NodeMCU / ESP-12E
- TI DRV8870EVM motor driver
- Pololu 25D 6 V gearmotor with encoder
- Bench power supply, multimeter, breadboard, jumper wires
- Laptop with Git Bash and VS Code

## Wiring / Pin Map

| Signal | Connection |
|---|---|
| ESP8266 D5 / GPIO14 | DRV8870 IN1 |
| ESP8266 D6 / GPIO12 | DRV8870 IN2 |
| ESP8266 GND | DRV8870 GND |
| Bench supply +/- | DRV8870 VM / GND |
| Motor red / black | DRV8870 OUT1 / OUT2 |
| Encoder blue / green | ESP8266 3V3 / GND |
| Encoder yellow / white | ESP8266 D1 GPIO5 / D2 GPIO4 |

## Project Structure

```text
project_c_motor_controller/
├── main/hello_world_main.c
├── components/project_hal/
│   ├── motor_hal.c / motor_hal.h
│   └── encoder_hal.c / encoder_hal.h
├── docs/day2_bringup_notes.md
├── docs/measurements_day2.md
├── evidence/day1/
├── evidence/day2_open_loop_motor/
└── README.md
```

## Firmware Architecture

| File | Purpose |
|---|---|
| `main/hello_world_main.c` | App entry point and Day 2 test loop |
| `motor_hal.c/.h` | DRV8870 IN1 / IN2 motor control |
| `encoder_hal.c/.h` | Encoder A/B GPIO input setup and raw reads |

Current motor HAL behavior: positive = forward, negative = reverse, zero = stop/coast.

## Build / Flash / Monitor

```bash
export IDF_PATH=/c/esp/ESP8266_RTOS_SDK
. $IDF_PATH/export.sh
cd ~/esp_projects/project_c_motor_controller
idf.py build
idf.py -p COM5 flash monitor
```

Exit monitor with `Ctrl + ]`.

## Day 2 Test Sequence

`Motor stopped 10 s -> forward 1 s -> stop 3 s -> reverse 1 s -> stop 5 s -> repeat`

## Day 2 Measurements

```text
Manual DRV8870EVM at ~6.94 V:
VM-GND 6.94 V, IN1-GND 4.991 V, IN2-GND 0.00 V
OUT1-OUT2 6.69 V, OUT1-GND 6.83 V, OUT2-GND 0.32 V
Firmware-controlled:
Forward: IN1-GND ~3.202 V, OUT1-OUT2 ~+6.72 V, motor spins forward
Stop:    IN1/IN2 ~0 V, OUT1-OUT2 ~0 V, motor stops/coasts
Reverse: IN2-GND ~3.202 V, OUT1-OUT2 ~-6.71 V, motor spins reverse
```

## Bring-up Note

The motor spins directly from 6 V, but the DRV8870EVM did not drive OUT1 / OUT2 reliably at exactly 6.0 V. It worked around 6.94-7.0 V with current limiting. Future testing will use PWM duty limiting.

## Evidence

Day 2 evidence is stored in `evidence/day2_open_loop_motor/`: demo video, wiring diagram, hardware photo, build/flash screenshots, monitor logs, and multimeter photos.

## Safety / Next Steps

Turn the bench supply off before wiring changes, keep all grounds common, do not connect motor VM to ESP8266 3V3/VIN, use current limiting, and keep 7 V tests short.

Completed: ESP8266 RTOS SDK build/flash/monitor, HAL component, DRV8870 manual test, ESP8266-controlled forward/stop/reverse/stop, and Day 2 demo video.

Next: add real PWM duty control, encoder quadrature counting, RPM measurement, and Day 3 PID/control-task work.
