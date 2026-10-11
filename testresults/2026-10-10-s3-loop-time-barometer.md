# Test: does the barometer read stretch the control tick? Same-binary A/B on the XIAO ESP32-S3 (R3.4)

- Date/time and timezone: 2026-10-10, three runs at 17:56, 18:05 and 18:24 (America/New_York).
- Operators and witnesses: Julian (bench rig, BMP280 rewire); Julian's Claude Code agent (build, flash, serial capture, analysis).
- Julian's explicit go for any motor-turning step: Not applicable. No ESC or motor was connected.
- Component model, markings, and serial/lot: XIAO ESP32-S3 (MAC e0:72:a1:fc:49:5c) with the three I²C sensors.
- Firmware branch/commit and ESP32 target: `feat/set3/R3.4-control-task`, diagnostic build (`sdkconfig.diag`: `CONFIG_DIRECSUN_BENCH_STAGE_TIMING=y`, which prints each stage's per-second max and mean). **The same binary in all three runs:** compile time 17:28:26, ELF SHA256 8f07bf892…. ESP-IDF v6.0, esp32s3.
- Procedure and acceptance-criterion reference: diagnosis for the R3.4 timing budget (FD F-4). The 976 µs worst tick left about 25 µs of margin, and the IMU read's once-a-second maximum had varied between builds.
- Rig setup, wiring revision, guard, and stop method: the breadboard rig. The only change between runs is whether the BMP280 answers (it dropped off after a replug and was rewired; see [2026-10-10-s3-i2c-scan-bmp280.md](2026-10-10-s3-i2c-scan-bmp280.md)). The board's orientation also changed (tilt 4° to 170°), which does not affect bus timing.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check: the stage-timing lines. Because the binary is identical, any difference between runs comes from the hardware state, not the code.

## Steps and observations

Per-second figures, with the first second excluded.

| Step | Condition | IMU read max | IMU read mean | Worst tick (median / max) | Missed ticks | Result |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Barometer answering, 17:56, 41 s (`baro ok` 42 of 42) | 655 to 687 µs | 456 µs | 903 / 935 µs | 0 | Baseline |
| 2 | Barometer silent (no acknowledge, so its task never starts), 18:05, 142 s | 459 to 462 µs | 454 µs | 769 / 774 µs | 0 | Spike gone |
| 3 | Barometer answering again after the rewire, 18:24, 147 s (`baro ok` 148 of 148) | 657 to 691 µs | 455 µs | 905 / 938 µs | 0 | Spike back |

## Outcome

- Overall: **Hypothesis confirmed in both directions.** The mean IMU read does not change. Only the once-a-second maximum moves with the barometer, by about 200 to 230 µs: its 1 Hz transactions on the shared bus delay the control task's IMU read. If something else caused the spike, run 2 would still show it.
- Faults, unexpected motion, heat, noise, or supply current limiting: none. This is a design margin, not a failure. The flight build passes its budget (976 µs against 1000 µs) with the barometer answering.
- Raw data and photos: [run 1](2026-10-10-s3-loop-time-barometer/1-baro-answering-1756.txt), [run 2](2026-10-10-s3-loop-time-barometer/2-baro-silent-1805.txt), [run 3](2026-10-10-s3-loop-time-barometer/3-baro-answering-again-1824.txt).
- Follow-up owner and action: Julian's agent. Move the barometer read off the IMU's path before R3.5's Wi-Fi is measured: either read it from the control task on a light tick, or start the barometer task's read just after the IMU read. That frees about 135 µs of worst-tick margin in the diagnostic build; the flight-build gain is not measured.
