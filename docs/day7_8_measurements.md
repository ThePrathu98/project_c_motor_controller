# Day 7-8 Measurements — PyQt6 GUI + Live Telemetry

## Test environment

| Item | Value |
|---|---|
| MCU | ESP8266 NodeMCU / ESP-12E |
| SDK | ESP8266 RTOS SDK v3.4 |
| Motor driver | TI DRV8870EVM |
| Motor | Pololu 25D 6 V gearmotor with encoder |
| Level shifter | TXS0108E 8-channel module |
| Motor supply | about 6.4 V, 1.0 A current limit |
| Command port | TCP `5005` |
| Telemetry port | TCP `5006` |
| GUI | PyQt6 + pyqtgraph |
| Logic analyzer | Saleae Logic Pro 8 |

## Electrical checks before GUI run

| Measurement | Observed result | Meaning |
|---|---:|---|
| TXS `VA` to GND | about 3.08 V | ESP8266/TXS low-side rail present |
| TXS `OE` to GND | about 3.08 V | TXS output enable asserted |
| TXS `VB` to GND | about 4.99 V | DRV8870/TXS high-side rail present |
| Manual TXS forward test `B1` high, `B2` low | motor spins fast, about 0.3 A bench current | DRV8870 and motor path are functional |
| Final GUI run at 6.4 V | motor spins from GUI commands | level-shifted ESP8266 command path is functional |

## GUI command results

| Commanded speed | GUI state | Fault | Observed behavior |
|---:|---|---|---|
| 300 RPM | RUNNING | NONE | motor spins; actual RPM settles near 300 after startup transient |
| 500 RPM | RUNNING | NONE | motor speed increases; actual RPM near commanded range |
| 800 RPM | RUNNING | NONE | motor speed increases again; actual RPM near 800 RPM in the clean run |
| 1500 RPM | RUNNING | NONE in captured run | motor runs, but overshoot/ripple remains; leave for Day 9 tuning |
| STOP | ARMED/IDLE | NONE | duty goes to 0 and motor stops |
| CLEAR_FAULT | IDLE | NONE after safe current | fault clears and system can be armed again |

## Representative screenshots in evidence folder

```text
evidence/day7_8_PyQt6_GUI_pyqtgraph_live_plot/
```

| File | What it proves |
|---|---|
| `GUI_300rpm_1_working.png` | 300 RPM command from GUI, RUNNING state, current plot active |
| `GUI_300rpm_2_working.png` | 300 RPM settling closer to target |
| `GUI_500rpm_working.png` | mixed-use speed change to 500 RPM |
| `GUI_800rpm_working.png` | mixed-use speed change to 800 RPM |
| `GUI_1500rpm_working.png` | high-speed command path works, with tuning limitation noted |
| `GUI_stall.png` | red fault block and event-history entry |
| `Saleae_logic_300rpm_working.png` | IN1/IN2 and encoder activity during 300 RPM run |
| `Saleae_logic_800rpm_working.png` | logic-analyzer proof during 800 RPM run |
| `Saleae_logic_1500rpm_working.png` | logic-analyzer proof during 1500 RPM run |

## Current observations from GUI

Approximate values from the Day 7-8 GUI screenshots and bench observations:

| Condition | Current observation |
|---|---:|
| Idle, no motor command | about 35-45 mA shown by GUI current estimate |
| 300 RPM running | about 100-200 mA typical GUI current estimate |
| 500 RPM running | about 100-200 mA typical GUI current estimate |
| 800 RPM running | about 200-300 mA typical GUI current estimate |
| 1500 RPM running | about 0.2 A bench current, GUI current around 300 mA in captured run |
| Hard transient / fault event | short spike can approach the fault threshold before PWM is disabled |

The GUI current is an estimate based on the DRV8870EVM ISEN pad into ESP8266 A0. The bench supply current is slower and lower-resolution, so it is useful for rough confirmation but not for fast transient timing.

## Saleae observations

Expected Saleae behavior during a successful run:

- D0 / IN1 is active during forward drive.
- D1 / IN2 stays low for the tested forward direction except for brief transitions.
- D2 / encoder A toggles while the motor spins.
- D3 / encoder B toggles while the motor spins.
- Encoder toggling frequency increases as commanded RPM increases.

If D0/D1 change but D2/D3 do not, the driver may be commanded but the motor is not rotating. If D2/D3 toggle while the GUI shows actual RPM, the encoder feedback path is working.

## Telemetry and reconnect observations

| Check | Expected result |
|---|---|
| `Test-NetConnection $ip -Port 5005` | `TcpTestSucceeded : True` |
| `Test-NetConnection $ip -Port 5006` | `TcpTestSucceeded : True` |
| GUI startup | `telemetry connected` event appears |
| Telemetry drop | GUI remains responsive and logs reconnect attempt |
| Telemetry reconnect | GUI logs `telemetry connected` again |
| Sequence check | gaps are logged if frames are missed within one continuous connection |

The GUI resets the sequence baseline after reconnect so reconnect downtime is not counted as a continuous-stream packet gap.

## Day 7-8 acceptance summary

| Day 7-8 brief item | Evidence/status |
|---|---|
| PyQt6 GUI with pyqtgraph rolling plot | implemented in `host_gui/project_c_motor_gui.py` |
| Target + actual RPM on one plot | top GUI plot |
| Current below | lower GUI plot |
| Colored state/fault block | right-side state block |
| START / STOP / CLEAR_FAULT buttons | command group in GUI |
| Speed input + Send | RPM spinbox and `Send SET_SPEED` button |
| Fault history list | right-side list, newest event first |
| Telemetry receiver in QThread | `TelemetryThread` class |
| Auto-reconnect on telemetry drop | reconnect loop with 10 s retry |
| Mixed-use RPM test | 300/500/800 RPM captured |
| Stall/fault + clear | red fault screenshot and CLEAR_FAULT path |

## Notes for Day 9

The system is ready for the Day 9 tuning pass. Recommended next measurements:

1. Log a clean 300 -> 800 -> 1500 step sequence.
2. Reduce high-speed overshoot by adjusting feed-forward and PI gains.
3. Add a small documented tuning table in `tuning_notes.md`.
4. Run the 10-minute soak requested by the project brief.
