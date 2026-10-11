# Test: merging the approved ground station PRs together (#33, #46, #82, #69, #70, #74)

- Date/time and timezone: 2026-10-11, 04:15 to 05:05 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. Scripted fake boxes in Playwright and vitest only.
- Component: `frontend/ground-station`, with the approved PRs merged in order onto `main` (195fb8c), each resolved and tested on its own branch:

  | PR | Head |
  | --- | --- |
  | #33 | d8c72f4 |
  | #46 | 19dbb2e |
  | #82 | 2a32e38 |
  | #69 | ffb0a26, Aaron's head over the stack |
  | #70 | a696f01 |
  | #74 | 441d078, docs |

- Procedure and acceptance-criterion reference: each PR's own checks (typecheck, vitest, Playwright e2e) on the merged tree, and the whole stack test-merged onto `main`.
- Rig setup: Linux laptop, Chrome 154, Playwright 1.64, headless. The load average was 4 to 10 from other work on the machine.
- Instruments, calibration or positive-control check:
  - Every fix below came with a test that fails without it and passes with it.
  - For the load-sensitive failures, the same test was run on `main` as the control.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | Merge `main` into #33 | Text conflict in `deviceControl.test.tsx` (kept all three tests). A **semantic conflict broke the typecheck**: `DeviceControls.tsx` used `telemetry`, which #33 had removed. Fixed by keeping both subscriptions | Clean | Fixed in d8c72f4 |
| 2 | #33 merged: typecheck, vitest, e2e ([e2e](2026-10-11-ui-stack-integration/e2e-pr33-merged.txt)) | Typecheck passes; 122 of 122; e2e 36 passed and 1 failed (`MOCK_ONLY`); CI green on all four jobs | All pass | Pass. `MOCK_ONLY` also fails on `main` here, step 9 |
| 3 | #46 onto #33 | Conflict in the guidance line, resolved so the near-limit warning keeps priority; 125 of 125; box-control e2e 6 of 6 | All pass | Fixed in 19dbb2e |
| 4 | #82 onto #46 ([before](2026-10-11-ui-stack-integration/unit-stack-33-46-82-before-fix.txt), [after](2026-10-11-ui-stack-integration/unit-pr82-stacked-after-fix.txt)) | Merged cleanly, but #46's test "disables Rotate and explains an unavailable wheel loop" failed (126 of 127). The wheel reference rising above 0 changed no mode, flag or arm guard, so the coalesced redraw waited and Rotate stayed disabled | 127 of 127 | **Fixed in 2a32e38**: turn readiness redraws at once. 127 of 127 after; the same tree without the line fails |
| 5 | #69 onto #82 | Conflicts in `DeviceLink.receive()` and its test. Kept #82's coalesced render, with #69's drop counters in its state update; 129 of 129; box-control 6 of 6 | All pass | Fixed in 58b928d |
| 6 | #70 onto #69, full e2e ([before](2026-10-11-ui-stack-integration/e2e-pr70-stacked-before-fix.txt)) | "SAFE parameters send only changed values" failed 2 of 2. Playwright: the plots panel's `rail__header` "intercepts pointer events" on "Apply changed parameters" ([probe](2026-10-11-ui-stack-integration/1-probe-click-intercepted.txt)). The screenshot shows the side column spilling under the plots panel at 1280×720 ([before](2026-10-11-ui-stack-integration/1-before-fix-1280x720.png)) | Apply is clickable | **Fail: a real layout bug from #70 meeting #72** |
| 7 | The fix: the side column scrolls inside its grid area on desktop ([after](2026-10-11-ui-stack-integration/e2e-pr70-params-after-fix.txt)) | The parameters test passes 2 of 2; the 12 responsive smoke tests pass; 133 of 133 unit. Screenshots at 1440×900: [panel open](2026-10-11-ui-stack-integration/2-after-fix-params-open-1440x900.png), Apply reachable, nothing overlapping; [panel closed](2026-10-11-ui-stack-integration/3-after-fix-params-closed-1440x900.png), the first screen unchanged | Apply clickable, first screen unchanged | Pass, d24de12 |
| 8 | The whole stack plus #74, test-merged onto `main` | Clean; the top of the stack contains every PR in it | Clean | Pass |
| 9 | Control: `MOCK_ONLY` alone on `main` (195fb8c), twice, load 10 ([1](2026-10-11-ui-stack-integration/control-main-mockonly-1.txt), [2](2026-10-11-ui-stack-integration/control-main-mockonly-2.txt)) | Failed 2 of 2: the 30 s test timeout while stepping the simulation clock | Pass | Fail on `main` too: load on this laptop, not the PRs. CI passes it |

## Outcome

- Overall: **Pass.**
  - Two real defects were found by merging the PRs together and fixed: turn readiness under the coalesced render, and the parameters panel under the plots panel.
  - Two failures depend on load, not code: `MOCK_ONLY`, and #82's backpressure bound ([display lag](2026-10-11-ui-display-lag.md)).
- Follow-up owner and action:
  - Julian merges in order: #33, #46 (retargeted to `main`), #82, #69, #70; #74 at any point. Agents can't merge without approval in this session's mode.
  - Aaron may want a larger bound on the backpressure spec.
