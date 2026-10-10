# Revision Set 3: Teleop

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
This file is the index and roll-up log for set 3. Per-feature specs live in the
sibling `R3.M-*` files; load only the feature(s) you are working on.

**Demo at the end of this set:** from the bench console, an operator arms the box, the wheel spins up, and in MANUAL `throttle` mode the operator nudges the wheel and the box turns the other way (the TD §11.4 floor). Underneath, two closed loops are proven on a makeshift turntable:
- the wheel-speed loop holds the wheel at speed;
- the rate loop holds the box at rest in READY.

The UI's judge sequence (Set 4) stands on both. E-stop, heartbeat loss, and every interlock drop the box to SAFE.

**Replan 2026-10-10:** `frontend/` (PR #1) is the main UI and the backend matches it. The MANUAL `wheel` and `rate` teleop inputs move to Set 5. Rung 1 (open-loop throttle through the bench console) stays, because it is how the wheel is first spun and characterised.

**Hardware assumed (2026-10-10):**
- Electronics: a breadboarded bench rig with the XIAO ESP32-S3, the three Adafruit sensors on STEMMA QT, the ESC, and the 2212 motor clamped to the bench. It moves to perfboard when ready.
- Power: a current-limited bench supply at about 10 V with a 2 A limit, until the printed NiMH holder exists.
- Instruments: an oscilloscope and a multimeter; no logic analyzer.
- ESC: no BLHeli passthrough, so the firmware uses DShot150 and the ESC's default settings (Brake on stop off).
- Flywheel, Hall magnets, and NiMH pack: they depend on the printers (Set 2), so tests that need them say so and stay `[ ]` until the hardware exists.
- Rotation: a makeshift turntable (the rig screwed to a board on a lazy-susan bearing or a 608 over the bolt) for anything that needs the box to turn.

**Branch:** `feat/set3-teleop` is stacked on `feat/set1-foundation` (approved 2026-10-10). Once Set 1 merges to `main`, Set 3 is rebased onto `main`, and its own PR opens only after that.

**Order.** First wave, in parallel because the files are disjoint:
- R3.1, R3.2, and R3.3 are drivers. Each brings itself up in its own `firmware/test_apps/` project, so none of them edits `main/`.
- R3.6 is pure core logic.

Then R3.4 wires everything into the control task, and R3.5 adds comms. R3.7, R3.8, and R3.9 follow in order. `direcsun_drivers/CMakeLists.txt` switches to `SRC_DIRS "src"` in whichever of R3.1 to R3.3 merges first; the others rebase.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [~] R3.1 dshot-output: DShot frame encoder (HT-1) and RMT driver, with an on-chip loopback self-test
- [~] R3.2 sensor-bus: MPU-6050, QMC5883P, and BMP280 on one i2cdev-owned bus at 400 kHz
- [~] R3.3 hall-and-adc: Hall edge capture and wheel-speed estimates (HT-2), battery and sun ADC
- [~] R3.4 control-task: 500 Hz control task, boot sequence, estimator subset, control-stall watchdog
- [~] R3.5 comms: Wi-Fi and mDNS, WebSocket server, schema v1 protocol, controlling client
- [x] R3.6 modes-and-interlocks: SAFE/SPINUP/READY/MANUAL machine, interlock table, ramp-down, parameters, Hall bench bypass
- [ ] R3.7 manual-throttle: rung 1, open-loop spin-up and MANUAL throttle, console teleop panel
- [ ] R3.8 wheel-loop: wheel-speed PI with feed-forward, bench characterisation (T-5, T-18)
- [ ] R3.9 rate-loop: rate loop, closed-loop host simulation (HT-5, HT-6), sign check; READY holds rate

## Open questions / decisions before implementing
None. `kBypassThrottleMax = 400` was accepted on 2026-10-10 (R3.6). R3.7 measures it before anything relies on it.

## DB changes in this set
None.

## Log
- 2026-10-10: Set scaffolded. Decisions from Julian: stack on Set 1; Hall bench bypass with a hard cap; bench supply power; oscilloscope available; turntable for rotation; keyboard and slider teleop; DShot150 with stock ESC settings.
- 2026-10-10: Specs approved by Julian, and the bypass cap of 400 accepted. The R1.4 and R1.5 commits were merged into this branch (e0ac956) ahead of their Set 1 merge, which is blocked only on R1.4's hardware boot test. Prep commit: `direcsun_drivers` uses `SRC_DIRS` with the full REQUIRES union, and `firmware/test_apps/README.md` fixes the bring-up project pattern. Wave 1 (R3.1, R3.2, R3.3, R3.6) started in parallel.
