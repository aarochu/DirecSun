# DirecSun project context

Bootstrap for every session: read this first, then the `_set.md` of the set you are working on, then only the feature file you are touching. This file holds project invariants that are not obvious from the code and do not change per set. Design detail lives in `docs/`; this file is the delta.

## What this is

DirecSun (formerly PocketADCS) is a single-axis reaction-wheel attitude trainer in a 3D-printed 2U CubeSat frame, built for a hackathon whose presentation is about 2026-10-11. A brushless motor spins a flywheel; changing its speed turns the box the opposite way. An ESP32-S3 on the box closes the loop from a gyro, a magnetometer, and a two-element sun sensor, and talks to a browser over Wi-Fi.

- System design: `docs/technical-design.md` (TD).
- Firmware design: `docs/firmware-design.md` (FD).

## Layout and ownership

| Path | What | Owner |
| --- | --- | --- |
| `firmware/` | ESP-IDF v6.0 project (run `idf.py` from here) | Julian |
| `protocol/` | Wire schema v1, the firmware/ground contract | Shared |
| `ground/link/` | Dependency-free ES module implementing the client side of schema v1 | Julian |
| `ground/console/` | Bench console (DirecSun1 page) built on the link module; not the demo UI | Julian |
| `cad/` | CadQuery sources; `cad/out/` holds generated STL and previews | Design: Julian and agents. Printing and measuring: Aaron |
| `docs/` | TD, FD, `sizing.py`, reference PDFs | Julian |
| `tools/` | Bootstrap, build, and check scripts | Shared |
| `changes/` | Spec-driven-dev sets | Shared |

The demo UI belongs to Aaron and lives outside this repo. It imports `ground/link/`.

## Source-of-truth precedence

1. Measurements recorded in TD §12.2.
2. `cad/stack.py` and `docs/sizing.py`. They win over TD prose.
3. TD.
4. FD.
5. `protocol/schema-v1.json` must agree with FD §15. A disagreement is a bug in whichever changed last.

The repo markdown is authoritative. The original Claude Doc export and `message.txt` are frozen history.

## Toolchain

- ESP-IDF v6.0 installed by EIM; target `esp32s3` (Seeed XIAO ESP32-S3, 8 MB flash). `tools/idf.sh <args>` activates the environment and runs `idf.py` in `firmware/`.
- Firmware language is C++17 (`gnu++17`), with exceptions and RTTI off. The vendored driver libraries are C.
- `firmware/sdkconfig` is generated and gitignored. Intent goes in `firmware/sdkconfig.defaults`, which is tracked and annotated line by line in the style of drone-flight-software. Wi-Fi credentials go in `firmware/sdkconfig.defaults.local`, which is gitignored. After editing either defaults file, delete `sdkconfig` and rebuild, or the old values win.
- `firmware/dependencies.lock` is committed for reproducible builds. Never hand-edit it, `managed_components/`, or `build/`.
- ESP-IDF v6.0 does not bundle `json` or `mdns`; both come from the component registry (`espressif/cjson`, `espressif/mdns`).
- Python 3.12 venv at `.venv` (CadQuery has no 3.13 wheels), created by `tools/bootstrap-venv.sh`.
- Node 20 or later for `ground/link` tests (`node --test`), with no npm dependencies.

## Hardware facts agents get wrong

