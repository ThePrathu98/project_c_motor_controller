# Project C — Days 3–4 Measurements and Evidence Notes

## Hardware Setup and Pin Connections

| Category | Signal / Wire | Connection | Purpose |
|---|---|---|---|
| ESP8266 to DRV8870 | D5 / GPIO14 | DRV8870 IN1 | PWM motor drive input |
| ESP8266 to DRV8870 | D6 / GPIO12 | DRV8870 IN2 | Direction / complementary drive input |
| Common Ground | ESP8266 GND | DRV8870 GND and bench supply negative | Shared logic/power reference |
| Motor Power | Bench supply + | DRV8870 VM | Motor supply input |
| Motor Power | Bench supply - | DRV8870 GND | Motor supply return |
| Motor Leads | Motor red / black | DRV8870 OUT1 / OUT2 | Motor output terminals |
| Encoder Power | Blue wire | ESP8266 3V3 | Encoder supply |
| Encoder Ground | Green wire | ESP8266 GND / common GND | Encoder ground |
| Encoder Feedback | Yellow wire / Encoder A | ESP8266 D1 / GPIO5 | Encoder phase A |
| Encoder Feedback | White wire / Encoder B | ESP8266 D2 / GPIO4 | Encoder phase B |
| Saleae D0 | Probe D0 | IN1 / GPIO14 | PWM observation |
| Saleae D1 | Probe D1 | IN2 / GPIO12 | Direction/complementary input observation |
| Saleae D2 | Probe D2 | Encoder A / GPIO5 | Encoder phase A observation |
| Saleae D3 | Probe D3 | Encoder B / GPIO4 | Encoder phase B observation |
| Saleae Ground | Saleae GND | Common GND | Logic analyzer reference |

Final test conditions:

- Bench supply: approximately 6.4 V
- Bench supply current limit: approximately 1.0 A
- DRV8870 VREF: high / approximately 100%
- Saleae channels: D0 = IN1, D1 = IN2, D2 = encoder A, D3 = encoder B

## How to Interpret Saleae Output

- D0 shows PWM on IN1.
- D1 stays mostly low for positive motor direction.
- D2/D3 show encoder quadrature pulses.
- Encoder pulse spacing changes as commanded speed changes.
- Faster speed gives denser encoder transitions.
- Slower speed gives wider encoder transitions.
- Continuous D2/D3 activity means the encoder is reporting continuous motor rotation.

## Final One-Go Test

### Command Sequence

```text
ARM
SET_SPEED 1500
STATUS repeated
SET_SPEED 800
STATUS repeated
SET_SPEED 300
STATUS repeated
STOP
STATUS
DISARM
```

### Final Summary Results

| Target RPM | Final Steady Samples | Final Steady Average | Error |
|---:|---|---:|---:|
| 1500 | 1533, 1549, 1494, 1429, 1485, 1540, 1527, 1551, 1496, 1429 | 1503.3 RPM | 0.22% |
| 800 | 835, 805, 774, 776, 831, 831, 820, 759, 809, 840 | 808.0 RPM | 1.00% |
| 300 | 318, 316, 312, 292, 294, 296, 312, 296, 288, 303 | 302.7 RPM | 0.90% |

### Pass Band

The rubric requires steady-state error below 3%.

| Target RPM | 3% Pass Band |
|---:|---|
| 1500 | 1455 to 1545 RPM |
| 800 | 776 to 824 RPM |
| 300 | 291 to 309 RPM |

The final steady-state averages are inside the required error limit.

## Step Response Test

### Command Sequence

```text
ARM
SET_SPEED 300
STATUS repeated for warm-up
SET_SPEED 500
STATUS repeated
SET_SPEED 1500
STATUS repeated
STOP
STATUS
DISARM
```

### 500 RPM Region

Observed 500 RPM values included:

```text
423, 473, 475, 480, 497, 497, 488, 482, 484, 495
```

The motor moved and approached the 500 RPM baseline.

### 1500 RPM Region After Step

Observed values after stepping to 1500 RPM included:

```text
1996, 1878, 1608, 1555, 1490, 1531, 1542, 1514, 1512, 1485, 1564, 1560
```

The step response shows an initial overshoot followed by settling near 1500 RPM.

## Step-Response Metrics to Report

The rubric asks for:

- Rise time less than 1.5 s
- Overshoot less than 15%
- Settling less than 2 s

I have generated CSV and plot to measure these from the actual step-response run. If the first step sample overshoots above the 15% limit, I have documented it honestly as a tuning limitation while still showing that the final steady-state velocity hold requirement passed.

## Git Bash Monitor Lines to Show

Important lines from the serial monitor:

```text
app_main: Project C Day 3-4 full firmware starting
motor_hal: IN1 GPIO=14, IN2 GPIO=12, PWM period=50 us
encoder_hal: Encoder A-edge counting enabled: A=GPIO5, B=GPIO4
control_task: 1 kHz hw_timer -> binary semaphore control loop started
command_server: Wi-Fi connected, IP=192.168.1.101
command_server: TCP command server listening on port 5005
```

These prove:

- Firmware booted.
- PWM hardware initialized.
- Encoder GPIOs initialized.
- 1 kHz timer/semaphore control loop is active.
- Wi-Fi is connected.
- TCP command server is ready.

## How to Read PowerShell STATUS Output

Example:

```text
STATUS -> OK STATUS state=RUNNING cmd=300 target=300 actual=303 duty=56 error=-3 delta=-139 missed=1528 step=0
```

Meaning:

- `state=RUNNING`: controller is active.
- `cmd=300`: command requested by the TCP client.
- `target=300`: slew-limited target.
- `actual=303`: measured RPM.
- `duty=56`: PWM duty sent to motor driver.
- `error=-3`: target minus actual.
- `delta=-139`: encoder count delta.
- `missed=1528`: missed control/semaphore count.
- `step=0`: step-test mode inactive.

## Required Evidence Checklist

- Final one-go PowerShell log
- Final one-go summary file
- Final Git Bash monitor log
- Final step-response PowerShell log
- Step-response CSV
- Step-response plot PNG
- README paragraph on gain choices
- Saleae wide screenshot
- Saleae zoomed PWM screenshot
- Saleae zoomed encoder A/B screenshot
- Photo/video of hardware wiring
- Photo/video of bench supply settings
- Final 16–20 minute demo video
