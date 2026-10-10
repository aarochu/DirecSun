# DirecSun

DirecSun (formerly PocketADCS) is a single-axis reaction-wheel attitude trainer in a 3D-printed 2U CubeSat frame. The box turns on two 608 bearings around a fixed bolt. Inside it, a 2212 brushless motor spins a flywheel; speeding the wheel up or slowing it down turns the box the other way, the way a satellite's reaction wheel does. A Seeed XIAO ESP32-S3 closes the loop from a gyro, a magnetometer, and a two-element sun sensor, and a browser ground station on Wi-Fi commands it and plots telemetry.

The demonstration modes are detumble (stop a hand spin), hold (keep a heading against pokes), slew (turn to a commanded heading), sun point (track a lamp), and manual teleoperation. The full system design is in [docs/technical-design.md](docs/technical-design.md) and the firmware design in [docs/firmware-design.md](docs/firmware-design.md).

## Repository layout

| Path | What |
| --- | --- |
| `firmware/` | ESP-IDF v6.0 project for the XIAO ESP32-S3, in C++17 |
| `protocol/` | Wire schema v1: the contract between the box and any ground station |
| `ground/link/` | Engineering bench client: dependency-free JavaScript module for the protocol |
| `ground/console/` | Engineering bench console for firmware bring-up (the main UI is `frontend/`) |
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

The bench console and the link module are engineering tools for firmware bring-up and characterisation; the main UI is `frontend/`. Both talk to the box at `ws://direcsun.local/ws` (port 80, SoftAP `ws://192.168.4.1/ws`), or to the protocol stub `tools/mock-box.py` before the firmware speaks schema v1. ES modules do not load over `file://`, so serve the repo over HTTP:

```bash
tools/mock-box.py                  # stub box on ws://127.0.0.1:8081/ws (needs tools/bootstrap-venv.sh)
python3 -m http.server 8000        # from the repo root
# open http://127.0.0.1:8000/ground/console/ and connect to ws://127.0.0.1:8081/ws
tools/check-schema.py              # protocol fixtures against protocol/schema-v1.json
tools/check-link.sh                # link module tests, with every sent message schema-checked
```

The main UI's Device link panel connects to the same stub at `ws://127.0.0.1:8081/ws`. On the wire, every quantity about the spin axis is clockwise positive, as the UI expects (`protocol/README.md`).

## Working in this repo

Read [AGENTS.md](AGENTS.md) and [changes/CONTEXT.md](changes/CONTEXT.md) before changing anything. Work is planned and tracked in `changes/` with the spec-driven-dev workflow: every feature has a spec, and its test scheme is the gate for merging.
