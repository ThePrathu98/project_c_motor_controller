# Limitations

This rig is a bench-portable reference motor controller, not a 
production motor controller. The PID gains are tuned for the specific
 ESP8266 NodeMCU, DRV8870EVM, Pololu 25D 6 V encoder motor, 6.4 V bench
 supply, and 1.0 A current limit used in the final tests. Current is
 estimated from the DRV8870EVM ISEN signal through ESP8266 A0 and is
 useful for logging and bench-level safety, but it is not a calibrated
 current measurement. The DRV8870 nFAULT signal is not wired to a 
 dedicated ESP8266 interrupt input in this setup, so the final 
 verified fault response is software STALL detection using encoder
 feedback and commanded duty. The PyQt6 GUI is a development and demo
 interface, not a safety-rated operator panel. Future work would 
 include calibrated current sensing, direct nFAULT interrupt wiring,
 formal response-time measurement, persistent configuration storage,
 and broader tuning across different motors and supply voltages.
