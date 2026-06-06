# Day 2 Bring-up Measurements

## Power setup

Bench supply was set to approximately 7.0 V for DRV8870 bring-up. The DRV8870EVM did not drive the motor output reliably at 6.0 V during manual test, but output stage enabled correctly around 6.94–7.0 V.

## Manual DRV8870EVM test

Manual jumper condition:

- IN1 -> V5P0
- IN2 -> GND

Measured:

- VM-GND: 6.94 V
- IN1-GND: 4.991 V
- IN2-GND: 0.00 V
- OUT1-OUT2: 6.69 V
- OUT1-GND: 6.83 V
- OUT2-GND: 0.32 V

Result: motor spun forward through DRV8870EVM.

## ESP8266 firmware-controlled test

Firmware wiring:

- ESP8266 D5 / GPIO14 -> DRV8870 IN1
- ESP8266 D6 / GPIO12 -> DRV8870 IN2
- ESP8266 GND -> DRV8870 GND

Measured during forward command:

- IN1-GND: approximately 3.202 V
- IN2-GND: approximately 0 V
- OUT1-OUT2: approximately +6.72 V
- Result: motor spun forward.

Measured during stop command:

- IN1-GND: approximately 0 V
- IN2-GND: approximately 0 V
- OUT1-OUT2: approximately 0 V
- Result: motor stopped/coasted.

Measured during reverse command:

- IN1-GND: approximately 0 V
- IN2-GND: approximately 3.202 V
- OUT1-OUT2: approximately -6.71 V
- Result: motor spun reverse.

## Notes

Multimeter readings briefly hover during switching because the motor command duration is short, the motor is inductive, and the meter averages changing voltage.

## Logic Analyzer Capture

Saleae Logic Pro 8 was connected to the motor-control and encoder signals.

Captured signals:

- IN2 / ESP8266 D6 / GPIO12
- Encoder A / ESP8266 D1 / GPIO5
- Encoder B / ESP8266 D2 / GPIO4

The capture shows IN2 command activity during reverse motion and encoder A/B transitions while the motor is moving. IN1 / ESP8266 D5 / GPIO14 forward command was verified separately using a multimeter at approximately 3.200 V during the forward command window, and also verified by observed forward motor rotation in the Day 2 demo video.