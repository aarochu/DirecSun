# Test: operator heartbeat while the ground station tab is hidden (#32 (viii), PR #33)

- Date/time and timezone: 2026-10-10 23:01 to 23:02 and 2026-10-11 02:37 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. The box was `tools/mock-box.py` on loopback; no ESC or motor.
- Component: the ground station's operator client. It sends `ping` at 5 Hz while it controls an armed box, and the box drops to SAFE with `HEARTBEAT_LOST` after 1 s without one (FD §14.4). Chrome throttles a hidden page's main-thread timers to about 1 Hz, but not a Worker's.
- Builds, each built with `vite build` and served with `vite preview`:
  - #34 (`main`'s page `setInterval`, the control);
  - #33 at d19c02f (heartbeat in a Worker);
  - #33 at 43f79e4 (the same, after its review fixes and a merge of `main`).
- Procedure and acceptance-criterion reference: issue #40's `hidden` scenario. Arm to READY against the mock, hide the operator tab behind another tab for 30 s, then show it again. Pass means no `HEARTBEAT_LOST` and pings at 5 Hz while hidden.
- Rig setup: [`stageB.mjs`](2026-10-11-ui-hidden-tab-heartbeat/stageB.mjs).
  - It starts plain `google-chrome` (154, Linux, headed) with a remote-debugging port.
  - It drives the page over raw CDP, hides the tab by opening `about:blank` with `PUT /json/new`, and shows it again with `/json/activate/<id>`.
  - The page records the send time of every `ping` and every `visibilitychange`.
- Instruments, calibration or positive-control check:
  - **Instrument check:** each run must record `visibilitychange` to `hidden`. Pages launched by Playwright never become hidden, so a Playwright run of this test passes whether or not the browser throttles.
  - **Positive control:** the #34 build must trip `HEARTBEAT_LOST`.

## Steps and observations

| Step | Build | Hidden window | Pings while hidden | Max ping gap | Mock box | Result |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | #34, page `setInterval` ([result](2026-10-11-ui-hidden-tab-heartbeat/1-pr34-page-interval-2301.json), [mock](2026-10-11-ui-hidden-tab-heartbeat/1-pr34-mock-2301.log)) | `hidden` at 8.4 s; the tab came back to `visible` for 5.6 s, then `hidden` again until 39.4 s | 53 in 30.1 s (1.76 Hz) | 1032 ms | `mode SAFE (cause HEARTBEAT_LOST)` 2.2 s after READY, about 1 s after the tab was hidden | Fail, as the control must |
| 2 | #33 at d19c02f, Worker ([result](2026-10-11-ui-hidden-tab-heartbeat/2-pr33-d19c02f-worker-2302.json), [mock](2026-10-11-ui-hidden-tab-heartbeat/2-pr33-d19c02f-mock-2302.log)) | `hidden` 8.9 s to 40.0 s | 150 in 30.1 s (4.98 Hz) | 281 ms | stayed READY | Pass |
| 3 | #33 at 43f79e4, after its fixes and `main` ([result](2026-10-11-ui-hidden-tab-heartbeat/3-pr33-43f79e4-worker-0237.json), [mock](2026-10-11-ui-hidden-tab-heartbeat/3-pr33-43f79e4-mock-0237.log)) | `hidden` 4.2 s to 35.2 s | 150 in 30.0 s (5.00 Hz) | 200 ms | stayed READY | Pass |
| 4 | #33's fix for a Worker that fails to load: a stub Worker that fires `error` right after construction, in a vitest probe | not applicable | First second: **1 ping** at d19c02f; **7** at 43f79e4 (the first, one immediate from the fallback, then 5 Hz) | not applicable | not applicable | Fail at d19c02f, pass at 43f79e4 |

## Outcome

- Overall: **Pass for #33.**
  - On `main`'s page interval, a hidden operator tab misses the 1 s heartbeat, and an armed box drops to SAFE. That is a demo hazard.
  - #33's Worker keeps 5 Hz while hidden. Since 43f79e4 it also falls back to a page interval if the Worker fails to load.
- Faults: none beyond the hazard on `main`, which #33 removes when it merges.
- Raw data: linked in the table. The vitest probe in step 4 lives in the review on #33.
- Follow-up owner and action:
  - Merging #33 is Julian's call.
  - The same check with the recorder of #40 and #86, and on the real box in #44 Stage 4, is in [2026-10-11-ui-recorder-linux.md](2026-10-11-ui-recorder-linux.md) and #44.
