# Test: 500 Hz control task, first run on the XIAO ESP32-S3 (R3.4)

- Date/time and timezone: 2026-10-10, 17:12:51 (America/New_York), 67 s of status lines.
- Operators and witnesses: Julian (bench rig); Julian's Claude Code agent (build, flash, serial capture, analysis).
- Julian's explicit go for any motor-turning step: Not applicable. The flight build sends DShot 0 only and cannot arm without comms; no ESC or motor was connected.
- Component model, markings, and serial/lot: XIAO ESP32-S3 (MAC e0:72:a1:fc:49:5c) with the three I²C sensors.
- Firmware branch/commit and ESP32 target: `feat/set3/R3.4-control-task` at 1128a41 (flight build), ESP-IDF v6.0, esp32s3.
- Procedure and acceptance-criterion reference: `changes/3-teleop/R3.4-control-task.md`, Tests: tick jitter (BC-3), timing budget under 1000 µs (FD F-4), and the gyro bias seed (FD §11.1).
- Rig setup, wiring revision, guard, and stop method: the breadboard rig, board flat and still. Battery, sun and Hall inputs unconnected. Nothing can move.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check: the firmware's once-a-second status line (`ticks`, `missed`, `loop_us_max`, `p99_dev_us`, DShot counters). This run's 2 missed ticks in the first second show the missed-tick counter can see a miss.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Ticks per second | 500 or 501 every second; p99 deviation 3 to 8 µs | 500 ±1, p99 under 50 µs | Pass |
| 2 | Missed ticks | 2, both in the first second | 0 | Fail |
| 3 | Worst tick (`loop_us_max`) | 6934 µs in the first second, then 1363 to 1813 µs in every second | Under 1000 µs | Fail |
| 4 | Gyro bias seed at boot | "gyro z bias not seeded (425 samples, range 0.122 deg/s)" | Seeded while the board is still | Fail |
| 5 | Heading at rest | Rate -2.13 deg/s; heading drifted from -2.5° to -139.7° over the run | Near-constant heading at rest | Fail (follows from step 4) |
| 6 | DShot frames out of GPIO7 | 34016 sent by the end, 0 dropped | About 500/s, 0 dropped | Pass for the transmit counter only. No ESC was connected, so nothing confirms reception |

## Outcome

- Overall: **Fail.** Two defects were found. Both were fixed and re-verified in [2026-10-10-s3-control-task-fixed.md](2026-10-10-s3-control-task-fixed.md).
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - **Loop time.** Diagnostic builds traced it to three causes. The 16 KB instruction cache could not hold the tick's code. The magnetometer status and data reads, plus an ADC read, shared one tick. A log line on the first tick cost 6.9 ms.
  - **Gyro bias never seeded.** FD §11.1 tested stillness as |a| within 0.05 g of 1 g, and this MPU-6050 reads 0.871 g at rest (see [2026-10-10-s3-sensor-bus.md](2026-10-10-s3-sensor-bus.md)). The boot log blamed motion, which was misleading.
  - The battery reading (5.09 V boot average, BATTERY_LOW latched) came from an unconnected pin and is not a finding; see [2026-10-10-s3-adc-unwired.md](2026-10-10-s3-adc-unwired.md).
- Raw data and photos: [serial log](2026-10-10-s3-control-task-first-run/serial-flight.txt).
- Follow-up owner and action: Julian's agent. Fixes 345e5f7 (loop time), 873b5e3 (stillness judged against the boot window's own accel norm, FD V-61), and FD V-60 and the worst-tick figures (cef5409, 19e1dbd).
