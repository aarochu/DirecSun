# Test: R3.5 bench kit against the firmware-parity mock box (PR #50, issue #41)

- Date/time and timezone: 2026-10-11, 02:41 to 03:05 (America/New_York).
- Operators and witnesses: Julian's Claude Code agent (PR-review session). No hardware.
- Julian's explicit go for any motor-turning step: Not applicable. Every server was a mock on loopback or the laptop's own `.local` name.
- Component: `tools/mock-box.py` as changed by #50. The change gives it the firmware's arm guards, its refusal reasons, a 5 s ramp-down window, and a spin-up that ramps at 400 rpm/s and needs 0.5 s within 5 % of the target.
- Branch/commit: #50 at ab15bcf, merged locally with `feat/set4/R4.3-hold-and-slew` at 8622d57. The control is 8622d57 without #50.
- Procedure and acceptance-criterion reference:
  - `tools/bench/r35/test/mock-suite.py`: 43 cases. Each falsifier must fail for its own reason.
  - `tools/check-all.sh`.
  - The parity claims, read against `firmware/components/direcsun_core/src/modes.cpp` on R4.3.
- Rig setup: this laptop (Linux, Chrome 154).
  - `R35_UI` points at `frontend/ground-station` built from `main` (a2ec2a2), as `prep.sh` requires. `prep.sh` passed.
  - `getent ahostsv4 <host>.local` resolves, which the t1 cases need.
- Instruments, calibration or positive-control check:
  - The suite's own falsifiers.
  - A control run of the t3 cases on the base without #50.
  - An instrument note: a first suite run at 02:39 failed every t1 case with no RESULT line, because `R35_UI` was unset (my set-up, not the PR). It was stopped and rerun.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result |
| --- | --- | --- | --- | --- |
| 1 | `mock-suite.py`, all 43 cases, on #50 ([summary](2026-10-11-r35-mock-suite-parity/1-suite-pr50-0241.txt)) | 42 of 43 behaved as expected. The two 10-minute positive runs passed: `t5-pass` (630 s) and `t7-pass` (607 s) | 43 of 43 | Fail: `t3-pass` |
| 2 | Why `t3-pass` failed ([case output](2026-10-11-r35-mock-suite-parity/1-t3-pass.txt), [mock log](2026-10-11-r35-mock-suite-parity/1-t3-pass-mock.log), [turner](2026-10-11-r35-mock-suite-parity/1-t3-turn-cmd.txt)) | `test/mock-turn.py` waits 5 s for READY. The mock took 6.4 s (SPINUP at 1105.49, READY at 1111.87), so the turner got `READY: False`, its `mode MANUAL` was refused `not in READY`, and its setpoints `not in MANUAL`. Net turn +13.2°, needs at least +30° | The kit's timing matches the mock | Fail, caused by #50 |
| 3 | `t3-f-ccw` in the same run | "Passed" (failed as expected) with net +12.4°, the same as `t3-f-no-turn`'s +13.0°. Nothing turned, so the falsifier no longer tests direction | Fails because the turn went counter-clockwise | Fail: the falsifier has become vacuous |
| 4 | Control: `--only t3` on the base without #50 ([output](2026-10-11-r35-mock-suite-parity/2-control-t3-base-0256.txt)) | 5 of 5. `t3-pass` PASS; `t3-f-ccw` fails with net −85.8°, "it went DOWN" | 5 of 5 | Pass: so steps 2 and 3 are caused by #50 |
| 5 | `--uncharacterised` with the Hall fitted ([probe](2026-10-11-r35-mock-suite-parity/uncharacterised_probe.py)) | READY at 6.36 s, the same as characterised. #50's docstring said the box "may never leave SPINUP" | Docstring matches behaviour | Fail (documentation) |
| 6 | `tools/check-all.sh` on #50 plus 8622d57 ([output](2026-10-11-r35-mock-suite-parity/3-check-all-pr50.txt)) | 9 of 9 PASS | 9 of 9 | Pass |
| 7 | `mock-box.py --selftest` | Passes | Passes | Pass |

## Outcome

- Overall: **Fail.** The parity itself matches `modes.cpp`, but the R3.5 kit's t3 timing breaks against it, and one falsifier became vacuous.
- Follow-up owner and action:
  - Aaron (review on #50): make the turner wait up to 12 s for READY and exit non-zero without it, widen the t3 sampling, and require the turn command to succeed.
  - #86 carries these fixes into R4.1's own tools (`mock-turn.py` waits 12 s, t3 samples 14 s), and #50 closes as superseded once #86 merges.
  - The suite should be rerun on #86 before it merges.
