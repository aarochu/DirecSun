# Agent guide

This file is the entry point for every agent working in this repository (Claude Code reads it through `CLAUDE.md`).

## Start here

1. Read `WORKSTREAMS.md`: who owns which paths (Julian or Aaron) and how to request work across the line. Never edit the other person's paths, except where `WORKSTREAMS.md` allows it (since 2026-10-11, Aaron's work may edit the unfinished software, #80).
2. Read `changes/CONTEXT.md`. It holds the project invariants, the toolchain, the hardware facts that agents get wrong, and the safety rules.
3. Find the open set in `changes/` (the highest-numbered `N-slug/` whose `_set.md` Status is not all `[x]`) and read its `_set.md`.
4. Load only the feature file you are working on. Do not re-derive context from the whole codebase.

## Workflow

Work follows the spec-driven-dev skill (`.agents/skills/spec-driven-dev/SKILL.md`):

- Every feature has a spec in `changes/N-slug/RN.M-slug.md` that is approved before code is written.
- Branches: set branch `feat/setN-slug` off `main`; feature branch `feat/setN/RN.M-slug` off the set branch.
- A feature's `## Tests` section is its merge gate. Run every item literally and mark it `[x]` or `[FAIL]`; never check an item you did not run. Flip the `_set.md` marker `[ ]` to `[~]` to `[t]` to `[x]` as each state is reached.
- The `spec-gate` hooks in `.agents/settings.json` enforce the gate on `git merge`. If a hook blocks you, fix the paper trail; do not work around it.
- A set reaches `main` only through a GitHub pull request, opened after you have verified every feature yourself, and merged by Julian.

## Binding contracts

Change these only through a spec that says so:

- Sign and frame conventions: FD §2.3 (counter-clockwise positive, +z up).
- Mode machine: FD §13 and TD §6.6.
- Interlocks: FD §14 and TD §6.7.
- Wire protocol: `protocol/schema-v1.json` and FD §15.
- Rules for agents changing the firmware: FD §21.
- CAD pipeline invariants: TD §8.3 to §8.5.

FD is `docs/firmware-design.md`; TD is `docs/technical-design.md`.

## The main UI

`frontend/` (Aaron) is the main UI. The backend matches it: when the UI's expectations of the box differ from the plan, Julian changes the firmware and `protocol/schema-v1.json`, not the UI, and records the change in FD §15 and §17. Do not edit `frontend/` from backend work. From the UI side, request contract changes as `Contract:` issues (see `WORKSTREAMS.md`); do not edit `protocol/`, `firmware/`, or the mock box, except under the 2026-10-11 section of `WORKSTREAMS.md`. `ground/link` and `ground/console` are engineering bench tools.

## Hardware safety

Agents may build, flash, and monitor the bench rig freely. Any step that can turn the motor stops, states exactly what will happen, and waits for Julian's explicit go, every time. See `changes/CONTEXT.md` for what counts.

## Skills in this repo

| Skill | Use it for |
| --- | --- |
| `spec-driven-dev` | Planning, scaffolding, resuming, and merging feature sets |
| `cad-skill` (`parametric-3d-printing`) | Changing CadQuery parts; its runner validates every build |
| `esp-idf` | Building, flashing, monitoring, and debugging the firmware |
| `esp-idf-components` | Adding or vendoring ESP-IDF components and registry dependencies |
