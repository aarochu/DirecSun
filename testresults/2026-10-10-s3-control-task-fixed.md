# Test: 500 Hz control task after the loop-time and gyro-bias fixes, 697 s on the XIAO ESP32-S3 (R3.4)

- Date/time and timezone: 2026-10-10, 17:44:06 (America/New_York), 697 s of status lines (11.6 min).
- Operators and witnesses: Julian (bench rig; the board was moved at least twice during the run, once at about 195 s and once after 630 s); Julian's Claude Code agent (build, flash, serial capture, analysis).
- Julian's explicit go for any motor-turning step: Not applicable. DShot 0 only; no ESC or motor was connected.
- Component model, markings, and serial/lot: XIAO ESP32-S3 (MAC e0:72:a1:fc:49:5c) with the three I²C sensors, all answering.
- Firmware branch/commit and ESP32 target: `feat/set3/R3.4-control-task`, flight build. The boot log's version string is `345e5f7-dirty` (compiled 17:41:32). Its gyro-seed line ("accel reference ... (norm ...)") is the stillness fix committed next as 873b5e3, so the uncommitted change was that fix. That last point is an inference from the log text. ESP-IDF v6.0, esp32s3.
- Procedure and acceptance-criterion reference: `changes/3-teleop/R3.4-control-task.md`, Tests: tick jitter (BC-3), timing budget (FD F-4), gyro bias (FD §11.1, V-61).
- Rig setup, wiring revision, guard, and stop method: the breadboard rig, board flat and mostly still. Battery, sun and Hall inputs unconnected. Nothing can move.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check: the firmware's status line. The same counters read 2 missed ticks and a 6934 µs tick in [the first run](2026-10-10-s3-control-task-first-run.md), so they do see misses and slow ticks.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Ticks per 10 s window (688 windows) | 5000 to 5001 in every window; 0 missed ticks over 697 s | 5000 ±1, no missed ticks | Pass |
| 2 | Tick jitter | p99 deviation 0 µs every second; max deviation 4 µs | p99 under 50 µs | Pass |
| 3 | Worst tick (`loop_us_max`), every sensor read, barometer answering in 697 of 697 status lines | 976 µs in the first second, 911 to 926 µs in every later second | Under 1000 µs | Pass **without Wi-Fi**. The spec item also requires Wi-Fi connected, which R3.4 does not have, so the item stays open until the R3.5 build. Margin about 25 µs |
| 4 | Gyro bias seed at boot | "seeded at -2.083 deg/s from 424 still samples (range 0.244); accel reference 0.871 g" | Seeded while still | Pass |
| 5 | Heading at rest | Longest stretch without handling: 429 s (201 to 630 s), heading constant at -3.5°. First 61 s: -0.0° to -0.1°. One handling event at about 195 s | No drift at rest | Pass |
| 6 | DShot frames out of GPIO7 | 349019 sent, 500.0 per second, 0 dropped | 500/s, 0 dropped | Pass for the transmit counter only. No ESC was connected |

## Outcome

- Overall: **Pass** for jitter and the gyro bias; **pass without Wi-Fi** for the timing budget.
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - None in the firmware.
  - The worst tick leaves about 25 µs of margin. The once-a-second barometer read was later shown to cost about 135 µs of it; see [2026-10-10-s3-loop-time-barometer.md](2026-10-10-s3-loop-time-barometer.md).
- Raw data and photos: [serial log](2026-10-10-s3-control-task-fixed/serial-flight-697s.txt).
- Follow-up owner and action:
  - Julian's agent: re-run the timing budget on the R3.5 build with Wi-Fi connected and a client streaming.
  - Julian (oscilloscope): the DShot gap-free item; the scope on D8.
