# Project C Day 2 Bring-up Notes

## Goal

Bring up the ESP8266 RTOS SDK firmware, HAL layer, DRV8870EVM motor driver, and Pololu 25D encoder motor for open-loop motor control.

## Hardware

- ESP8266 NodeMCU / ESP-12E
- TI DRV8870EVM motor driver
- Pololu 25D 6 V gearmotor with encoder
- Bench power supply
- Multimeter
- Breadboard and jumper wires

## Pin map

| Signal | Connection |
|---|---|
| ESP8266 D5 / GPIO14 | DRV8870 IN1 |
| ESP8266 D6 / GPIO12 | DRV8870 IN2 |
| ESP8266 GND | DRV8870 GND |
| Encoder blue | ESP8266 3V3 |
| Encoder green | ESP8266 GND |
| Encoder yellow | ESP8266 D1 / GPIO5 |
| Encoder white | ESP8266 D2 / GPIO4 |
| Bench supply + | DRV8870 VM |
| Bench supply - | DRV8870 GND |
| Motor red | DRV8870 OUT1 |
| Motor black | DRV8870 OUT2 |

## Bring-up result

The motor now responds under ESP8266 firmware control:

- Forward command: motor spins forward.
- Stop command: motor stops/coasts.
- Reverse command: motor spins reverse.
- Stop command: motor stops/coasts.

## Measurements

Manual DRV8870EVM test at approximately 6.94 V:

- VM-GND: 6.94 V
- IN1-GND: 4.991 V
- IN2-GND: 0.00 V
- OUT1-OUT2: 6.69 V
- OUT1-GND: 6.83 V
- OUT2-GND: 0.32 V

ESP8266 firmware-controlled test:

- IN1-GND during forward: approximately 3.202 V
- IN2-GND during reverse: approximately 3.202 V
- OUT1-OUT2 during forward: approximately +6.72 V
- OUT1-OUT2 during reverse: approximately -6.71 V
- OUT1-OUT2 during stop: approximately 0 V

## Important note

The motor itself spins from 6 V directly, but the DRV8870EVM did not drive the output reliably at exactly 6.0 V during bring-up. The DRV8870EVM output stage worked correctly around 6.94–7.0 V with current limiting enabled. Short bring-up tests were performed at approximately 7.0 V.

## Firmware status

Implemented:

- `motor_hal_init()`
- `motor_hal_set_duty()`
- `motor_hal_stop()`
- `encoder_hal_init()`
- raw encoder A/B reads

Current test sequence:

```text
Forward 1 second
Stop 3 seconds
Reverse 1 second
Stop 5 seconds
Repeat