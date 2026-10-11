# Revision Set 5: Pointing and teleop extras (outline)

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
Outline only. Feature specs are written when Set 4 closes, because they depend on the measured plant and on how the magnetometer behaves near the spinning wheel.

**Demo at the end of this set:**
- HOLD keeps an absolute heading against pokes (PR-3), using magnetometer-corrected heading.
- SUN_POINT tracks a lamp.
- Manual rate and wheel teleop are available when the UI offers them.

TD §11.4 drops these in reverse order if time runs out.

**Replan 2026-10-10:** relative slews and the judge sequence moved to Set 4. The MANUAL `wheel` and `rate` inputs moved here from Set 3.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R5.1 heading: magnetometer axis map and hard- and soft-iron calibration (T-9 at three wheel speeds); complementary heading filter (FD §11.3); absolute HOLD (T-17)
- [t] R5.2 teleop-extras (host-tested, hardware pending): MANUAL `wheel` and `rate` inputs (FD §13.5), wired to whatever teleop controls the UI provides
- [t] R5.3 respin (host-tested, hardware pending): lock-pin desaturation and alternate wheel start speeds (FD §13.4), with the UI's prompt flow
- [t] R5.4 sun-point (host-tested, hardware pending): OP550 calibration (T-10), sun angle, SUN_POINT and its invalid-sun exit

## Open questions / decisions before implementing
To be raised when the set is specced. Already known: a lamp at the venue (TD §12.3).

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning, then replanned the same day after the main UI decision.
- 2026-10-11: R5.2 and R5.3 specced and built on the host (integrated branch claude/fervent-hypatia-gwbokr). Hardware items stay open for Julian's go.
- 2026-10-11: R5.4 specced and built on the host.
