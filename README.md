# DirecSun

[![ci](https://github.com/aarochu/DirecSun/actions/workflows/ci.yml/badge.svg)](https://github.com/aarochu/DirecSun/actions/workflows/ci.yml)

DirecSun (formerly PocketADCS) is a single-axis reaction-wheel attitude trainer in a 3D-printed 2U CubeSat frame. The box turns on two 608 bearings around a fixed bolt. Inside it, a 2212 brushless motor spins a flywheel; speeding the wheel up or slowing it down turns the box the other way, the way a satellite's reaction wheel does. A Seeed XIAO ESP32-S3 closes the loop from a gyro, a magnetometer, and a two-element sun sensor, and a browser ground station on Wi-Fi commands it and plots telemetry.

The demonstration modes are detumble (stop a hand spin), hold (keep a heading against pokes), slew (turn to a commanded heading), sun point (track a lamp), and manual teleoperation. The full system design is in [docs/technical-design.md](docs/technical-design.md) and the firmware design in [docs/firmware-design.md](docs/firmware-design.md).

## Repository layout

| Path | What |
| --- | --- |
| `firmware/` | ESP-IDF v6.0 project for the XIAO ESP32-S3, in C++17 |
| `protocol/` | Wire schema v1: the contract between the box and any ground station |
| `ground/link/` | Dependency-free JavaScript module implementing the client side of the protocol |
| `ground/console/` | Bench console for firmware bring-up (the demo UI lives outside this repo) |
| `cad/` | Parametric CadQuery sources; printable STLs and previews in `cad/out/` |
| `docs/` | Technical design, firmware design, sizing calculations |
| `tools/` | Bootstrap, build, and check scripts |
| `changes/` | Spec-driven development: one directory per revision set |

## Quickstart

Clone with submodules (the agent skills are pinned submodules):

```bash
git clone --recurse-submodules git@github.com:aarochu/DirecSun.git
```

Each area adds its commands here as it lands.

### CAD

Printable STLs and six-view previews are committed in `cad/out/`, so printing needs no setup. To change a part, edit its script in `cad/` (shared dimensions live in `cad/stack.py`) and rebuild:

```bash
tools/bootstrap-venv.sh        # once: Python 3.12 venv in .venv with pinned CadQuery
tools/build-cad.sh             # all parts, the assembly collision check, cad/out/manifest.json
tools/check-cad-stale.py       # fails if any script changed since cad/out was built
```

See [cad/README.md](cad/README.md) for the print list, hardware, and assembly order.

### Ground link and console

The bench console and the link module talk to the box, or to the protocol stub `tools/mock-box.py` before the firmware speaks schema v1. ES modules do not load over `file://`, so serve the repo over HTTP:

```bash
tools/mock-box.py                  # stub box on ws://127.0.0.1:8081/ (needs tools/bootstrap-venv.sh)
python3 -m http.server 8000        # from the repo root
# open http://127.0.0.1:8000/ground/console/ and connect to ws://127.0.0.1:8081/
tools/check-schema.py              # protocol fixtures against protocol/schema-v1.json
tools/check-link.sh                # link module tests, with every sent message schema-checked
```

The demo UI lives outside this repo and imports `ground/link/direcsun-link.js`.

### Firmware

Needs ESP-IDF v6.0 installed with EIM (`eim install -i v6.0`). `tools/idf.sh` loads that environment and runs `idf.py` in `firmware/`, from any directory:

```bash
tools/idf.sh set-target esp32s3                  # once per clone (creates firmware/sdkconfig)
tools/idf.sh build
tools/idf.sh -p /dev/ttyACM0 flash monitor       # XIAO ESP32-S3 over USB-C; Ctrl+] exits
```

Build defaults live in `firmware/sdkconfig.defaults`. Wi-Fi credentials go in `firmware/sdkconfig.defaults.local` (copy the `.example`; it is gitignored). After editing either file, delete `firmware/sdkconfig` and rebuild.

## Checks

`tools/check-all.sh` runs every repository check in a fixed order, the same way locally and in CI (`.github/workflows/ci.yml`). Each check that has a `--selftest` mode runs it too, proving the check can fail.

```bash
tools/check-all.sh              # purity, host tests, schema, link, mock box, CAD staleness
tools/check-all.sh --firmware   # also builds the firmware (needs ESP-IDF v6.0)
```

Logs land in `build/check-all/`. CI also builds the firmware in the `espressif/idf:v6.0` container and uploads `direcsun.bin`.

## Working in this repo

Read [AGENTS.md](AGENTS.md) and [changes/CONTEXT.md](changes/CONTEXT.md) before changing anything. Work is planned and tracked in `changes/` with the spec-driven-dev workflow: every feature has a spec, and its test scheme is the gate for merging.