- The sensor libraries are esp-idf-lib `mpu6050` ^2.1.9, esp-idf-lib `bmp280` ^1.0.7, and a vendored `duruofu/qmc5883p` 0.1.0. Each was validated alone on an ESP32-S3 at SDA GPIO5 / SCL GPIO6 in `~/projects/{mpu6050,bmp280,qmc5883p}-espidf-example`. They had never shared a bus before this repo.
- i2cdev owns I2C port 0. The QMC driver takes an `i2c_master_bus_handle_t`, so it must receive `i2cdev_get_shared_handle(I2C_NUM_0, ...)` after the first i2cdev transaction has created the bus. Creating a second bus on port 0 fails.
- The esp-idf-lib drivers default to 1 MHz SCL and the QMC driver hard-codes 100 kHz. DirecSun runs every device at 400 kHz (TD §5.3).
- I2C addresses: MPU-6050 0x68, QMC5883P 0x2C, BMP280 0x77 (Adafruit board default, not 0x76).
- Firmware logs reach USB-C through the secondary USB-Serial-JTAG console. The primary console stays on UART0, as in the validated examples.
- Do not copy glue code from `~/projects/drone-flight-software/main`. It does not compile. Copy only its `sdkconfig.defaults` approach.
- The verified Wi-Fi link is DirecSun1: `esp_http_server` WebSocket on port 81, path `/`, STA mode. It ran on an ESP32-C3 through PlatformIO; this repo targets the S3 with plain `idf.py`.

## Safety rules (binding for humans and agents)

- Agents may build, flash, and monitor freely when the bench rig is on USB. Any step that can turn the motor stops, states exactly what will happen, and waits for Julian's explicit go, every time. "Can turn the motor" means: sending `arm`, entering any mode with nonzero throttle, or sending DShot throttle above 0 while the ESC is powered.
- FD §21 rules are binding: never send DShot commands 1 to 47 from flight code; never raise the 4000 rpm cap or weaken an interlock without a TD revision; no automatic reconnect or re-arm; never delete or weaken a positive control (HT-6, the `assembly.py` controls, the `--selftest` modes of the check scripts).
- `firmware/components/direcsun_core` includes no ESP-IDF, FreeRTOS, or driver headers. `tools/check-core-purity.sh` enforces this.

## CAD rules

- Never edit STL or STEP files by hand. `tools/build-cad.sh` is the only producer of `cad/out/`. STL and preview PNG files are committed; STEP files are gitignored.
- `tools/check-cad-stale.py` fails when a script or `stack.py` changed without a rebuild.
- TD §8.3 to §8.5 invariants apply: assembly positive controls, `HOLE_COMP`, no supports, shared dimensions only in `stack.py`.

## Conventions

- Sign convention: counter-clockwise positive seen from above, +z up the spin axis (FD §2.3). The wire carries CCW-positive values. A UI converts for display; the link module does not.
- Wire protocol: schema v1, JSON text frames at `ws://direcsun.local:81/` (mDNS). Additions before the freeze at the end of Set 1 stay v1; any change after it bumps `v`.
- Git: `main` is the integration branch. Set branch `feat/setN-slug` off `main`; feature branch `feat/setN/RN.M-slug` off the set branch. Agents commit and push set and feature branches. Feature branches merge into the set branch locally once their Tests pass (the spec-gate hook enforces this). A set reaches `main` only through a GitHub PR, opened after the agent has verified every feature itself, and merged by Julian.
- Commit messages carry no agent co-author trailer. Prose and messages use no em dashes.
- Unrelated defects found while working: fix them inline if the fix is under about five minutes and low-risk, and note it in the feature log. Otherwise add them to the current `_set.md` Open questions, or open an issue on aarochu/DirecSun.

## Rejected alternatives (do not re-litigate)

- Arduino-ESP32 or PlatformIO, the FD's original toolchain: replaced by ESP-IDF v6.0 to match the validated sensor libraries and the drone environment.
- C, as in the drone repo: C++17 chosen.
- arduinoWebSockets: replaced by `esp_http_server`, verified in DirecSun1.
- A static 10.TE.AM.120 address on the OM5P-AC as the only network mode: kept as a menuconfig option. The default is STA with DHCP and mDNS, with SoftAP as a fallback.
- TD §10.2 automatic momentum dump: replaced by `respin`, the lock-pin desaturation from TD §3.6.
- "Nothing printed spins with the wheel": dropped for the flywheel, which becomes a printed PETG wheel with sealed washers. Set 2 revisits the hazards and PR-4.
- A full plant simulator for the UI: dropped (TD §11.5). The closed-loop host simulation in `firmware/test/host` is for control tests only.
