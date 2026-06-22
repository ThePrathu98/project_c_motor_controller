# Day 7-8 Details — PyQt6 GUI + pyqtgraph Live Plot

## Goal

Day 7-8 extends the motor controller from firmware-only/socket-only testing into a single operator GUI. The GUI must make the whole system reachable from one window: command buttons, speed input, live telemetry plots, current view, state/fault view, fault history, and telemetry reconnect behavior.

The recorded 25-minute demo should be evaluated mainly as a GUI/telemetry checkpoint. PID tuning is still expected to continue on Day 9.

## Final hardware configuration used

### Motor power

- Bench supply positive -> DRV8870EVM `VM`.
- Bench supply negative -> DRV8870EVM `GND`.
- Demo supply setting: about `6.4 V`, `1.0 A` current limit.
- Motor red/black -> DRV8870EVM `OUT1` / `OUT2`.

### Logic-drive path

A TXS0108E level shifter was added after testing showed the ESP8266 3.3 V GPIO drive was not consistently producing a usable DRV8870 input command through the EVM input path.

Final connections:

| Connection | Purpose |
|---|---|
| TXS `VA` -> ESP8266 `3V3` | low-side reference for ESP8266 logic |
| TXS `OE` -> ESP8266 `3V3` | enables the level shifter |
| TXS `GND` -> common GND | common logic reference |
| TXS `VB` -> DRV8870EVM `V5P0` | high-side reference for DRV8870 input logic |
| ESP8266 `D5/GPIO14` -> TXS `A1` -> TXS `B1` -> DRV8870 `IN1` | forward PWM input |
| ESP8266 `D6/GPIO12` -> TXS `A2` -> TXS `B2` -> DRV8870 `IN2` | direction/complementary input |

The level shifter header was soldered before final testing. Before soldering, intermittent breadboard/header contact caused inconsistent behavior: pressing the board or meter probe sometimes made the motor spin, proving the electrical path was marginal rather than a purely firmware issue.

### Encoder path

| Encoder wire | Connection |
|---|---|
| Blue | ESP8266 `3V3` |
| Green | common GND |
| Yellow / A | ESP8266 `D1/GPIO5` |
| White / B | ESP8266 `D2/GPIO4` |

## GUI implementation

File:

```text
host_gui/project_c_motor_gui.py
```

Important implementation points:

1. `TelemetryThread` is a `QThread`, so blocking socket reads happen outside the GUI thread.
2. Commands use text TCP requests on port `5005`.
3. Telemetry uses fixed 20-byte binary frames on port `5006`.
4. The GUI redraws plots at about 30 Hz even though telemetry arrives at 100 Hz. This keeps the GUI smooth and avoids unnecessary redraw load.
5. The top plot overlays target RPM and actual RPM.
6. The bottom plot shows current in milliamps.
7. The state/fault block is color-coded:
   - gray = IDLE
   - purple = ARMED
   - green = RUNNING
   - red = FAULT
8. The history list stores the newest 10 events, including command replies, state changes, fault changes, reconnects, and sequence gaps.
9. Telemetry reconnects automatically after a socket drop. The reconnect message is logged, and sequence checking is restarted for the new connection.

## Binary telemetry frame

The GUI decodes the firmware telemetry using this Python struct format:

```python
FRAME = struct.Struct("<IhhhhBBHI")
```

Frame fields:

| Field | Type | Meaning |
|---|---|---|
| `seq` | `uint32_t` | sequence number |
| `target` | `int16_t` | internal target RPM |
| `actual` | `int16_t` | measured RPM |
| `duty_permille` | `int16_t` | duty percent x 10 |
| `current_ma` | `int16_t` | current estimate in mA |
| `state` | `uint8_t` | IDLE/ARMED/RUNNING/FAULT enum |
| `faults` | `uint8_t` | bitmask for overcurrent/stall/driver fault |
| `missed` | `uint16_t` | low 16 bits of missed control deadline counter |
| `uptime_ms` | `uint32_t` | control tick uptime in ms |

