# Revision Set 5: Pointing (outline)

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
Outline only: the feature specs are written when Set 4 closes, because they depend on the measured plant and the magnetometer's behaviour near the spinning wheel.

**Demo at the end of this set:** HOLD keeps a heading against pokes (PR-3), SLEW turns 90 deg in under 3 s (PR-2), and SUN_POINT tracks a lamp. TD §11.4 drops these in reverse order if time runs out: sun point first, then slew, then hold.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R5.1 heading: magnetometer axis map and hard/soft-iron calibration (T-9 at three wheel speeds); complementary heading filter (FD §11.3); `zero_heading`
- [ ] R5.2 hold-and-slew: attitude loop, HOLD capture, SLEW trapezoid (HT-4), slew completion and timeout, T-16 and T-17
- [ ] R5.3 respin: lock-pin desaturation and alternate wheel start speeds (FD §13.4), with the console prompt flow
- [ ] R5.4 sun-point: OP550 calibration (T-10), sun angle, SUN_POINT and its invalid-sun exit

## Open questions / decisions before implementing
To be raised when the set is specced. Already known: a lamp at the venue (TD §12.3).

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning.
