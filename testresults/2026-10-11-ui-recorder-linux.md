# Test: UI turn recorder and capture checker on Linux (PR #51, issue #40)

- Date/time and timezone: 2026-10-11, 03:00 to 03:30 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. The recorder armed only `tools/mock-box.py` on 127.0.0.1:8765.
- Component:
  - `tools/ui-turn-recorder.mjs`: drives the ground station and records every WebSocket frame.
  - `tools/check-ui-capture.py`: judges the capture.
- Branch/commit and builds:
  - The tools from #51 at 534f77b, merged with `feat/set4/R4.3-hold-and-slew` at 8622d57.
  - The UI built from #33 at 43f79e4, and the control from `main` at a2ec2a2.
  - The box was #50's parity mock.
- Procedure and acceptance-criterion reference: #40's "Done when" list, with the ping rule as #51 had it: every gap within 200 ± 60 ms.
- Rig setup: [`rec51-run.sh`](2026-10-11-ui-recorder-linux/rec51-run.sh) starts a fresh mock for each scenario, runs the recorder, stops the mock and runs the checker. Google Chrome 154 on Linux.
- Instruments, calibration or positive-control check:
  - The checker's `--selftest` passes: every planted capture fails for its own reason.
  - For `hidden`, the page's own `visibilitychange` record is the instrument check. The positive control is a `main` build, which must trip `HEARTBEAT_LOST`.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | `turn --turns 4`, headless, as written ([recorder](2026-10-11-ui-recorder-linux/1-headless-turn-hung-recorder.txt)) | Connected, then "FAILED: locator.click: Timeout 30000ms" on "Arm the box". The process and its Chrome were **still running 10 min later**, after "stopped early" was printed, and were killed by hand | A failed run exits | **Fail, two defects.** The page starves in headless Chrome on Linux (see [display lag](2026-10-11-ui-display-lag.md)). A failed run never closes its browser or stops its sampler, so against a real box the page would stay connected and keep an armed box armed |
| 2 | The same scenarios in headed Chrome, through a local one-line patch (`headless: false`) ([summary](2026-10-11-ui-recorder-linux/2-headed-summary.txt)) | `turn`: 8 SLEWs, ack max 29 ms, 0 stale, ping max 248 ms. `estop`: re-arm refused `ramp-down in progress`, ping max 258 ms. `watch`: 0 non-tlm frames before the first send, ping max 244 ms | Checker passes | Pass ([turn](2026-10-11-ui-recorder-linux/2-headed-turn-check.txt), [estop](2026-10-11-ui-recorder-linux/2-headed-estop-check.txt), [watch](2026-10-11-ui-recorder-linux/2-headed-watch-check.txt)) |
| 3 | `stop`, three runs ([1](2026-10-11-ui-recorder-linux/2-headed-stop-check.txt), [2](2026-10-11-ui-recorder-linux/2-headed-stop-2-check.txt), [3](2026-10-11-ui-recorder-linux/2-headed-stop-3-check.txt)) | Failed 2 of 3, each on a single gap: 278 ms, then about 261 ms. The third passed | Passes | **Fail on the 200 ± 60 ms rule.** The gaps are jitter in a visible page |
| 4 | `hidden` 30 s, #33 build ([check](2026-10-11-ui-recorder-linux/2-headed-hidden-33-check.txt), [frames](2026-10-11-ui-recorder-linux/2-headed-hidden-33-frames.jsonl.gz)) | Hidden 30.0 s (one hidden event). Stayed READY, no `HEARTBEAT_LOST`. 3 of 204 gaps outside 200 ± 60: 286 ms at startup, and 343 ms then 57 ms right after the page came back. Every gap while hidden was inside 200 ± 60 | Passes | **Fail on the 200 ± 60 ms rule**, though the safety property held |
| 5 | `hidden` 30 s, `main` build, the positive control ([check](2026-10-11-ui-recorder-linux/2-headed-hidden-main-check.txt), [frames](2026-10-11-ui-recorder-linux/2-headed-hidden-main-frames.jsonl.gz)) | Hidden 30.0 s. `HEARTBEAT_LOST` at 32.5 s; 33 of 86 gaps outside, max 1016 ms | Fails | Pass: the control fails as it must |
| 6 | The arm gate: `--url ws://direcsun.local/ws` ([output](2026-10-11-ui-recorder-linux/3-gate-direcsun-local.txt)) | Exit 3 before opening a browser; no output directory written | Refuses | Pass |
| 7 | The arm gate: `--url ws://127.box.example/ws` ([output](2026-10-11-ui-recorder-linux/3-gate-127-box-example.txt)) | Printed "(loopback mock)" and went on toward arming. It stopped only because a bogus `--ui-dir` was given on purpose | Refuses: not a loopback address | **Fail.** `/^127\./` matches any DNS name starting "127." |

## Outcome

- Overall: **Fail, with the fixes now decided.**
  - The checker and the hidden-tab positive control work.
  - Steps 1 and 7 are recorder defects.
  - Steps 3 and 4 showed that the per-gap 200 ± 60 ms rule fails good runs.
- Decision (Julian, 2026-10-11, #40): the ping rule is now every gap under 500 ms, a median within 200 ± 20 ms, a pong for every ping, and no `HEARTBEAT_LOST`. Under it, steps 3 and 4 pass, and step 5 still fails.
- Follow-up owner and action: Aaron, in #86 (R4.1's own tools). That covers:
  - the new ping rule;
  - closing a browser that will not exit after 10 s;
  - a Chrome or Chromium fallback with headed `hidden` under `xvfb-run`;
  - an exact loopback host list.

  #51 closes as superseded once #86 merges. #86 reports the R4.1 items still failing in a container without a GPU, so they need a rerun on this laptop.
