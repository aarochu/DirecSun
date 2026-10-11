# Test: ESC on a DShot 0 stream, the unexplained motor spins, and DShot300 (R3.1, FD V-68)

- Date/time and timezone: 2026-10-11, 00:55 to 04:09 (America/New_York).
- Operators and witnesses: Julian (wiring, the bench supply's output switch, listening and counting spins, the oscilloscope); Julian's Claude Code agent (firmware, flash, serial capture, analysis). Written up afterwards from the session transcript, the raw captures, Julian's scope photo and the kernel's USB log by a separate Claude Code agent.
- Julian's explicit go for any motor-turning step: **No motor-turning step was commanded in this log. The box was never armed:**
  - every status line in every capture from 00:50 to 04:10 reads mode SAFE, throttle 0;
  - no WebSocket client connected to the box in any of them, and the laptop was not on the bench network until 04:15;
  - for the stretches with no capture, the same is inferred: 00:59 to 01:05 and 02:57 to 03:39.
  - Julian switched the bench supply on and off himself at each step; powering the ESC on DShot 0 is R3.1's "ESC accepts the stream" step.
  - **The motor turned anyway**, on its own, while the box sent zeros. This is safety-relevant: an e-stop is also a stream of zeros.
- Component model, markings, and serial/lot:
  - ESC #1 and ESC #2: LANRC 35A BLHeli_S (as named in the V-68 commit), each soldered to its own 2212 motor, so ESC #2 came with motor #2. Neither ESC's MCU type nor its BLHeli_S version was read.
  - Signal from XIAO D8 (GPIO7), its ground to the XIAO's GND. A series resistor of unrecorded value was added on the signal line between 02:57 and 03:04.
  - The motor was mounted on the motor deck with no flywheel (Julian, 01:35).
  - Boards, from the kernel's USB log: XIAO ESP32-S3 e0:72:a1:fc:49:60 from 00:49 to 01:13, then e0:72:a1:fc:49:5c. The session did not notice this swap.
- Firmware branch/commit and ESP32 target (ESP-IDF v6.0, esp32s3, all builds 0 warnings):
  - 00:50 to 01:13, board 49:60: `feat/set3/R3.7-manual-throttle` built from the 912aed8 tree (version string 65f9c11). DShot150.
  - 01:13 to 03:55, board 49:5c: `feat/set3/R3.5-comms` at 3799644, by its boot logs. DShot150.
  - 03:58: a scratch image, 912aed8 with `app_main.cpp` forcing DShot300, never committed. The session restored the source afterwards; its boot log reads `912aed8-dirty` and DShot300.
  - 04:03: R3.7 built from the tree committed as b9fe88d (V-68: `dshot_rate` defaults to 300). The boot log shows DShot300 under the stale version string 65f9c11.
- Procedure and acceptance-criterion reference:
  - `changes/3-teleop/R3.1-dshot-output.md`, Tests: "ESC accepts the stream ... DShot 0 only, so the motor cannot turn. The ESC plays its arming tones (low then high, TD §6.3)", and the D8 oscilloscope item.
  - `tools/bench/first-spin/RUNBOOK.md` (R3.7 branch), §7, and §11's stop condition "The motor turns without an `arm`, or turns at ESC power-up beyond a beep twitch".
  - TD §5.2 and §6.3. FD §16 and §23.1 rows V-21 and V-68.
- Rig setup, wiring revision, guard, and stop method:
  - Bench supply into the ESC through a 470 µF capacitor. A fuse was planned but not confirmed in the session.
  - The wiring was rebuilt on one common ground rail between 02:16 and 02:36. From 02:43, the signal wiring was on the breadboard and the power wiring came straight from the supply (Julian).
  - Stop method: the supply's output switch.
- Power source, set voltage, current limit, and measured voltage: bench supply at 10 V, with 9.98 V metered at the rails at 02:40. The current limit was not reported; the session assumed 2 A.
- Instruments, calibration or positive-control check:
  - Julian's ears and eyes: tones, spins counted by hand.
  - A Hantek DSO2D15 oscilloscope. Its calibration-terminal check was requested, not reported.
  - The box's status lines, captured with wall-clock timestamps and without resetting the board (`idf.py monitor --no-reset --timestamps`). [`segment.py`](2026-10-11-esc-dshot150-spins/segment.py) splits each capture into supply-on and supply-off windows (`vbat` above or below 5 V), mode, throttle and latches.
  - The kernel's USB log, for board identity and power cycles.
  - Positive controls: the status line's throttle field moved during the 04:26 dry run ([2026-10-11-s3-first-spin-dry-run.md](2026-10-11-s3-first-spin-dry-run.md)), so "throttle 0" is a live reading; and the capture's `vbat` follows the supply on and off.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | ESC #1, board 49:60 on DShot150 zeros: supply on at 10 V (00:55:05) | Julian heard the arming tones. Box 00:55:35 to 00:56:00: SAFE, throttle 0 in 22 lines; IMU missing, so it could not arm | Arming tones, no motion | Pass on R3.1's stated criterion, the tones. See step 3 |
| 2 | The agent's raw serial read at 00:55:11 | It reset board 49:60 (`rst:0x15`) with the ESC powered; the session did not mention it. The firmware's log puts the first zero frame 40 ms after its clock starts, so the ESC saw a signal gap of at least the reset and boot time | No reset with the ESC powered | Observation. Not shown to cause anything |
| 3 | ESC #1, board 49:60, supply on, about 01:00 | Julian, 01:00:20: "i think your test has the motor spinning on and off rn"; 01:04:29: "the bell kept turning for a moment after it was beeping for a while". Supply switched off at the agent's request. Nearest box record, 00:58:51 to 00:59:06: SAFE, throttle 0, 500 frames/s, 0 dropped, 0 missed. Nothing was recording at the moment of the motion | No motion on DShot 0 | **Fail: unexpected motion** (runbook §11 stop condition) |
| 4 | ESC #1, board 49:5c on DShot150 zeros, supply on at 10 V (02:38:22 to 02:41:48) | Julian, 02:40:46: "heard the arming tones at slightly uneven intervals and every once in a while itll slowlly spin for half a secon one or a few time before going back to making the noise". Box: SAFE, throttle 0 on every line, 500 ticks/s, 0 dropped, 0 missed | No motion | **Fail: unexpected motion, repeated.** The session read the uneven tones as the ESC repeatedly losing the signal and re-arming (inferred) |
| 5 | Scope, first attempt (02:52) | Julian: "I dont see any rising edge or anything on the oscilloscope" | Frames every 2.0 ms | Inconclusive: the scope set-up was not checked against a known signal |
| 6 | Scope photo of "the esc signal" (sent 02:55:45; probe point inferred to be the ESC end; supply state when taken not recorded, the box saw it on from 02:54:10 to 02:54:36). 1 V/div, 5 µs/div, trigger 1.56 V | My reading of the photo, which agrees with the session's: pulses about 3.2 V high and about 2.5 µs wide, about 7 µs apart, consistent with DShot150 zero bits (6.67 µs bit, 2.5 µs high). A spike at every edge, about 1.5 V above the high level and about 1.5 V below ground. On the idle line before the frame, two small spikes of a few tenths of a volt; no full pulse in this frame. Julian: "I noticed these fast twitches of square signals on the left half every now and again". Read from a photo, not with the scope's measurements | Clean 0 to 3.3 V frames, nothing between frames | **Fail: ringing at every edge, and stray pulses on the idle line by Julian's report.** Their source was not found |
| 7 | Box record across the scope work, 02:37:06 to 02:57:04 | SAFE, throttle 0 on all 1196 mode lines; 0 dropped. Supply on 02:38:22 to 02:41:48, 02:45:19 to 02:50:14, 02:54:10 to 02:54:36. **The board power-cycled once**: USB dropped out at 02:51:52 (kernel log, monitor error), and it rebooted with the supply off. The session reported "no resets" because its counter looked for the ROM's reset line, lost in the reconnect. Its "2 missed ticks" are that boot's first second. After the reboot the IMU was silent ([sensor log](2026-10-11-s3-sensor-boot.md)) | No reset | Observation; the session's summary was wrong on resets |
| 8 | Series resistor in the signal line (02:57 to 03:04) | Julian, 03:04:08: "The series resistor did cause the ringing to go down a bit but im still getting the occasional pulse and the motor still randomly spins". No box capture from 02:57:04 to 03:39:18; the board power-cycled again at 03:02:36 (USB) | No motion | **Fail** |
| 9 | ESC #2 with motor #2, board 49:5c on DShot150 zeros (fitted between 03:09 and 03:38; the board power-cycled at 03:37:53). Supply on, 03:39:20 to 03:42:22 by the box | Julian, 03:38:51: "it made the arming jingle and is now spinning at consistant intervals"; 03:44:31: "it gets about 6 spins in every 5 seconds". Box: SAFE, throttle 0 on every line, 0 dropped, 0 missed; IMU silent the whole time, so it could not arm | No motion | **Fail: the same on a second ESC and motor** |
| 10 | No-signal test: ESC #2's signal wire off the XIAO and its signal pin tied to its signal ground, supply on (inferred to be the box's second on-window, from 03:43:52) | Julian, 03:44:31: "It doesnt spin after step 3", read as no spins with the signal pin grounded | No motion | Pass. The spins need the signal from the box |
| 11 | BMP280 unplugged and the board reset (03:48:53), removing the once-a-second barometer read the session suspected; supply on 03:51:35 to 03:52:17 | Julian, 03:52:26: "It still spins I counted 23 spins in 20 seconds", about 1.15 per second. Box: SAFE, throttle 0, 0 dropped, 0 missed, barometer absent | No motion | **Fail.** The barometer read is not the cause |
| 12 | DShot300 zeros, scratch image (boot log: `GPIO7 DShot300: bit 133 ticks`, zeros from 45 ms). Two earlier scratch images still booted at DShot150, because the source was restored before `idf.py flash` rebuilt them (inferred from the command order). Supply on 03:59:28 to 04:00:04 | Julian, 04:00:30: "it played the jingle then no spins at all for about 30 second". Box: SAFE, throttle 0, 0 dropped, 0 missed | No motion | **Pass, once, on ESC #2, for about 30 s** |
| 13 | V-68 committed (b9fe88d, 04:02:56) and flashed (04:03:40) | Host tests 109 of 109 and the selftest passed, flight build 0 warnings (session output). Boot log: DShot300 | Builds and boots at DShot300 | Pass. On the R3.7 branch only, not on `main`; the session's message asking the PR-review session to carry it up expired undelivered, and the request was posted on issue #80 instead |
| 14 | ESC #2 powered on the V-68 build for the battery checks (04:05:15 to about 04:09:40) | No spin reported. Julian was not asked, and nothing was recorded about motion | No motion | Inconclusive |
| 15 | ESC #1 on DShot300 | Not run: ESC #1 was removed at 03:09 | | Not run |

## Outcome

- Overall: **Fail on DShot150, pass once on DShot300.** Both ESCs turned the motor in short bursts while the box logged SAFE, throttle 0 and no dropped frames, on two boards and two firmware builds. With the signal pin grounded, ESC #2 stayed still; on DShot300 zeros it stayed still for about 30 s.
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - **Unexpected motion, repeated (steps 3, 4, 8, 9, 11).** No heat, smell or current limiting was reported.
  - **Status of the finding: numbered but not explained.** The session assigned FD **V-68** at 04:02 (b9fe88d: "the bench ESCs misread DShot150") and made DShot300 the default.
  - What is shown: the spins need the box's signal, they occur on DShot150 zeros, and one ESC stayed still on DShot300 zeros for about 30 s.
  - What is not shown:
    - why these ESCs misread a zero frame (their MCU type and BLHeli_S version are unread);
    - where the ringing and the stray idle-line pulses come from, and whether they matter at DShot300, whose pulses are half as wide;
    - how ESC #1 behaves on DShot300;
    - whether DShot300 stays clean over a long run and under motor current.
  - Until those are shown, treat a stray spin as possible at any time the ESC is powered, including after an e-stop.
- Raw data and photos (terminal colour codes stripped; network name and BSSID scrubbed):
  - [step 2: the raw read that reset the board](2026-10-11-esc-dshot150-spins/1-raw-read-reset-esc-powered-0055.txt)
  - step 1: [00:55](2026-10-11-battery-divider/1-d9-not-connected-0055.txt); step 3: [00:58](2026-10-11-battery-divider/2-tap-on-d9-10v-0058.txt)
  - [steps 4 to 7: ESC #1, 02:37 to 02:57](2026-10-11-esc-dshot150-spins/2-esc1-session-0237.txt.gz)
  - [step 6: scope photo](2026-10-11-esc-dshot150-spins/7-scope-esc-signal-0255.jpg), cropped from Julian's photo to the instrument
  - [steps 9 and 10: ESC #2, 03:39 to 03:49](2026-10-11-esc-dshot150-spins/3-esc2-session-0339.txt.gz)
  - [step 11: no barometer, 03:49 to 03:55](2026-10-11-esc-dshot150-spins/4-esc2-no-barometer-0349.txt); its boot: [03:48](2026-10-11-s3-sensor-boot/5-bmp280-unplugged-0348.txt)
  - step 12: [DShot300, 03:58 to 04:03](2026-10-11-esc-dshot150-spins/5-esc2-dshot300-0358.txt); its boot: [03:58](2026-10-11-s3-sensor-boot/6-bmp280-unplugged-r37-0358.txt)
  - [step 13: the V-68 build's boot](2026-10-11-esc-dshot150-spins/6-v68-build-boot-0403.txt)
- Follow-up owner and action:
  - Julian: read the ESCs' MCU type and BLHeli_S version through a passthrough (V-21, V-68).
  - Julian: scope the ESC input with two channels (D8 and the ESC end) at DShot300, ESC powered, to see whether the stray pulses remain.
  - Julian: decide whether 30 s on one ESC is enough evidence to spin.
  - Julian's agent: get b9fe88d onto R4.3 and R4.1 (issue #80).
