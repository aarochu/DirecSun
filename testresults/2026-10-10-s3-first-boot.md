# Test: XIAO ESP32-S3 first boot of the firmware skeleton (R1.4)

- Date/time and timezone: 2026-10-10, afternoon (America/New_York); the exact time was not recorded.
- Operators and witnesses: Julian (board on USB, board swap); Julian's Claude Code agent (`esptool`, flash, serial monitor).
- Julian's explicit go for any motor-turning step: Not applicable. The skeleton has no DShot code, and no ESC or motor was connected.
- Component model, markings, and serial/lot: Seeed XIAO ESP32-S3. `esptool flash-id`: ESP32-S3 (QFN56) rev v0.2, dual core, 8 MB PSRAM, 8 MB flash, MAC e0:72:a1:fc:49:5c.
- Firmware branch/commit and ESP32 target: `feat/set1/R1.4-firmware-skeleton` at e079631, ESP-IDF v6.0, esp32s3.
- Procedure and acceptance-criterion reference: `changes/1-foundation/R1.4-firmware-skeleton.md`, Tests, hardware item: `flash-id` reports 8 MB, then the banner shows the version, `esp32s3` and `8MB`, with no panic or watchdog reset over 60 s.
- Rig setup, wiring revision, guard, and stop method: the bare XIAO on USB-C. Nothing can move; the stop method is unplugging USB.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check: `esptool flash-id` as the instrument check before flashing. It caught the wrong board: the first board plugged in was an ESP32-C3 (QFN32 rev v0.4, single core, 4 MB, MAC 80:f1:b2:63:46:2c), so nothing was flashed until Julian swapped it.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | `esptool flash-id` on the first board | ESP32-C3, 4 MB | ESP32-S3, 8 MB | Fail (wrong board), caught before flashing |
| 2 | `esptool flash-id` after the swap | ESP32-S3 (QFN56) rev v0.2, "Detected flash size: 8MB" | 8 MB | Pass |
| 3 | `tools/idf.sh -p /dev/ttyACM0 flash` | All three images written and hash-verified | Verified write | Pass |
| 4 | 70 s of `idf.py monitor` | Banner: "DirecSun skeleton", "firmware e079631, ESP-IDF v6.0", "target esp32s3, chip ESP32-S3 rev v0.2, 2 cores", "flash 8MB configured, 8MB detected" | Version, `esp32s3`, `8MB` | Pass |
| 5 | Search the 70 s log for panic, abort, watchdog, backtrace, error and `rst:` lines | Exactly one `rst:` line (the initial `USB_UART_CHIP_RESET`), none of the others | No reset over 60 s | Pass. A reboot would have added a second `rst:` line |

## Outcome

- Overall: **Pass.**
- Faults, unexpected motion, heat, noise, or supply current limiting: none.
- Raw data and photos: **not preserved.** The serial log was in a temporary directory that a machine reboot wiped later that day. The observations above are the ones recorded at the time in the R1.4 Notes / log.
- Follow-up owner and action: none.
