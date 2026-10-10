# Revision Set 3: Teleop

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
This file is the index and roll-up log for set 3. Per-feature specs live in the
sibling `R3.M-*` files; load only the feature(s) you are working on.

**Demo at the end of this set:** an operator arms the box from the browser, the wheel spins up, and in MANUAL mode the operator drives it with the keyboard. Each rung of the teleop ladder can be shown on its own:
- **Rung 1, throttle:** open-loop nudges.
- **Rung 2, wheel speed:** a closed loop holds the commanded wheel speed.
- **Rung 3, rate:** the operator commands box rate. This needs the rig to turn, so it uses a makeshift turntable.

E-stop, heartbeat loss, and every interlock drop the box to SAFE. This is the TD §11.4 fallback floor and everything above it short of autonomous modes.

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

Then R3.4 wires everything into the control task, and R3.5 adds comms. The rungs R3.7, R3.8, and R3.9 follow in order. `direcsun_drivers/CMakeLists.txt` switches to `SRC_DIRS "src"` in whichever of R3.1 to R3.3 merges first; the others rebase.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R3.1 dshot-output: DShot frame encoder (HT-1) and RMT driver, with an on-chip loopback self-test
- [ ] R3.2 sensor-bus: MPU-6050, QMC5883P, and BMP280 on one i2cdev-owned bus at 400 kHz
- [ ] R3.3 hall-and-adc: Hall edge capture and wheel-speed estimates (HT-2), battery and sun ADC
- [ ] R3.4 control-task: 500 Hz control task, boot sequence, estimator subset, control-stall watchdog
- [ ] R3.5 comms: Wi-Fi and mDNS, WebSocket server, schema v1 protocol, controlling client
- [ ] R3.6 modes-and-interlocks: SAFE/SPINUP/READY/MANUAL machine, interlock table, ramp-down, parameters, Hall bench bypass
- [ ] R3.7 manual-throttle: rung 1, open-loop spin-up and MANUAL throttle, console teleop panel
- [ ] R3.8 manual-wheel: rung 2, wheel-speed PI with feed-forward, bench characterisation (T-5, T-18)
- [ ] R3.9 manual-rate: rung 3, rate loop, closed-loop host simulation (HT-5, HT-6), sign check

## Open questions / decisions before implementing
1. **Bypass throttle cap (R3.6):** with the Hall sensor absent, the only overspeed protection is a compile-time throttle ceiling. Proposed `kBypassThrottleMax = 400` of 2047. Assuming duty is roughly linear in DShot throttle, that is about 17.5% duty, or about 1600 rpm no-load at 10 V (920 KV); it is labelled [A]. R3.7 measures the real speed with the oscilloscope on a motor phase before anything relies on it. Recommendation: accept 400.

## DB changes in this set
None.

## Log
- 2026-10-10: Set scaffolded. Decisions from Julian: stack on Set 1; Hall bench bypass with a hard cap; bench supply power; oscilloscope available; turntable for rotation; keyboard and slider teleop; DShot150 with stock ESC settings.