This fixed frame is intentionally small and avoids dynamic string allocation in the telemetry path.

## Firmware path used by the GUI

Command flow:

```text
PyQt6 button -> TCP port 5005 -> command_server.c -> control_task.c public API -> motor_hal.c
```

Telemetry flow:

```text
control_task.c snapshot -> telemetry_server.c 20-byte frame -> TCP port 5006 -> TelemetryThread -> GUI plots/state/history
```

This separation is important because the GUI never touches motor hardware directly. It only sends the same command protocol that PowerShell can send.

## Control behavior shown in the demo

The final Day 7-8 demo should use the cleanest stable speeds:

```text
300 RPM -> 500 RPM -> 800 RPM -> STOP -> DISARM
```

Expected observations:

- At 300 RPM, the motor starts and settles near the command after a startup transient.
- At 500 RPM, the motor speed increases and the actual RPM follows the target.
- At 800 RPM, the motor reaches the higher command range and the current plot remains active.
- STOP returns duty to zero and the state returns to ARMED or IDLE depending on the command sequence.
- CLEAR_FAULT returns the controller to IDLE when current is below the clear threshold.

The 1500 RPM run is included as evidence, but it still shows overshoot/ripple. That is acceptable to document honestly before Day 9 PID tuning.

## Why the level shifter fixed the major blocker

The earlier problem looked confusing because manual driver tests worked, but ESP8266-controlled tests often did not. The final evidence points to an input-drive/contact issue:

- Manual jumpers from DRV8870 `V5P0` to `IN1` and GND to `IN2` made the motor spin fast.
- ESP8266 direct GPIO drive often produced no motor motion or only a brief jerk.
- The TXS0108E path initially still failed because the unsoldered header/breadboard contact was intermittent.
- Pressing a probe across `VB` and `B1` made the motor spin, proving that `IN1` needed a solid high-level drive.
- After soldering the level shifter header and using the final `A1/B1`, `A2/B2` wiring, the motor ran from GUI commands.

So the final conclusion is: the firmware command path was mostly working, but the DRV8870 input-drive path was unreliable until the level shifter/header connection became solid.

## Safety and fault handling shown by the GUI

The GUI shows both normal states and faults using the same telemetry stream.

Normal transitions:

```text
IDLE -> ARMED -> RUNNING -> STOP -> IDLE/ARMED
```

Fault transition:

```text
RUNNING -> FAULT
```

Recovery:

```text
CLEAR_FAULT -> IDLE
```

The Day 7-8 video should show the red FAULT block and the event history entry. The exact fault may be STALL or OVERCURRENT depending on the physical run, but the important GUI proof is that the fault is visible, logged, and clearable.

## What to say about imperfections

Use this wording in review if asked about the overshoot:

> The Day 7-8 checkpoint is focused on the PyQt6 GUI, live plotting, QThread telemetry, reconnect behavior, and fault visibility. The motor follows 300/500/800 RPM commands from the GUI. The higher-speed PID response still has overshoot and ripple, which I documented as Day 9 tuning work.

That is better than claiming the PID is final.

## Files touched for Day 7-8 cleanup

- `README.md`
- `.gitignore`
- `host_gui/project_c_motor_gui.py`
- `components/project_control/control_task.c`
- `components/project_control/control_task.h`
- `components/project_hal/motor_hal.c`
- `components/project_hal/adc_hal.c`
- `components/project_comm/command_server.c`
- `components/project_comm/telemetry_server.c`
- `main/app_main.c`
- `docs/day7_8_details.md`
- `docs/day7_8_measurements.md`

## Clean project zip notes

The clean zip intentionally excludes generated or local-only content:

- `.git/`
- `build/`
- `host_gui/.venv/`
- Python `__pycache__/`
- `main/wifi_secrets.h`

Keep `main/wifi_secrets.h.template` in the repo and recreate the real `wifi_secrets.h` locally when building.
