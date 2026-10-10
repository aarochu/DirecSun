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
- [ ] R4.1 ui-command-contract: implement the commands the finished UI sends (arm, disarm, e-stop, rotate ±90 / stop / reset, heartbeat), matching `frontend/`'s client exactly; schema additions stay v1 if additive
- [~] R4.2 gyro-heading: heading from integrated gyro with `zero_heading` at sequence start; drift budget measured over 10 s
- [~] R4.3 hold-and-slew: attitude loop, HOLD capture, relative SLEW trapezoid at 60 deg/s and 120 deg/s² (HT-4), completion and timeout, the return leg
- [ ] R4.4 detumble-and-safety: the DETUMBLE wire mode for hand spins; T-12 and T-13 on the real box (ESC signal loss, every interlock)
- [ ] R4.5 characterisation-and-acceptance: T-2 to T-4, T-7, T-8 on the assembled box, `sizing.py` re-run (G2); T-15 and T-16 trials driven from the UI

## Open questions / decisions before implementing
1. ~~Who sequences rotate, hold, and return?~~ **Answered by the UI (Aaron's PR #10, 2026-10-10).** The UI sequences it with two `mode SLEW` commands, each carrying an absolute `psi_ref_deg` in the wire frame (start ± 90, then back to the start). It treats a slew as done when `mode` returns to READY or a `slew_complete` event arrives. It aborts on `SLEW_TIMEOUT`, a refusal, or SAFE. `mode READY` stops a slew and `disarm` cancels.

   No new command is needed. R4.1's scope becomes: confirm the firmware's events and timings match this flow, and run the UI against the real box with Aaron (integration). R4.3 and the mock box are being aligned with it.

2. Known requirements on the UI's client once it starts commanding, from R1.8's look at `frontend/`:
   - It must send `ping` at 5 Hz while it controls an armed box. Today it sends nothing, so the box would treat it as a passive viewer, and the heartbeat ramp-down would stop any motion it started.
   - It must match acks by `seq` and send `estop` without queueing it (CONTEXT.md).

   For Aaron, at 375 px the URL field truncates to `ws://127.0.0.1:80`, which reads like a port number.

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning (as "Detumble"), then replanned the same day around the main UI's judge sequence.
- 2026-10-10: R4.2 and R4.3 specs approved, including the guard amendment (HOLD and SLEW on gyro heading for relative use). They are built ahead, stacked on R3.9.
- 2026-10-10: Set branch `feat/set4-judge-sequence` created from `feat/set3-teleop` (stacked; rebased onto `main` once Set 3 lands). R4.2 and R4.3 started on feature branches stacked on R3.9.
