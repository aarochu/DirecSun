# Test: control-stall watchdog on the XIAO ESP32-S3 (R3.4, BC-5)

- Date/time and timezone: 2026-10-10, four captures between 18:03 and 18:22 (America/New_York).
- Operators and witnesses: Julian (bench rig); Julian's Claude Code agent (build, flash, serial capture, analysis).
- Julian's explicit go for any motor-turning step: Not applicable. No ESC or motor was connected; the bench build sends DShot 0 only.
- Component model, markings, and serial/lot: XIAO ESP32-S3 (MAC e0:72:a1:fc:49:5c) with the three I²C sensors.
- Firmware branch/commit and ESP32 target: `feat/set3/R3.4-control-task` at 873b5e3, bench build (`sdkconfig.bench`: `CONFIG_DIRECSUN_BENCH_STALL_TEST=y`), 0 warnings. ESP-IDF v6.0, esp32s3. The stall hook is compiled only into this build. It is absent from the flight binary by `nm` and `strings` (R3.4 log).
- Procedure and acceptance-criterion reference: `changes/3-teleop/R3.4-control-task.md`, Tests, control-stall watchdog item: the hook blocks the control task for 200 ms; the board resets within 150 ms of the block starting; the reset reason is logged on the next boot; the oscilloscope shows DShot frames again within 300 ms of the reset.
- Rig setup, wiring revision, guard, and stop method: the breadboard rig. Nothing can move.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check:
  - Host arrival times of serial lines, taken by [`rawts.py`](2026-10-10-s3-stall-watchdog/rawts.py), a plain pyserial reader that stamps each line with `time.monotonic()` and decodes nothing. Capture 3 used an earlier version that read in 50 ms chunks; capture 4 used the per-byte version saved here.
  - Positive control for the reader: in capture 4, the first status line and the stall line are 8774 ms apart by the board's log clock, and they arrived 8774.3 to 8774.6 ms apart in all three cycles. So arrival time tracks board time.
  - Positive control for the watchdog: if it were missing, the hook prints "did NOT fire" when its 200 ms block returns.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | The hook blocks the control task 11.3 s after boot, on every boot (10 cycles across four captures) | Every cycle: `CONTROL_STALL: no control tick finished for 100 ms`, `esp_system_abort`, reboot. "did NOT fire" never printed | A reset before the block ends | Pass |
| 2 | Time from the stall line to `CONTROL_STALL`, capture 4 | 99.0, 103.1, 100.8 ms | About 100 ms (the threshold) | Pass |
| 3 | Time from the stall line to the ROM's `rst:0xc` line, printed just after the reset, capture 4 | 127.6, 131.4, 129.1 ms | Under 150 ms | Pass |
| 4 | Reset reason on the next boot | `reset reason: panic or abort (4)` and `previous reset: CONTROL_STALL watchdog` | Logged | Pass |
| 5 | DShot frames after the reset, from the firmware's own log | The DShot driver initialises at boot-log 275 ms and the zero sender starts right after, so the first frame is about 275 to 280 ms after the reset | Under 300 ms, **on the oscilloscope** | Not run: needs the scope. The firmware's figure is just inside the bound |

Instrument note: `idf.py monitor --timestamps` (capture 2) put the reboot 234 to 273 ms after the stall line. That figure is wrong. The monitor runs addr2line on the backtrace and holds the following lines back for about 145 ms. The raw reader on the same binary shows no such gap.

## Outcome

- Overall: **Partial.** The watchdog fires at about 100 ms, the board resets within 150 ms of the stall, and the reason is logged. The scope half is open.
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - None in the firmware.
  - The spec's reasoning, that frames return "inside the ESC's own 0.3 s signal-loss cut", does not hold as written. The ESC sees no frames from the start of the stall, so its gap is about 130 + 280 ≈ 410 ms, and its own cut acts first. The outcome is still safe: the motor stops either way, and the zeros then hold the ESC disarmed. Whether to measure the item from the stall's start, or to shorten the reboot path, is Julian's decision.
- Raw data and photos:
  - [Capture 1](2026-10-10-s3-stall-watchdog/1-monitor-1804.txt): `idf.py monitor`, 2 cycles.
  - [Capture 2](2026-10-10-s3-stall-watchdog/2-monitor-timestamps-1819.txt): `--timestamps`, 2 cycles; skewed, see the note above.
  - [Capture 3](2026-10-10-s3-stall-watchdog/3-raw-reader-50ms-1821.txt): raw reader at 50 ms granularity, 3 cycles.
  - [Capture 4](2026-10-10-s3-stall-watchdog/4-raw-reader-per-byte-1822.txt): raw reader, per byte, 3 cycles. The timings above come from this capture.
- Follow-up owner and action: Julian. Oscilloscope on D8 across the stall and reset.
