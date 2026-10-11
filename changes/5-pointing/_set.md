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
- [t] R5.1 heading (host-tested, hardware pending): magnetometer axis map and hard- and soft-iron calibration (T-9 at three wheel speeds); complementary heading filter (FD §11.3); absolute HOLD (T-17)
- [t] R5.2 teleop-extras (host-tested, hardware pending): MANUAL `wheel` and `rate` inputs (FD §13.5), wired to whatever teleop controls the UI provides
- [t] R5.3 respin (host-tested, hardware pending): lock-pin desaturation and alternate wheel start speeds (FD §13.4), with the UI's prompt flow
- [t] R5.4 sun-point (host-tested, hardware pending): OP550 calibration (T-10), sun angle, SUN_POINT and its invalid-sun exit

## Open questions / decisions before implementing
To be raised when the set is specced. Already known: a lamp at the venue (TD §12.3).

## Contract note for Aaron (found on the integrated branch, 2026-10-11)
The UI's parameter panel (`frontend/ground-station/src/hardware/parameters.ts`, test `src/tests/parameters.test.ts`) lists 11 of the schema's keys and treats `mag_axis_map`, `mag_cal`, `sun_cal` and `light_threshold` as deferred. Against the schema as integrated here, `vitest` fails one test, "offers every non-deferred, non-bench schema parameter": the Set 3 keys `rate_gain_scale`, `spinup_throttle`, `hall_expect_throttle`, `hall_installed`, `bench_cap_rpm` and `bench_underspeed_rpm` are not offered, and R5.1 and R5.4 make the four calibration keys live. No firmware or schema change is needed; the panel should add the keys it wants and shrink `DEFERRED_PARAMETER_KEYS`. Everything else in `frontend/ground-station` passes typecheck and 112 of 113 unit tests. This is a `Contract:` issue for Aaron, not an edit from here.

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning, then replanned the same day after the main UI decision.
- 2026-10-11: R5.2 and R5.3 specced and built on the host (integrated branch claude/fervent-hypatia-gwbokr). Hardware items stay open for Julian's go.
- 2026-10-11: R5.4 specced and built on the host.
- 2026-10-11: R5.1 specced and built on the host. Every Set 5 feature is now built and host-tested; every hardware item is open for Julian's go.
