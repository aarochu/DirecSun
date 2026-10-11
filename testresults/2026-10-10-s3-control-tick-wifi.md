# Test: control-tick timing with the Wi-Fi radio up, before and after the IRAM fix (R3.4 timing budget, R3.5, FD V-62)

- Date/time and timezone: 2026-10-10, 20:49 to 22:34 (America/New_York): a 10 min baseline, a 150 s stage-timing run, a 198 s experiment, a 10 min run of the fix, 18 boot-only captures, a 3 min run, a 45 min soak (21:37 to 22:22), and a 3 min run of R4.3 with the fix.
- Operators and witnesses: Julian's Claude Code agent (build, flash, serial capture, analysis). Julian was away and had asked the agent to work on what needed no hands; nobody touched the rig. Written up afterwards from the session transcript and the raw captures by a separate Claude Code agent, which recomputed every number below from the captures.
- Julian's explicit go for any motor-turning step: Not applicable. No ESC or motor was connected to the board (the ESC was first powered at 00:55 the next morning). The firmware sends DShot 0 only.
- Component model, markings, and serial/lot: XIAO ESP32-S3, MAC e0:72:a1:fc:49:5c (from every `esptool` flash log), with the MPU-6050, QMC5883P and BMP280 on the STEMMA QT chain. All three answered on every boot.
- Firmware branch/commit and ESP32 target: ESP-IDF v6.0, esp32s3, all builds 0 warnings. Each build adds a SoftAP layer kept outside the repo (an access point with a random WPA2 password, channel 6). The laptop never joined it, so **no client was connected in any run**.
  - Before the fix: `feat/set3/R3.5-comms` at 1a552e4.
  - Stage timing: 1a552e4 plus `sdkconfig.diag` (`CONFIG_DIRECSUN_BENCH_STAGE_TIMING=y`, the only config difference).
  - IRAM config experiment: 1a552e4 plus `CONFIG_FREERTOS_IN_IRAM`, `CONFIG_I2C_ISR_IRAM_SAFE` and `CONFIG_ADC_ONESHOT_CTRL_FUNC_IN_IRAM`.
  - The fix: the working tree committed at 21:37 as 2a62ca1 ("keep the control tick in IRAM with Wi-Fi up (V-62)": `firmware/main/linker.lf` and those three settings in `sdkconfig.defaults`). Its boot log reads `1a552e4-dirty`, because the version string is fixed when CMake configures the build directory.
  - Boot-second experiments: the fix plus `CONFIG_ESP_WIFI_NVS_ENABLED=n`; the fix plus that and PHY calibration storage off; the fix plus an uncommitted change that created the I2C bus and the DShot channel on core 1 (reverted at 21:30).
  - R4.3 with the fix: a scratch merge, 0a86770, of `feat/set4/R4.3-hold-and-slew` (5cb6263) and R3.5 (1797b59). Its tree is identical to the merge pushed at 22:35 as a5a6fa2 (`git diff` is empty).
- Procedure and acceptance-criterion reference:
  - `changes/3-teleop/R3.4-control-task.md`, Tests, timing budget: "`loop_us` max under 1000 µs over 10 min with every sensor read and Wi-Fi connected (FD F-4)". Also the tick-jitter item (BC-3): 5000 ticks ±1 per 10 s, p99 deviation under 50 µs, no missed ticks.
  - FD §9.2 (timing budget) and §23.1 row V-62. TD §6.2 (tasks and timing).
  - The session's own log of these runs is on `feat/set3-teleop`: `changes/3-teleop/R3.5-comms.md` (b95f781 and 8713662).
