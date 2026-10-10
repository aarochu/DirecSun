# Revision Set 1: Foundation

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
This file is the index and roll-up log for set 1. Per-feature specs live in the
sibling `R1.M-*` files; load only the feature(s) you are working on.

Goal: one repository that holds the docs, CAD, firmware skeleton, wire contract, and checks, so that Sets 2 to 5 start from frozen interfaces. Set 1 is not a demo; it is the base every demo set builds on.

Branch: `feat/set1-foundation`. Order: R1.1 first, because every other feature needs its skeleton. R1.2, R1.3, and R1.4 touch disjoint directories and can run in parallel. R1.5 needs R1.4. R1.6 needs R1.2, because the schema follows the amended FD §15. R1.7 comes last.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R1.1 scaffold-and-skills: monorepo skeleton, skill submodules, spec-gate hooks, README, AGENTS.md
- [ ] R1.2 docs-import: TD and FD into docs/, renamed, diagrams rebuilt, approved amendments and the MANUAL mode applied
- [ ] R1.3 cad-import: CadQuery sources, cad/out pipeline, venv bootstrap, staleness check
- [ ] R1.4 firmware-skeleton: ESP-IDF v6.0 C++17 project that builds and boots on the XIAO ESP32-S3
- [ ] R1.5 host-test-harness: doctest host build with a positive control, core purity check
- [ ] R1.6 protocol-link-console: schema v1, link module, bench console, mock box
- [ ] R1.7 ci: GitHub Actions running every check in this set

## Open questions / decisions before implementing
1. **Mock box (R1.6):** add `tools/mock-box.py`, a protocol stub (not a plant model) that streams schema v1 `tlm` at 50 Hz, acks commands, and enforces the heartbeat. Without it, the console's end-to-end test and Aaron's UI have nothing to talk to until Set 3's firmware speaks v1. It adds the `websockets` Python package to the tools venv. Recommendation: yes. Confirm before implementing.
2. **MANUAL setpoint staleness (R1.2, binding once written into the TD):** when `cmd:"manual"` setpoints stop for 250 ms while pings continue, the `rate` input falls to ω_cmd = 0 (the box stops turning), while `throttle` and `wheel` hold their last value (the wheel holds speed, so the box does not move). The 1 s heartbeat rule still ramps everything down. Recommendation: accept. Confirm before implementing.

## DB changes in this set
None.

## Log
- 2026-10-10: Set scaffolded from the grilling session (layout, C++17, repo-authoritative docs, cad/out policy, link module, network, sets by demo milestone, hardware policy, git flow, CI, MANUAL mode).
