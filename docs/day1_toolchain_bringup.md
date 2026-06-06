# Day 1 Toolchain Bring-up

## Goal

Install and verify the ESP8266 RTOS SDK v3.4 toolchain and confirm that `idf.py build`, `idf.py flash`, and `idf.py monitor` work on the ESP8266 NodeMCU / ESP-12E.

## Result

Day 1 completed successfully.

Verified:

- ESP8266 RTOS SDK v3.4 installed at `C:/esp/ESP8266_RTOS_SDK`
- `IDF_PATH` configured in Git Bash
- Python dependency issue fixed by installing compatible setuptools
- `idf.py build` completed successfully
- ESP8266 detected on COM5
- Firmware flashed successfully
- Serial monitor showed ESP8266 bootloader and application output

## Evidence

Day 1 screenshots/logs are stored in:

```text
evidence/day1/