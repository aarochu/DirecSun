# DirecSun

[![ci](https://github.com/aarochu/DirecSun/actions/workflows/ci.yml/badge.svg)](https://github.com/aarochu/DirecSun/actions/workflows/ci.yml)

DirecSun (formerly PocketADCS) is a single-axis reaction-wheel attitude trainer in a 3D-printed 2U CubeSat frame. The box turns on two 608 bearings around a fixed bolt. Inside it, a 2212 brushless motor spins a flywheel; speeding the wheel up or slowing it down turns the box the other way, the way a satellite's reaction wheel does. A Seeed XIAO ESP32-S3 closes the loop from a gyro, a magnetometer, and a two-element sun sensor, and a browser ground station on Wi-Fi commands it and plots telemetry.

The demonstration modes are detumble (stop a hand spin), hold (keep a heading against pokes), slew (turn to a commanded heading), sun point (track a lamp), and manual teleoperation. The full system design is in [docs/technical-design.md](docs/technical-design.md) and the firmware design in [docs/firmware-design.md](docs/firmware-design.md).

## Repository layout

| Path | What |
| --- | --- |
| `frontend/` | The main UI: the landing page (Vite, TypeScript, three.js); the ground station (operator client) is in `frontend/ground-station/` (React); `frontend/shared/` holds the data and styles both use |
| `firmware/` | ESP-IDF v6.0 project for the XIAO ESP32-S3, in C++17 |
| `protocol/` | Wire schema v1: the contract between the box and any ground station |
| `ground/link/` | Engineering bench client: dependency-free JavaScript module for the protocol |
| `ground/console/` | Engineering bench console for firmware bring-up and characterisation, never shown to judges |
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

### Frontend (main UI)

The landing page lives in `frontend/` and the ground station in `frontend/ground-station/`. The backend matches what this UI expects. Setup and commands are in [frontend/README.md](frontend/README.md); in short:

```bash
cd frontend && npm ci && npm run dev           # landing page, http://localhost:5173
cd frontend/ground-station && npm ci && npm run dev     # ground station, http://localhost:5174
```

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

### UI turn recorder and capture checker

`tools/ui-turn-recorder.mjs` drives the ground station (`frontend/ground-station`) through its own controls (the URL field, Connect, Arm with the lock-pin confirmation, Rotate, Stop turn, the space bar) and records every WebSocket frame in both directions. `tools/check-ui-capture.py` then judges the capture. Playwright comes from `frontend/ground-station/node_modules`, so run `npm ci` there first, and build and serve the UI:

```bash
(cd frontend/ground-station && npm run build && npx vite preview --port 4173)   # the UI
tools/mock-box.py --port 8765                                                    # the box
node tools/ui-turn-recorder.mjs --scenario turn --url ws://127.0.0.1:8765/ws --base http://localhost:4173 --out build/ui-capture/turn
tools/check-ui-capture.py build/ui-capture/turn/frames.jsonl
tools/check-ui-capture.py --selftest        # plants a stale tlm and a missing ack; both must fail
```

Scenarios (`--scenario`): `turn` (N turns, `--turns 4`, alternating right and left), `stop` (Stop turn mid-slew), `estop` (the space bar, then a re-arm 1 s later, which a parity mock refuses with "ramp-down in progress"), `watch` (a second page only watches, then e-stops), `hidden` (arm, hide the operator tab behind another for `--hidden-s 30`, bring it back). Each writes `frames.jsonl`, `timeline.jsonl` (status and result text over time), `summary.json` and screenshots. Frame times are taken inside the page, so ping gaps are the page's own.

The checker passes only if every cmd is acked once within 1000 ms (it prints p50 and max), the first tlm after each mode event has that event's mode, every SLEW ends in `slew_complete` before 6 s (unless the operator interrupted it), pings go out every 200 +/- 60 ms and each gets a pong, a watch-only page receives only tlm until it sends, and no `HEARTBEAT_LOST` occurs. It prints the comms phase (`tlm.t_ms mod 20`).

`hidden` needs plain Chrome, because pages that Playwright drives stay "visible" behind other tabs. The recorder starts `google-chrome` (or `--chrome <path>`) with a debugging port, detaches Playwright before it hides the tab, and reattaches afterwards. Check it with `tools/check-ui-capture.py --expect-hidden --min-hidden-s 30 frames.jsonl`: the run counts only if the page recorded at least 30 seconds hidden, and its ping gaps must still meet 200 +/- 60 ms. Against a ground station whose heartbeat is a page `setInterval` the box trips `HEARTBEAT_LOST` and the check fails (the positive control); a Worker heartbeat (#33) passes. It uses your Chrome window for the 30 s, so leave the machine alone while it runs.

**Before a run against the real box,** this arms the motor. The recorder refuses to send `arm` to anything but a loopback address unless given `--confirm-arm julian-go`, and it prints what it will do first. Julian must confirm, every time: the firmware is flashed and the box is on the turntable; the lock pin is in; `wheel_ref_rpm` is above 0 in READY and `rate_gain_scale` 1 has been sent; and that he is at the box with a hand on the power. Never pass the flag on your own.

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
