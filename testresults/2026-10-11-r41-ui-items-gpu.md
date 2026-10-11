# Test: R4.1's UI items against the mock on a laptop with a GPU, and the R3.5 kit on PR #86

- Date/time and timezone: 2026-10-11, 05:00 to 05:23 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. Every box was `tools/mock-box.py` on loopback.
- Component: R4.1's two "no hardware" Tests items (`changes/4-judge-sequence/R4.1-ui-command-contract.md`):
  - "UI against the mock": `turn`, `stop`, `estop`, `watch`;
  - "Heartbeat in a hidden tab".

  #86 reported both still failing in a container without a GPU.
- Builds:
  - The recorder, checker and mock from #86 (98c1fc5; code identical to 3b3b0cf). The ping rule is the one Julian approved on #40.
  - The UI from `feat/ui-telemetry-plots` at a696f01, which is `main` plus #69 and #70, built with `vite build` and served with `vite preview`.
- Rig setup: Linux laptop, Google Chrome 154, headed (`--headed`), with the GPU flags from #86. Load average 4 to 8 from other work.
  - [`r41-run.sh`](2026-10-11-r41-ui-items-gpu/r41-run.sh) starts a fresh mock for each scenario, runs the recorder, stops the mock and runs the checker.
  - The hidden-tab item also ran through [`stageB.mjs`](2026-10-11-r41-ui-items-gpu/stageB.mjs): plain Chrome over CDP, hidden behind `about:blank`.
- Instruments, calibration or positive-control check:
  - The checker's selftest passes in #86 (planted 600 ms gap, 300 ms cadence and `HEARTBEAT_LOST`).
  - For hidden, the page's own `visibilitychange` record is required.
  - The positive control that a hidden page's timer trips the box is in [2026-10-11-ui-hidden-tab-heartbeat.md](2026-10-11-ui-hidden-tab-heartbeat.md) (`main` before #33).

## Steps and observations

| Step | Scenario | Measured value and units | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | `turn`, 4 turns ([check](2026-10-11-r41-ui-items-gpu/turn-check.txt), [recorder](2026-10-11-r41-ui-items-gpu/turn-recorder.txt)) | 4 × "TURN COMPLETE", 8 SLEWs, 19 mode events. Acks p50 4.3 ms, max 14.0 ms. Pings median 200 ms, max gap 221 ms | Checker passes | Pass |
| 2 | `stop` ([check](2026-10-11-r41-ui-items-gpu/stop-check.txt)) | "TURN STOPPED", max gap 211 ms | Passes | Pass |
| 3 | `estop` ([check](2026-10-11-r41-ui-items-gpu/estop-check.txt), [recorder](2026-10-11-r41-ui-items-gpu/estop-recorder.txt)) | Re-arm inside the ramp: "ARM REFUSED: ramp-down in progress". Max gap 252 ms | Passes, refusal shown | Pass |
| 4 | `watch` ([check](2026-10-11-r41-ui-items-gpu/watch-check.txt), [recorder](2026-10-11-r41-ui-items-gpu/watch-recorder.txt)) | The watch page's `tlm` shows `HEARTBEAT_LOST` 1009 ms after the operator's last ping, as designed. Operator max gap 316 ms; watcher 251 ms | Passes | Pass |
| 5 | `hidden` with #86's recorder ([recorder](2026-10-11-r41-ui-items-gpu/hidden-recorder.txt), [check](2026-10-11-r41-ui-items-gpu/hidden-check.txt)) | Recorder: "the page stayed visible behind another tab", exit 1. **The checker passed the same capture** | A real hidden period | Not a result. Playwright-launched Chrome never hides a page, even headed. The checker does not require a hidden period (reported on #86) |
| 6 | Hidden tab in plain Chrome over CDP ([result](2026-10-11-r41-ui-items-gpu/hidden-cdp.json), [mock](2026-10-11-r41-ui-items-gpu/hidden-cdp-mock.log)) | `visibilitychange` to `hidden` at 8.6 s and back at 39.6 s. 151 pings in 30.0 s (5.03 Hz), max gap 201 ms. The mock stayed READY with no `HEARTBEAT_LOST` | 5 Hz while hidden, no `HEARTBEAT_LOST` | Pass |
| 7 | R3.5 kit on #86, all 43 cases ([summary](2026-10-11-r41-ui-items-gpu/kit-suite-pr86-summary.txt)) | 41 of 43. `t3-pass` passes, and `t3-f-ccw` fails for the right reason (net −90.6°). BAD: `t5-pass-bursty-sender` and `t7-pass`, both on "capture file agrees with capture-box's summary" (file 40 vs 37, and 2 vs 0) | 43 of 43 | Fail, see step 8 |
| 8 | Control: `t5-pass-bursty-sender` on `feat/set3/R3.5-comms` at 3799644 ([output](2026-10-11-r41-ui-items-gpu/kit-control-r35-bursty.txt)) | Same failure, file 40 vs capture-box 35 | Pass | Fail on R3.5 too: the mismatch came with 3799644's loss-count fix, not #86 |

## Outcome

- Overall: **Pass for R4.1's two UI items on a machine with a GPU** (steps 1 to 4, and 6).
- Two tool defects are reported on #86:
  - The checker does not require a hidden period, and the recorder cannot hide a page.
  - The kit's capture cross-check still uses the old loss method.
- Follow-up owner and action:
  - Aaron (#86): record these results in R4.1's `## Tests`, and fix the two tool defects.
  - The hardware items stay with Julian's go.
