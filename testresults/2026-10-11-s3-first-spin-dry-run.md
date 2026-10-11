# Test: first-spin dry run with the ESC unpowered, over the bench network (R3.7 rung 1, runbook §6)

- Date/time and timezone: 2026-10-11, 04:15 to 04:32 (America/New_York). The status capture ran from 04:18:43; this record stops at 04:32:00. After that, following the agent's next instructions, the supply went off at about 04:33:00 and on again at 04:33:12 (inferred: to reconnect the ESC's power), and the box was armed at 04:34:14 for the first powered spin. **That spin is not recorded here**; it was still in progress when this log was written.
- Operators and witnesses: Julian (bench console in Chrome, supply, ESC power lead, every arm); Julian's Claude Code agent (laptop network switch, serving the console, status capture, checks). Written up afterwards from the session transcript and the raw capture by a separate Claude Code agent.
- Julian's explicit go for any motor-turning step: **Nothing could turn.** The ESC's power lead was disconnected for the whole record (Julian, 04:26:13: "esc power disconnected, psu on, bench setup done"). The divider stayed fed from the supply so that arming was allowed.
  - Before each arm the agent stated what it would send and what the box should do, and asked for Julian's go.
  - Julian then pressed Arm and Confirm arm himself: about 04:26:54 and 04:30:09.
  - The 0 V check on the ESC side of the disconnect, which the agent asked for, was not reported.
- Component model, markings, and serial/lot: XIAO ESP32-S3 e0:72:a1:fc:49:5c (flash log at 04:03, no flash after), with the MPU-6050, QMC5883P and BMP280 (all answered at the 04:10 boot) and the A1104 input. ESC #2 and its motor were on the rig with the ESC's power lead open.
- Firmware branch/commit and ESP32 target:
  - Box: `feat/set3/R3.7-manual-throttle` built from the tree committed as b9fe88d (V-67 battery calibration, V-68 DShot300). Flashed 04:03, booted 04:10; the boot log shows DShot300 and the stale version string 65f9c11. ESP-IDF v6.0, esp32s3.
  - Console: `ground/console/` from the same checkout, served on the laptop by `python3 -m http.server`.
  - Julian noted at 04:20 that this is the bench console, not Aaron's operator UI. The runbook calls for the bench console: Aaron's UI has no manual throttle or bench mode.
- Procedure and acceptance-criterion reference:
  - `tools/bench/first-spin/RUNBOOK.md` (R3.7 branch) §6, "Dry run with the ESC unplugged".
  - `changes/3-teleop/R3.7-manual-throttle.md`, Tests, "Hardware, rung 1": arm, SPINUP, READY, MANUAL; the logged maximum throttle never exceeds the clamp; Space starts a 5 s ramp and then DShot 0; closing the tab starts the ramp within 1.5 s.
  - FD §13.3, §13.5, §14.3 (ramp-down) and §14.4 (heartbeat, 1 s). TD §9.2 T-13.
  - Also recorded here: R3.5's "Wi-Fi and mDNS" item, and R3.4's timing item with Wi-Fi connected.
- Rig setup, wiring revision, guard, and stop method: the rewired breadboard rig (common ground rail), Hall bypassed by the console's Bench setup (no flywheel or magnets yet). Stops: Space in the console, then the supply switch.
- Power source, set voltage, current limit, and measured voltage: the XIAO on laptop USB. The bench supply was on at 10 V from 04:25:20, feeding only the divider; the box read 10.02 to 10.04 V. No meter reading was reported in this window.
- Instruments, calibration or positive-control check:
  - The box's 1 Hz status lines and log lines, captured with wall-clock timestamps without resetting the board.
  - [`phase-timing.py`](2026-10-11-s3-first-spin-dry-run/phase-timing.py) gives the per-phase tick figures.
  - Positive control: the mode, throttle and latch fields change exactly when the console acts, so they are live. At 1 Hz the status lines cannot resolve the runbook's 0.24 s, 1.5 s and 2.0 s spin-up marks; the ramp slopes below are the check instead.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Laptop joins the bench network (04:15) | Laptop 192.168.8.203. `ping -c 2 192.168.8.224` answered; `getent ahostsv4 direcsun.local` returned 192.168.8.224 | R3.5: `ping direcsun.local` resolves | Pass by an equivalent check: the name resolved by mDNS and the address answered. `ping direcsun.local` itself was not run |
