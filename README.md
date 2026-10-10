# DirecSun

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

## Working in this repo

Read [AGENTS.md](AGENTS.md) and [changes/CONTEXT.md](changes/CONTEXT.md) before changing anything. Work is planned and tracked in `changes/` with the spec-driven-dev workflow: every feature has a spec, and its test scheme is the gate for merging.
