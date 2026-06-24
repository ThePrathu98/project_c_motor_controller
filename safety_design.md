# Safety Design

Project C uses a four-state motor-control safety model: `IDLE`, `ARMED`, `RUNNING`, and `FAULT`. The 1 kHz control task is the single owner of motor PWM output. The TCP command server and PyQt6 GUI can request state changes, but the control task decides whether the motor is allowed to run.

## State behavior

In `IDLE`, the motor output is disabled and no speed command is active. `ARM` moves the system to `ARMED`. In `ARMED`, the controller is ready but still commands zero motor duty until a valid speed command is received. `SET_SPEED <rpm>` moves the system into `RUNNING`, where the PI controller drives the motor toward the requested RPM. If a fault is detected, the system enters `FAULT`, immediately commands zero duty, and keeps the motor output disabled until the unsafe condition is removed and `CLEAR_FAULT` succeeds.

## Fault triggers

The final verified fault path is `STALL`. A stall is detected when the controller is commanding motion, the requested speed is above the low-speed threshold, duty is high enough to be driving the motor, and encoder-measured speed remains near zero long enough to indicate no motion. In the final test, the stall was triggered by commanding motor speed and turning off the motor VM supply while the ESP8266 remained powered. The firmware latched `fault_name=STALL`, reported actual RPM as 0, commanded duty to 0, and kept the integrator at 0.

The firmware also includes an overcurrent path using the DRV8870EVM ISEN signal measured through ESP8266 A0. This current estimate is logged in telemetry and used for bench-level safety checks. In the final bench setup, the live demonstrated fault path was STALL, because it was repeatable and safe to trigger without forcing the motor shaft by hand.

## Response timing and behavior

The control loop runs at 1 kHz. Fault checks are evaluated inside the control task, and once a fault is latched, the control task disables motor PWM output on every tick while the system remains in `FAULT`. During the final STALL proof, the GUI showed `STATE: FAULT`, `FAULT: STALL`, actual RPM at 0, duty at 0, and the integrator value at 0. This verified that the fault path stopped motor drive and prevented integrator windup.

## Recovery rules

Fault recovery is intentionally explicit. The unsafe condition must first be removed. Then the host sends `CLEAR_FAULT`. If the fault condition is no longer present, the system returns to `IDLE` with `fault_name=NONE`. The operator must then send `ARM` again before sending a new speed command.

The final recovery sequence was:

```text
CLEAR_FAULT
ARM
SET_SPEED 800
```

The motor recovered to approximately 800 RPM with `fault_name=NONE`, and the missed deadline count returned to 0 during normal running.
