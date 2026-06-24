# Day 10 Final Measurements

## Hardware setup

- MCU: ESP8266 NodeMCU / ESP-12E.
- Firmware framework: ESP8266 RTOS SDK v3.4.
- Motor driver: TI DRV8870EVM.
- Motor: Pololu 25D 6 V brushed DC gearmotor with encoder.
- Motor supply: 6.4 V bench supply.
- Current limit: 1.0 A.
- Host GUI: PyQt6 + pyqtgraph.
- Command socket: TCP port 5005.
- Telemetry socket: TCP port 5006.

## Final wiring summary

- ESP8266 D5 / GPIO14 -> DRV8870 IN1.
- ESP8266 D6 / GPIO12 -> DRV8870 IN2.
- ESP8266 GND -> DRV8870 GND / common ground.
- Bench supply positive -> DRV8870 VM.
- Bench supply negative -> DRV8870 GND.
- Motor leads -> DRV8870 OUT1 / OUT2.
- Encoder blue -> ESP8266 3V3.
- Encoder green -> common ground.
- Encoder yellow A -> ESP8266 D1 / GPIO5.
- Encoder white B -> ESP8266 D2 / GPIO4.
- DRV8870 ISEN -> ESP8266 A0.

## Final controller values

- Kp = 0.0025.
- Ki = 0.004.
- Kd = 0.000.
- Kaw = 0.200.
- Control loop period: 1 ms.
- Speed measurement period: 250 ms.
- GUI telemetry period: 40 ms.

## Final step-response evidence

The final `STEP_TEST` command produced a 500 RPM to 1500 RPM response. The GUI showed target RPM, actual RPM, and current. The firmware monitor printed `step_csv` rows for the response. Peak current during the final step-response run was approximately 467 mA.

Observed behavior:
- 500 RPM section settled near the commanded speed.
- 1500 RPM section showed overshoot and then settled near the commanded speed.
- Final step-response run ended with `fault_name=NONE`, `missed=0`, and `wifi_reconnects=0`.

## Standalone STALL / anti-windup proof

A deliberate no-motion condition was created by turning off the motor VM supply while the ESP8266 remained powered and the controller was commanding motor speed.

Observed fault status:
- `state=FAULT`.
- `cmd=500`.
- `target=500`.
- `actual=0`.
- `duty=0`.
- `current_ma=40`.
- `pid_i_x1000=0`.
- `fault=0x02`.
- `fault_name=STALL`.
- `wifi_reconnects=0`.

Recovery proof:
- `CLEAR_FAULT` returned OK.
- `ARM` returned OK.
- `SET_SPEED 800` returned OK.
- Recovery status showed `state=RUNNING`, `target=800`, `actual=800`, `fault_name=NONE`, and `missed=0`.
- Final STOP/DISARM returned the system to `IDLE`, `fault_name=NONE`.

## Official 10-minute soak result

The official soak alternated 500 RPM and 1500 RPM commands for 15 cycles, with load/stall behavior demonstrated mid-session.

Representative results:
- Cycle 1, 500 RPM: actual approximately 499 RPM, missed=0.
- Cycle 1, 1500 RPM: actual approximately 1518 RPM, missed=0.
- Cycle 15, 500 RPM: actual approximately 488 RPM, missed=0.
- Cycle 15, 1500 RPM: actual approximately 1514 RPM, missed=0.
- Peak current during soak: approximately 449 mA.
- Final state after STOP/DISARM: IDLE.
- Final fault status: NONE.
- Final missed control deadlines: 0.
- Final Wi-Fi reconnect count: 0.
- Final heap delta: approximately 1420 bytes.

## Final conclusion

The final bench run demonstrates closed-loop RPM control, current telemetry, GUI visualization, STALL fault detection, CLEAR_FAULT recovery, zero missed deadlines during normal running, zero Wi-Fi reconnects in the final status logs, and a completed 15-cycle soak.
