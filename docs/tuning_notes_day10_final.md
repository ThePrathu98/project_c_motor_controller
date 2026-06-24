# Day 10 Final Tuning Notes

## Final gains

The final controller gains are:

- Kp = 0.0025
- Ki = 0.004
- Kd = 0.000
- Kaw = 0.200

The PID output correction is clamped to a small trim range around the base feed-forward duty. This keeps the controller from commanding excessive duty during startup, disturbances, or fault conditions.

## Tuning summary

The motor was first tested with proportional control to find a gain range that produced visible response without unstable oscillation. A more aggressive proportional gain produced faster response but increased overshoot at the 1500 RPM step. The final Kp value was backed off to 0.0025 for a more stable response.

Integral gain was added to reduce steady-state error at the main operating points. Ki=0.004 was kept because the motor held 500 RPM and 1500 RPM close enough for the final soak while avoiding sustained oscillation. Derivative gain was kept at 0 because the encoder-derived RPM estimate is quantized and noisy at low speed.

Anti-windup back-calculation was kept enabled using Kaw=0.200. During the final STALL test, the system reported `pid_i_x1000=0` while in FAULT, confirming that the integrator was not allowed to ramp while the motor was disabled.

## Final behavior

The final `STEP_TEST` showed a 500 RPM to 1500 RPM transition with documented overshoot and recovery. The final soak alternated 500 RPM and 1500 RPM for 15 cycles. Normal running sections reported `missed=0`, `fault_name=NONE`, and `wifi_reconnects=0`.

Final soak highlights:
- 500 RPM sections stayed near the command, with final observed value around 488 RPM.
- 1500 RPM sections stayed near the command, with final observed value around 1514 RPM.
- Peak current during soak was approximately 449 mA.
- The final STOP/DISARM sequence returned the controller to IDLE with fault NONE.
- The standalone STALL proof showed clear fault latch and recovery behavior.

## Final tuning conclusion

The final gains are acceptable for the Project C bench rig. They provide stable closed-loop control for the tested 500 RPM, 800 RPM, and 1500 RPM commands, allow recovery after a STALL fault, and support the final 10-minute soak without missed control deadlines during normal running.
