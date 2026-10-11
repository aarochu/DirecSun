# Test: battery divider and battery ADC against the multimeter (R3.3, FD V-67)

- Date/time and timezone: 2026-10-11, 00:48 to 04:08 (America/New_York), in five stretches: the tap calibration (00:48), the first board (00:55 to 00:59), the floating-ground fault (01:59 to 02:16), the rewired rig (02:36 to 02:54), and the final check (04:05 to 04:07).
- Operators and witnesses: Julian (wiring, bench supply, multimeter readings); Julian's Claude Code agent (firmware calibration, build, flash, serial reads). Written up afterwards from the session transcript, the raw captures and the kernel's USB log by a separate Claude Code agent.
- Julian's explicit go for any motor-turning step: Not applicable; no step commanded the motor. The ESC was powered during some readings (00:55 to 01:00, 02:38 to 02:54, 04:05 to 04:09) with the box sending DShot 0 and never armed. The motor nevertheless turned on its own in the first two windows; that is recorded in [2026-10-11-esc-dshot150-spins.md](2026-10-11-esc-dshot150-spins.md).
- Component model, markings, and serial/lot:
  - Divider on the breadboard: top resistor from the supply side, bottom resistor to ground, tap to D9 (GPIO8, ADC1 channel 7). The fitted values were not read. Julian first proposed 83.2 kΩ and 21.23 kΩ (ratio 0.2033), then fitted "slightly different" resistors. Per FD V-67 the 83.2 kΩ / 21.23 kΩ pair was never fitted. Julian confirmed at 02:52 that the rewire kept the same resistors.
  - Two XIAO ESP32-S3 boards, told apart by the kernel's USB log and the flash logs:
    - e0:72:a1:fc:49:60 was on USB from 00:49 to 01:13.
    - e0:72:a1:fc:49:5c from 01:13 on.
  - The session did not notice the swap: it treated the 01:59 to 03:49 readings as coming from the board it had flashed with R3.7 at 00:50.
- Firmware branch/commit and ESP32 target (ESP-IDF v6.0, esp32s3). **The board on USB from 01:13 to 03:55 was not running the calibrated firmware.**
  - 00:50, board 49:60: `feat/set3/R3.7-manual-throttle`, built from the tree committed as 912aed8 (V-67: ratio = 1.761 / 10.01 = 0.1759). The boot log's version string reads 65f9c11. The build log shows `adc_inputs.cpp`, `app_main.cpp` and `control_task.cpp` recompiled, but the build directory had been configured at 65f9c11, so that is the version it prints.
  - 01:13 to 03:55, board 49:5c: `feat/set3/R3.5-comms` at 3799644, flashed at 23:08 the evening before. Its boot logs at 02:04, 02:13, 02:36 and 03:48 say so (compiled 23:07:08). No flash log between 23:08 and 03:55 names this board. That commit divides by the design ratio 22/122 = 0.1803.
  - 04:03 on, board 49:5c: R3.7 built from the tree committed as b9fe88d (V-67 and V-68). Its boot log shows DShot300, the V-68 change, while its version string still reads 65f9c11.
- Procedure and acceptance-criterion reference:
  - `changes/3-teleop/R3.3-hall-and-adc.md`, Tests: "Battery ADC against the multimeter, two independent instruments. With the bench supply feeding the divider at 10.0 V, vbat reads within 2% of the meter. At 9.0 V it follows within 2%. A broken channel would sit still while the supply moved."
  - TD §5.1, FD §8 and §23.1 row V-67. The 9.6 V arm guard and the 8.8 V BATTERY_LOW trip use this reading (FD §14.1).
  - The session marked the R3.3 item `[x]` on `feat/set3-teleop` in 46325a9.
- Rig setup, wiring revision, guard, and stop method:
  - Two wiring states. Before 02:16 the divider's bottom and the supply's negative reached the XIAO's ground through an uncertain path; the session inferred it ran only through the ESC signal lead's ground, but nobody measured it.
  - From 02:16 to 02:36 Julian rewired everything to one common ground rail, following the agent's wiring table.
  - Stop method: the bench supply's output switch.
- Power source, set voltage, current limit, and measured voltage: a bench supply, set to 10 V or 9 V; its current limit was not reported. Julian's multimeter read:
  - 10.01 V at 00:48;
  - 9.98 V "at the supply rails" at 02:40;
  - 10.01 V at 04:06;
  - 9.00 V at 04:07.
- Instruments, calibration or positive-control check:
  - Julian's multimeter (model not recorded), and the box's `vbat` in its 1 Hz status line, read without resetting the board (`idf.py monitor --no-reset`) except where a row says "boot".
  - Positive controls:
    - jumpering D9 to a XIAO GND pin moved the reading from 17.53 V to about 1.1 V, so the firmware reads the pin the tap is on;
    - switching the supply off and on moved the reading each time.

## Steps and observations

