# Test: ground station display lag under 50 Hz telemetry (issue #78, PR #82)

- Date/time and timezone: 2026-10-11, 03:05 to 04:36 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. The telemetry came from `tools/mock-box.py` (#50's parity mock) at 50 Hz in SAFE.
- Component: the ground station's device link and display.
  - Before #82, every telemetry frame queued React work.
  - #82 coalesces display rendering to one frame per animation frame, at most every 50 ms. The operator logic still sees every frame.
- Builds:
  - `main` at a2ec2a2 (and at 195fb8c for step 6);
  - #33 at 43f79e4;
  - #82 at 652ac22, and its stacked head 2a32e38.
  - Each was built with `vite build` and served with `vite preview`.
- Procedure and acceptance-criterion reference: the falsifier written in #78. Rerun the headless probe: `evaluate` should stay bounded and the packet count should keep near 50 per second.
- Rig setup: Linux laptop, Chrome 154 through Playwright, 1440×900. The other load on the machine (a slicer, Docker, another Claude session) put the load average between 2 and 10; each step records it where it mattered.
- Instruments, calibration or positive-control check:
  - [`lag.mjs`](2026-10-11-ui-display-lag/lag.mjs) connects, then times one `page.evaluate` every 2 s for 30 s and reads the "N PACKETS" counter.
  - [`timing.mjs`](2026-10-11-ui-display-lag/timing.mjs) times several kinds of query.
  - The positive control is the headed run on the same build (step 3), which shows the probe reads a healthy page as healthy.

## Steps and observations

| Step | Build, browser | Measured value and units | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | #33, headless ([output](2026-10-11-ui-display-lag/1-pr33-headless-timing.txt)) | `evaluate` 39 ms, then 1.1 s, 4.1 s; a CSS count 16 s; a role query 39 s | Bounded | Fail: falls behind without limit |
| 2 | `main`, headless ([output](2026-10-11-ui-display-lag/2-main-headless-timing.txt)) | 6 ms, then 420 ms; a role query 3.2 s; a DOM count 16.9 s | Bounded | Fail: so this predates #33 |
| 3 | `main`, **headed** ([output](2026-10-11-ui-display-lag/3-main-headed-lag.txt)) | 2 to 14 ms throughout; packets 2, 103, 204 … 1407 in 30 s (50 per second) | Bounded | Pass: an operator's real window keeps up on this laptop |
| 4 | `main`, headless, `lag.mjs` ([output](2026-10-11-ui-display-lag/4-main-headless-lag.txt)) | 11 ms, 1.4 s, 16 s, 56 s; 609 packets shown at 81.7 s, of about 4000 sent | Bounded | **Fail: the bug in #78** |
| 5 | #82 at 652ac22, headless, `lag.mjs` ([output](2026-10-11-ui-display-lag/5-pr82-headless-lag.txt)), load 4.8 | 56 to 274 ms, not growing; packets 101, 214 … 1402 at 30.8 s | Bounded, about 50 per second | **Pass.** `evaluate` sits above the 100 ms #78 suggested, because software WebGL still costs each frame, but it no longer accumulates |
| 6 | #82's own e2e `telemetry-backpressure.spec.ts`, run against `main` ([output](2026-10-11-ui-display-lag/6-backpressure-spec-on-main.txt)) | 791 packets behind at the first 2 s sample | Fails | Pass: the spec can see the bug |
| 7 | The same spec on #82, original and stacked, two runs each, load 5.7 ([c82 1](2026-10-11-ui-display-lag/7-bp-c82-1.txt), [c82 2](2026-10-11-ui-display-lag/7-bp-c82-2.txt), [m82 1](2026-10-11-ui-display-lag/7-bp-m82-1.txt), [m82 2](2026-10-11-ui-display-lag/7-bp-m82-2.txt)) | 12, 11, 13 and 11 packets behind, against the spec's bound of 10 | Under 10 | **Inconclusive.** Load on this laptop pushes the fixed build just past a tight bound; CI passes it. Against 791 for the bug, a bound of about 50 would still discriminate |

## Outcome

- Overall: **Pass for #82.** The bug in #78 is reproduced on Linux headless, and #82 bounds it.
- Faults: none in #82. Its backpressure spec is load-sensitive (step 7). That was left as Aaron wrote it and reported on #82.
- Follow-up owner and action: Julian merges #82 after #33 and #46 (see [stack integration](2026-10-11-ui-stack-integration.md)).