| 2 | Bench console connects (04:21:30) | Box log: `ws: client 1 connected (fd 57)`. Which URL was typed was not recorded | R3.5: the console connects at `ws://direcsun.local/ws` | Pass for the connection; the URL is unconfirmed. The main UI's Device link was not tried |
| 3 | E-stop rehearsal, supply off (from 04:22:07) | Latch 0x0011 (ESTOP plus BATTERY_LOW) from 04:22:07. That Julian pressed Space is inferred from the agent's instructions and the latch; the ack was not captured | ESTOP latched | Pass |
| 4 | Bench setup in the console (04:25:46) | Three `persist: parameters saved to NVS (version 1)`; flag 0x08, HALL_BYPASS (`kFlagHallBypass = 1u << 3`), set from 04:25:47 | Three acks and HALL_BYPASS | Pass |
| 5 | **Arm 1** (about 04:26:54) | Latches cleared to 0x0000. SPINUP at throttle 76 (04:26:55.71), then 276 one second later, a slope of 198 per second against the runbook's 200 per second (48 to 300 over 1.26 s). READY at 300 by 04:26:57.74 | SPINUP, ramp, READY | Pass |
| 6 | **Unplanned: HEARTBEAT_LOST** in READY (04:27:08.63, after 11 s in READY) | Mode SAFE, latch 0x0002, throttle 285, 224, 164, 103, then 0 at 04:27:12.69. That fits 300 × (1 − t/5 s) from a trip at about 04:27:08.38, sent as 0 once below 48 at 4.2 s. The WebSocket stayed open: no `ws: client 1 disconnected` line | No heartbeat loss while the console runs; if lost, the 5 s ramp | **Pass for the stop path; fail, open, for the heartbeat.** The cause is not shown. The session's hypothesis: the console tab was not visible and Chrome slowed its 1 s ping timer |
| 7 | **Arm 2** (about 04:30:09) | SPINUP 92 (04:30:09.82), then 296 (slope 201 per second); READY at 300 by 04:30:11.60 | SPINUP, ramp, READY | Pass |
| 8 | Enter MANUAL throttle (04:30:19.71) | Throttle stays 300 | No step | Pass |
| 9 | Nudge up | Status lines: 310, 330, 340, 350, 360, 370, 380, then 400 from 04:30:40.74 to 04:30:52.65. Never above 400. Flag 0x04 (MANUAL_STALE) on between presses, as the console sends only on a press | Rises by 10 per press to 400 and holds there | Pass for "never above the 400 clamp" at 1 Hz sampling. That an 11th press is refused is not shown: presses are not logged. The Down presses to 150 (runbook step 3) were not done |
| 10 | Space (e-stop) from MANUAL 400 (about 04:30:53.5) | SAFE, latch 0x0001 (ESTOP); throttle 385, 305, 224, 142, 62, then 0 at 04:30:58.73. That fits 400 × (1 − t/5 s) from about 04:30:53.48 | 5 s ramp, 0 by 4.4 s | Pass |
| 11 | DShot 0 on the wire after each ramp | Not measured: no scope on D8 or the ESC input | DShot 0 | Not run |
| 12 | Tab-close test (runbook §6 step 5; R3.7 "closing the tab starts the ramp within 1.5 s") | Not done. Step 6's heartbeat loss is not a substitute: when the last ping arrived is unknown | Ramp within 1.5 s | Not run |
| 13 | Worst tick with Wi-Fi in station mode, **console connected and streaming** (04:21:30 to 04:32:00, 630 s) | 761 to 1322 µs, median 764. **Over 1000 µs in 2 seconds**: 1080 µs at 04:25:58 and 1322 µs at 04:28:26, both in SAFE with nothing else logged near them. Armed windows: at most 834 and 874 µs. 0 missed ticks, 500 to 501 ticks per second, 0 dropped frames, 0 invalid IMU reads, barometer OK. Before the client connected (04:18:46 to 04:21:29): 761 to 766 µs | R3.4: under 1000 µs over 10 min with Wi-Fi connected | **Fail**, 2 of 630 seconds. The session reported only the armed figure, 874 µs. The cause of the two slow seconds is not known |

## Outcome

- Overall: **Pass for the dry run's command path**: arm, spin-up, READY, MANUAL, the clamp never exceeded, and both ramp-downs. The tab-close test and the scope checks were not run. **Two open findings**: an unplanned heartbeat loss while armed (the stop path worked), and two seconds over 1000 µs with a client connected.
- Faults, unexpected motion, heat, noise, or supply current limiting: none. The ESC had no power, so nothing turned.
- Raw data and photos: [status capture, 04:18:43 to 04:32:00](2026-10-11-s3-first-spin-dry-run/dry-run-0418-0432.txt.gz), cut at 04:32:00 so that it holds nothing from the powered spin. Terminal colour codes stripped. The boot at 04:10 is in [the sensor log's folder](2026-10-11-s3-sensor-boot/7-all-sensors-0410.txt).
- Follow-up owner and action:
  - Julian's agent: find why the heartbeat lapsed. Rerun with the console tab known to be visible and log ping arrival on the box. A heartbeat that lapses mid-spin ramps the motor down, which is safe but would break a demo.
  - Julian's agent: find the two slow ticks with a client connected, then run R3.4's 10 min timing item properly.
  - Julian: the tab-close rehearsal and the D8 scope check before relying on them.
