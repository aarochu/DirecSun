# Software finish plan (2026-10-11)

The supplied GitHub-state inventory, `changes/CONTEXT.md`, TD, FD, the wire schema, feature specs and actual frontend traffic control this plan. Mock and host checks are not hardware evidence. Mark a feature complete only after its literal spec gate passes.

## Critical path for the judge turn

1. Complete Set 3 in dependency order: R3.1 DShot loopback, timing, continuous frames, watchdog and ESC tones; R3.2 three sensors on one 400 kHz bus; R3.3 Hall and ADC measurements; R3.4 500 Hz task and stall watchdog; R3.5 Wi-Fi/mDNS/WebSocket, T-11 and the main UI reading the real box. Merge #77's host-test fix into R3.5 after its checks. Then complete R3.7 first spin, bypass cap, e-stop and heartbeat, R3.8 measured feed-forward table and wheel step, and R3.9 sign and gains. Each hardware claim needs bench evidence. Merge the passing features to `feat/set3-teleop`, then PR #5 to `main` after integrated CI.
2. Resolve #33's current conflict, then retarget and test #46. These cover immediate e-stop, Worker heartbeat, armed disconnect, stale telemetry, Stop before telemetry, target display and Rotate readiness. Check the UI against schema v1, typecheck, unit, browser and mock tests. Land #50 for mock refusal and timing parity and #51 for recorder and capture checking. Their mock runs cannot satisfy real-box gates.
3. Complete Set 4: R4.1's snapshot-before-outbound ordering and HT-13 already exist on its feature branch; rerun its literal gate including hidden-tab and real-box captures. R4.2 needs measured 10 s gyro drift and hand-turned ±90°. R4.3 needs ten alternating slews, settling, HOLD recovery, timeout and stop on the turntable. R4.4 DETUMBLE uses the READY zero-rate cascade and needs T-15 hand-spin trials. R4.5 collects T-2–T-4, T-7, T-8, T-15 and T-16 acceptance. Merge Set 4 only after Set 3 and these gates pass.

## Pointing and remaining firmware

4. Implement calibrated magnetometer heading and absolute HOLD: axis mapping, hard/soft-iron calibration, `mag_axis_map`, `mag_cal`, `mag_deg`, and the complementary filter, following TD §6.4 and FD §11.3. Preserve the approved relative gyro HOLD/SLEW behavior.
5. Enable MANUAL wheel and rate inputs on their existing PI/FF and rate-to-wheel paths. Apply FD §13.5 clamps and the 250 ms stale rule, prove on host and bench.
6. Implement `sun_cal`, `light_threshold`, calibrated sun angle and validity, SUN_POINT control, and return to READY after 0.5 s invalid sun. Pin READY and sun-valid refusals in firmware, schema, mock and FD together.
7. Implement guarded READY to SPINUP respin with optional speed and dwell per TD §3.6. Add `FF_TABLE_MISSING` and `RATE_GAIN_REDUCED` as additive v1 status flags, and make `PARAMS_RESET` reachable after a client speaks. Update fixtures, protocol Changes, mock and UI as needed. Never increase the 4000 rpm cap or weaken an interlock.

## Software integration

8. Resolve and retest #69 notices/diagnostics, #70 plots, #74 integration ports, #75 bench wheel-step/docs, #50 and #51. Fix #69's torque unit label from `MN·M` to `mN·m` before merge. Recheck #74's main-branch status statements after integration. A wheel-step action needs a fresh explicit Julian go each time.
9. Resolve TD §7.4 versus §11.5 on balancing UI (#66) before implementing that scope. Rebuild `hardware-stack.glb` and posters through the CAD pipeline (#22), then run staleness checks. Synchronize FD §17, TD §7.4, the Hold/Slew heading rule and refusal strings after the implementation lands.

Integrate feature branches in dependency order and resolve conflicts against the current docs and tests. Run each feature's exact `## Tests` list, recording `[x]` or `[FAIL]` truthfully. Run `tools/check-all.sh --firmware --frontend` and the real-box capture and bench checks on the integrated candidate before the set PRs reach `main`. Any command that can turn the motor needs Julian's explicit go for that step. Commits carry no Codex co-author trailer.

## Status after the Set 5 pass (2026-10-11)

Plan items 4 to 7 are built on the R4.4 stack and verified on the host and the mock box; none has run on hardware.

| Item | Feature | Where | Host evidence | Hardware still owed |
| --- | --- | --- | --- | --- |
| 4 | Compass heading, `mag_axis_map`, `mag_cal`, absolute HOLD | R5.1 | `test_pointing.cpp` (26 new cases across R5.1 to R5.4), mutants `mag_snap_armed` and `mag_sign_flip`, `tools/calibrate.py mag` | T-9 at three wheel speeds (spin needs Julian's go); 60 s HOLD drift |
| 5 | MANUAL `wheel` and `rate` | R5.2 | `test_pointing.cpp`, HT-11 re-run with a characterised box | Step and rate tracking on the pinned box |
| 6 | `sun_cal`, `light_threshold`, `sun_deg`, SUN_POINT, `sun_invalid` exit | R5.4 | `test_pointing.cpp` sun and SUN_POINT cases, closed-loop lamp sim, mutants `sun_extrapolate` and `no_sun_lost`, `tools/calibrate.py sun` | T-10 and a lamp |
| 7 | Guarded respin, `FF_TABLE_MISSING`, `RATE_GAIN_REDUCED`, `PARAMS_RESET` reachable | R5.2, R5.3 | 3 respin cases, mutant `no_respin_range`, the PARAMS_RESET case | Respin on the pinned box |

Integration done in the same pass: schema flags and examples; the mock box (`--lamp`, the firmware's reasons for the new commands, a selftest each); `ground/link` and `tools/check-link.sh` (TAP summary on Node 22); the UI's mode menu (DETUMBLE, SUN_POINT and respin no longer "Firmware pending"), refusal hints, and parameter panel (`rate_gain_scale`, `spinup_throttle`, `hall_expect_throttle`; the bench keys stay with the bench console); FD §11.3, §11.4, §13.1, §15.3, §16, §23 (V-68 to V-70) and `protocol/README.md` Changes.

Items 8 and 9 are not part of this pass. #69, #70, #74, #75, #50 and #51 are still separate PRs against `main` or the Set 4 stack. This branch merges `origin/main` as of 2026-10-11, so it already carries the UI that is on `main`; the unmerged UI PRs (#33 and the PRs stacked on it) are not in it.

What no check here can show: the ESP-IDF build (no toolchain in the cloud container, so `control_task.cpp` is reviewed, not compiled, and IRAM use is unmeasured), the 1000 µs tick budget with the compass step in the tick (the step is single precision to keep it near 2 µs; F-4 must be re-run on the R3.5 build with Wi-Fi), and every hardware gate.