The 2% limit is applied to the box's reading against the meter. "Rescaled" readings multiply a 3799644 reading by 0.1803 / 0.1759 = 1.0250, giving what the calibrated firmware would have reported for the same pin voltage. They are arithmetic, not a measurement of that firmware.

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Meter at the tap, supply at 10.01 V (00:48) | 1.761 V, a ratio of 0.1759 (design 0.1803) | About 1.8 V for the design values | Recorded; it became the firmware's calibration (912aed8, V-67) |
| 2 | Board 49:60, supply on at 10 V, ESC armed on its tones (00:55:35 to 00:56:00) | 2.70 to 2.71 V in 22 lines, the same as with the supply off at 00:52 (2.72 V) | Follows the supply | Not a valid run: Julian had not yet connected the tap to D9, so the pin floated |
| 3 | Board 49:60, tap on D9, supply on at 10 V (00:58:51 to 00:59:06) | 9.89 to 9.90 V in 11 lines; -1.2% to -1.1% against the 10.01 V metered at 00:48 | Within 2% (9.81 to 10.21 V) | Pass, with two caveats: the supply was not re-metered at that moment, and 10.01 V is the calibration point, so this is not independent |
| 4 | Meter at the tap, supply at 9.0 V (before 01:59) | 1.586 V, a ratio of 0.1762, within 0.2% of the 0.1759 at 10.01 V | Linear divider | Pass for linearity. The supply was not metered; the ratio assumes exactly 9.00 V |
| 5 | Board 49:5c (3799644), supply **off**, ESC signal lead unplugged (01:59:50 to 02:04) | 17.53 V in every line (12 at 01:59, 10 at 02:03, and from the first 43 readings after a reset at 02:04). That is 3.16 V at the pin with this build's ratio, the ADC's ceiling. Julian's meter: 0 V on D9 at 02:03; 0.756 V from the XIAO's own GND to D9 at 02:10 | About 0 V | **Fail: D9 floating.** The divider's bottom was not on the XIAO's ground |
| 6 | D9 jumpered to a XIAO GND pin, supply off (02:15) | 1.05 to 1.11 V in 10 lines, about 0.19 to 0.20 V at the pin | Near 0 V | Pass as a positive control. The session called the residue the ADC's offset near 0 V, but step 7 read 0.00 V on the same pin, so that explanation does not fit. The cause is not established |
| 7 | After the rewire, supply off, board reset (02:36) | 0.00 to 0.01 V in 20 lines; 0.00 V over the 43 boot-window readings | About 0 V | Pass |
| 8 | Supply on at 10 V, meter 9.98 V at the supply rails, ESC #1 powered (02:38:22 to 02:41:48). Board 49:5c, **3799644 (design ratio)** | 9.70 to 9.74 V, median 9.72, in 203 lines (the 4 lines at the switching edges excluded): -2.8% to -2.4%, median -2.6%. Rescaled: 9.94 to 9.98 V, -0.4% to 0.0% | Within 2% | **Fail as run**, because the board was running the uncalibrated build. The session saw -2.6% and blamed a ground offset from the ESC's current; the rescaled figures show the ratio alone accounts for it (inferred, but from the boot log and the code) |
| 9 | Supply on again, no meter reading, 02:45:19 to 02:50:14 and 02:54:10 to 02:54:36 | 9.72 to 9.75 V, median 9.74 (rescaled 9.96 to 9.99 V) | Steady | Observation: steady while the ESC beeped and the motor twitched |
| 10 | 9.0 V point on the rewired rig before 04:00 | Not set: the session turned to the ESC's unexplained spins | Within 2% at 9.0 V | Not run |
| 11 | Board 49:5c, R3.7 with V-67 (04:03 build), supply on at 10 V, meter 10.01 V, ESC #2 powered on DShot300 (04:05:22 to 04:05:34) | 10.01 to 10.03 V in 11 lines (9 at 10.02): 0.0% to +0.2% | Within 2% | Pass, not independent: this is the calibration point |
| 12 | Supply off while the setting was changed (04:06:52 to 04:07:01) | 0.20 falling to 0.05 V | Follows the supply | Pass: the reading moved with the supply |
| 13 | Supply on at 9.0 V, meter 9.00 V (04:07:29 to 04:07:41) | 8.99 to 9.00 V in 11 lines: -0.1% to 0.0% | Within 2% (8.82 to 9.18 V) | **Pass: the independent point** |

## Outcome

- Overall: **Pass at 04:05 to 04:07** for R3.3's battery item, on the calibrated R3.7 firmware: within 0.2% at 10.01 V (the calibration point) and within 0.1% at 9.00 V (independent). The earlier failures were wiring (a floating D9, steps 2 and 5) and the wrong firmware on the board (step 8).
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - The motor turned on its own while the ESC was powered in steps 2, 3 and 8; see the ESC log.
  - **Correction to the session's record:** the R3.3 log entry in 46325a9 says the -2.6% "likely cause is a ground offset from the ESC current". By the boot log, the board was running 3799644, whose design ratio reads 2.5% low against this divider. Rescaled, those readings are within 0.4% of the meter.
- Raw data and photos (every capture taken without a reset unless noted; terminal colour codes and carriage returns stripped, the network name scrubbed):
  - [step 2](2026-10-11-battery-divider/1-d9-not-connected-0055.txt)
  - [step 3](2026-10-11-battery-divider/2-tap-on-d9-10v-0058.txt)
  - step 5: [01:59](2026-10-11-battery-divider/3-reads-17v53-supply-off-0159.txt), [02:03](2026-10-11-battery-divider/4-reads-17v53-meter-0v-0203.txt), [02:04 boot](2026-10-11-s3-sensor-boot/2-imu-readback-fail-0204.txt)
  - [step 6](2026-10-11-battery-divider/5-d9-jumpered-to-gnd-0215.txt)
  - [step 7, boot](2026-10-11-s3-sensor-boot/4-after-rewire-0236.txt)
  - steps 8 and 9: [the 02:37 to 02:57 session](2026-10-11-esc-dshot150-spins/2-esc1-session-0237.txt.gz)
  - [step 11](2026-10-11-battery-divider/6-v67-build-10v-0405.txt), [step 12](2026-10-11-battery-divider/7-supply-off-during-change-0406.txt), [step 13](2026-10-11-battery-divider/8-v67-build-9v-0407.txt)
  - Steps 1 and 4 are Julian's meter readings, reported in the session.
- Follow-up owner and action:
  - Julian: correct the "ground offset" note in R3.3's log (46325a9), or confirm it another way.
  - Julian: re-measure the tap if the resistors ever change. V-67's calibration belongs to these resistors.
