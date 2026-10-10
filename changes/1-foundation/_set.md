# Revision Set 1: Foundation

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
This file is the index and roll-up log for set 1. Per-feature specs live in the
sibling `R1.M-*` files; load only the feature(s) you are working on.

Goal: one repository that holds the docs, CAD, firmware skeleton, wire contract, and checks, so that Sets 2 to 5 start from frozen interfaces. Set 1 is not a demo; it is the base every demo set builds on.

Branch: `feat/set1-foundation`. Order: R1.1 first, because every other feature needs its skeleton. R1.2, R1.3, and R1.4 touch disjoint directories and can run in parallel. R1.5 needs R1.4. R1.6 needs R1.2, because the schema follows the amended FD §15. R1.7 comes last.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [x] R1.1 scaffold-and-skills: monorepo skeleton, skill submodules, spec-gate hooks, README, AGENTS.md
- [x] R1.2 docs-import: TD and FD into docs/, renamed, diagrams rebuilt, approved amendments and the MANUAL mode applied
- [x] R1.3 cad-import: CadQuery sources, cad/out pipeline, venv bootstrap, staleness check
- [x] R1.4 firmware-skeleton: ESP-IDF v6.0 C++17 project that builds and boots on the XIAO ESP32-S3
- [x] R1.5 host-test-harness: doctest host build with a positive control, core purity check
- [x] R1.6 protocol-link-console: schema v1, link module, bench console, mock box
- [t] R1.7 ci: GitHub Actions running every check in this set
- [x] R1.8 match-ui-wire-contract: wire sign clockwise-positive and endpoint port 80 /ws, matching the main UI (frontend/, PR #1)
- [t] R1.9 frontend-ci: the main UI in CI and in the repo guides (after PR #1 is merged)

## Open questions / decisions before implementing
None. Both questions were answered 2026-10-10: the mock box is in (R1.6), and the MANUAL staleness rule is accepted as recommended (R1.2).

## DB changes in this set
None.

## Log
- 2026-10-10: Set scaffolded from the grilling session (layout, C++17, repo-authoritative docs, cad/out policy, link module, network, sets by demo milestone, hardware policy, git flow, CI, MANUAL mode).
- 2026-10-10: Specs approved by Julian; open questions resolved.
- 2026-10-10: Aaron's frontend (PR #1) became the main UI. Decisions: the backend matches the UI, the wire is clockwise-positive, the endpoint is port 80 at `/ws`, the UI owns the operator client, and `ground/link` and the console are bench tools. R1.8 and R1.9 were added to carry this into Set 1 before it reaches `main`.
