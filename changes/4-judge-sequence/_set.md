# Revision Set 4: Judge sequence (outline)

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
R4.2 and R4.3 are fully specced, because they depend on neither the UI's commands nor the hardware measurements, so they can be built ahead. R4.1 waits for the UI's command contract. R4.4 and R4.5 are specced when Set 3 closes, because they need its measured gains, `ff_table`, s_w, and friction. The backend matches the UI (CONTEXT.md, 2026-10-10).

**Demo at the end of this set:** the UI's own judge sequence runs on the real box:
1. From `frontend/`, the operator arms the box and the wheel spins up.
2. "Rotate right 90°" or "Rotate left 90°": the box slews 90°, holds for 1 s, and returns to its start heading.
3. Stop and e-stop work from the UI at every point.

A judge spinning the box by hand in READY sees it come to rest (DETUMBLE behaviour, PR-1).

**Replan 2026-10-10:** this set replaces the earlier "Detumble" outline. The UI's judge sequence is the main demo. Over a 3 to 5 s sequence, gyro-integrated heading is accurate enough, so magnetometer heading moves to Set 5.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [~] R4.1 ui-command-contract: implement the commands the finished UI sends (arm, disarm, e-stop, rotate ±90 / stop / reset, heartbeat), matching `frontend/`'s client exactly; schema additions stay v1 if additive
- [~] R4.2 gyro-heading: heading from integrated gyro with `zero_heading` at sequence start; drift budget measured over 10 s
- [~] R4.3 hold-and-slew: attitude loop, HOLD capture, relative SLEW trapezoid at 60 deg/s and 120 deg/s² (HT-4), completion and timeout, the return leg
- [~] R4.4 detumble-and-safety: the DETUMBLE wire mode for hand spins; T-12 and T-13 on the real box (ESC signal loss, every interlock)
- [~] R4.6 status-flags-and-refusals: the flags `FF_TABLE_MISSING`, `RATE_GAIN_REDUCED`, `PARAMS_RESET` and the refusal strings for DETUMBLE and SUN_POINT (issues 62 and 59; spec approved with changes by Julian, 2026-10-11)
- [~] R4.7 accel-rms: send `accel_rms_g` in bench telemetry for T-7 (issue 66, issue 80 P1.3; spec approved with changes by Julian, 2026-10-11)
- [~] R4.5 characterisation-and-acceptance: T-2 to T-4, T-7, T-8 on the assembled box, `sizing.py` re-run (G2); T-15 and T-16 trials driven from the UI

## Open questions / decisions before implementing
1. ~~Who sequences rotate, hold, and return?~~ **Answered by R4.1** from the UI's source: the UI does (frontend PR #13), with `zero_heading`, then `mode SLEW` to ±90, a 1 s hold in READY, then `mode SLEW` to 0. No new command; FD §17 records the flow.
2. ~~The UI's client once it commands.~~ **Answered by R4.1**: it pings every 200 ms from its first command, matches acks by `seq` within 1 s, and sends `estop` at once (FD §17).

   For Aaron, at 375 px the URL field truncates to `ws://127.0.0.1:80`, which reads like a port number.

3. **A host-built core box and a mock parity check** (R4.1 open question 1). Recommendation: yes, as a feature after the demo. Not decided.
4. **Flags for demo preconditions the UI cannot see** (R4.1 open question 2): `FF_TABLE_MISSING` and `RATE_GAIN_REDUCED`, additive v1. Recommendation: yes. Not decided; until then the pre-demo checklist checks `wheel_ref_rpm` > 0 in READY and re-sends `rate_gain_scale` 1.

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning (as "Detumble"), then replanned the same day around the main UI's judge sequence.
- 2026-10-10: R4.2 and R4.3 specs approved, including the guard amendment (HOLD and SLEW on gyro heading for relative use). They are built ahead, stacked on R3.9.
- 2026-10-10: Set branch `feat/set4-judge-sequence` created from `feat/set3-teleop` (stacked; rebased onto `main` once Set 3 lands). R4.2 and R4.3 started on feature branches stacked on R3.9.
- 2026-10-11: R4.4 (detumble and interlock verification, H1 to H11, T-12, T-15) and R4.5 specs approved with changes by Julian (decision record on PR 86) and ported from PR 83 onto the R4.1 stack, with `tools/check-trials.py`, the extra R4.4 host tests and the R4.5 robustness test. R4.5 started; both stay `[~]` for their hardware items.