- Rig setup, wiring revision, guard, and stop method: the breadboard rig on the desk, board flat and still. Nothing could move.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check:
  - The firmware's 1 Hz status line (`loop_us_max`, missed ticks, jitter, DShot frames sent and dropped, invalid IMU reads), read by the raw serial reader `rawts.py` (kept in [2026-10-10-s3-stall-watchdog](2026-10-10-s3-stall-watchdog/rawts.py)). [`status-summary.py`](2026-10-10-s3-control-tick-wifi/status-summary.py) reproduces the numbers below from the files in this folder.
  - Positive control: the same counters report a 4 ms tick and 1 or 2 missed ticks in most first seconds, and 1369 to 1649 µs before the fix. A counter stuck low would show neither.
  - The stage-timing build is not a neutral instrument: with Wi-Fi up it ran faster than the plain build (median worst tick 1076 µs against 1454 µs). Its per-stage figures are used only to compare with and without Wi-Fi in the same build type.
  - [`flashcalls.py`](2026-10-10-s3-control-tick-wifi/flashcalls.py) lists flash-resident code reachable from the control task by disassembly (direct calls and `callx` through `l32r` literals). It is the instrument behind the cause, not a measurement.

## Steps and observations

All worst-tick figures exclude the first second after boot, which step 9 covers.

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Before the fix, SoftAP up, 10 min (20:49:28) | Worst tick 1369 to 1649 µs, median 1454, over 1000 µs in 627 of 627 seconds. 0 missed ticks; 10 s windows 5000 to 5004 ticks; p99 deviation at most 28 µs; DShot 500.0 frames/s, 0 dropped; 1 invalid IMU read; barometer OK in 628 of 628 lines | Worst tick under 1000 µs | **Fail.** R3.4 without Wi-Fi had run 911 to 926 µs ([2026-10-10-s3-control-task-fixed.md](2026-10-10-s3-control-task-fixed.md)) |
| 2 | Stage timing with Wi-Fi up, 150 s (21:00), against the same build type without Wi-Fi at 18:24 ([capture](2026-10-10-s3-loop-time-barometer/3-baro-answering-again-1824.txt)) | Per-second stage maxima, median, with against without Wi-Fi: IMU 643 / 657 µs, magnetometer 406 / 276, ADC 203 / 54, estimator 61 / 8, supervisor 26 / 6, DShot 50 / 18. Snapshot mean 75 / 7 µs. Worst tick 1019 to 1174 µs (median 1076) against 904 to 938 (median 905) | Find the stage that grew | Observation: the magnetometer, ADC, estimator, supervisor, DShot and snapshot stages all grew, the IMU read did not. That points at instruction-cache misses rather than one slow driver (the session's reading; inferred) |
| 3 | IRAM config experiment: the three IRAM settings only, 198 s (21:05) | Worst tick 914 to 1125 µs, median 957; over 1000 µs in 9 of 197 seconds. 29 flash-resident functions still reachable from the tick ([list](2026-10-10-s3-control-tick-wifi/flashcalls-iram-config-build.txt)) | Under 1000 µs | Fail; partial improvement |
| 4 | The fix (linker fragments plus the settings), 10 min (21:14) | Worst tick 753 to 756 µs, median 754, 0 seconds over 1000. 0 missed ticks; 10 s windows 5000 to 5001; p99 deviation at most 9 µs; DShot 500.0/s, 0 dropped; 0 invalid IMU reads; barometer OK 628 of 628. Flash-resident callees left: 16, all set-up, error or parameter-update paths by my reading of the [list](2026-10-10-s3-control-tick-wifi/flashcalls-fix-build.txt) (gptimer and Hall set-up, `esp_err_to_name`, `snprintf`, `floor` in parameter validation) | Under 1000 µs | **Pass with the radio up and no client** |
| 5 | The fix after the core-1 experiment was reverted, 3 min (21:31) | Worst tick 753 to 759 µs; 0 missed; p99 at most 5 µs | Same as step 4 | Pass |
| 6 | **Soak**: the fix, 45 min (21:37:37 to 22:22) | 2698 status lines. Worst tick 752 to 758 µs, median 753, 0 seconds over 1000. 0 missed ticks after the first second; 10 s windows 5000 to 5001; p99 deviation at most 6 µs (max deviation 7 µs); DShot 500.0/s, 0 dropped; 0 invalid IMU reads; barometer OK in 2698 of 2698; no reset after the first boot | Under 1000 µs, no missed ticks | **Pass with the radio up and no client** |
| 7 | R4.3 with the fix (0a86770), SoftAP up, 3 min (22:31) | Worst tick 755 to 760 µs; 0 missed; p99 at most 6 µs; DShot 500.0/s, 0 dropped | Same as step 4 | Pass |
| 8 | Wi-Fi **connected with a client streaming** | Not in this session. Later captures give tick timing in station mode with no client (754 to 761 µs over 886 s, 02:37 to 02:51 on 2026-10-11, [capture](2026-10-11-esc-dshot150-spins/2-esc1-session-0237.txt.gz)) and with the bench console connected: see [2026-10-11-s3-first-spin-dry-run.md](2026-10-11-s3-first-spin-dry-run.md), where 2 of 630 seconds exceeded 1000 µs | Under 1000 µs | Not run here |
| 9 | The first second after boot, every SoftAP image tonight (25 boots: rows 1 to 7 plus 18 boot-only captures) | 16 of 25 boots show one tick of 3992 to 4057 µs with 1 or 2 missed ticks and 2 invalid IMU reads. By image: before the fix 3 of 3; the fix, alone or merged into R4.3, 3 of 4; Wi-Fi NVS off 3 of 8; NVS and PHY calibration storage off 1 of 2; bus and DShot created on core 1 6 of 8. Without the spike the first second still read over 1000 µs in 7 of the other 9 boots (1302 to 1580 µs); the remaining two read 968 and 791 µs | No missed ticks | **Fail, open.** None of the three experiments removed it. The session's likely cause, a flash access during Wi-Fi start-up that stalls the other core, is not shown. All of it happens in SAFE, before any client can connect |
| 10 | DShot output through every run | 500.0 frames/s and 0 dropped in every capture, by the firmware's transmit counter | 500/s, 0 dropped | Pass for the counter only. No scope on D8 |

## Outcome

- Overall: **Pass for the worst tick with the Wi-Fi radio up and no client** (the fix holds 752 to 760 µs over 45 min, also merged into R4.3). R3.4's timing item still needs Wi-Fi connected with a client streaming; a later measurement with the console connected found 2 seconds over 1000 µs (see step 8). **Fail, open: the first second after boot**, with a 4 ms tick and missed ticks in 16 of 25 boots.
- Faults, unexpected motion, heat, noise, or supply current limiting: none. No reset or panic in any capture after its boot. One first-second invalid-IMU pair per spiked boot; one invalid IMU read later in step 1.
- Raw data and photos (times are the capture start; every file has the SoftAP's network name scrubbed to `<softap-ssid>`):
  - [1, before the fix, 10 min](2026-10-10-s3-control-tick-wifi/1-before-fix-10min-2049.txt.gz)
  - [2, stage timing, 150 s](2026-10-10-s3-control-tick-wifi/2-stage-timing-150s-2100.txt)
  - [3, IRAM config experiment](2026-10-10-s3-control-tick-wifi/3-iram-config-experiment-2105.txt)
  - [4, the fix, 10 min](2026-10-10-s3-control-tick-wifi/4-after-fix-10min-2114.txt.gz)
  - [5, the 18 boot-only captures, each under its own header](2026-10-10-s3-control-tick-wifi/5-boot-second-experiments-2125.txt.gz)
  - [6, after the revert, 3 min](2026-10-10-s3-control-tick-wifi/6-after-revert-3min-2131.txt)
  - [7, the 45 min soak](2026-10-10-s3-control-tick-wifi/7-soak-45min-2137.txt.gz)
  - [8, R4.3 with the fix, 3 min](2026-10-10-s3-control-tick-wifi/8-r43-merge-3min-2231.txt)
  - [`flashcalls.py`](2026-10-10-s3-control-tick-wifi/flashcalls.py) and its two outputs; [`status-summary.py`](2026-10-10-s3-control-tick-wifi/status-summary.py).
- Follow-up owner and action:
  - Julian: decide whether R3.4's "no missed ticks" and the worst-tick item cover the first second after boot (the session raised this in the R3.5 log).
  - Julian's agent: measure the worst tick for 10 min with a client streaming telemetry, and find the boot-second stall.
