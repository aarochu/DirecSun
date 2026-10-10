# Revision Set 4: Judge sequence (outline)

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
Outline only. Feature specs are written when Set 3 closes, because they need its measured gains, `ff_table`, s_w, and friction, and when the main UI (`frontend/`) defines the commands it will send. The backend matches the UI (CONTEXT.md, 2026-10-10).

**Demo at the end of this set:** the UI's own judge sequence runs on the real box:
1. From `frontend/`, the operator arms the box and the wheel spins up.
2. "Rotate right 90°" or "Rotate left 90°": the box slews 90°, holds for 1 s, and returns to its start heading.
3. Stop and e-stop work from the UI at every point.

A judge spinning the box by hand in READY sees it come to rest (DETUMBLE behaviour, PR-1).

**Replan 2026-10-10:** this set replaces the earlier "Detumble" outline. The UI's judge sequence is the main demo. Over a 3 to 5 s sequence, gyro-integrated heading is accurate enough, so magnetometer heading moves to Set 5.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R4.1 ui-command-contract: implement the commands the finished UI sends (arm, disarm, e-stop, rotate ±90 / stop / reset, heartbeat), matching `frontend/`'s client exactly; schema additions stay v1 if additive
- [ ] R4.2 gyro-heading: heading from integrated gyro with `zero_heading` at sequence start; drift budget measured over 10 s
- [ ] R4.3 hold-and-slew: attitude loop, HOLD capture, relative SLEW trapezoid at 60 deg/s and 120 deg/s² (HT-4), completion and timeout, the return leg
- [ ] R4.4 detumble-and-safety: the DETUMBLE wire mode for hand spins; T-12 and T-13 on the real box (ESC signal loss, every interlock)
- [ ] R4.5 characterisation-and-acceptance: T-2 to T-4, T-7, T-8 on the assembled box, `sizing.py` re-run (G2); T-15 and T-16 trials driven from the UI

## Open questions / decisions before implementing
1. Who sequences rotate, hold, and return: the UI, sending SLEW commands (FD §17's ground-orchestrated pair), or the firmware (a single "judge" command)? Decided by whatever the finished UI sends, per the backend-matches-UI rule.

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning (as "Detumble"), then replanned the same day around the main UI's judge sequence.
