# Revision Set 4: Detumble (outline)

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
Outline only: the feature specs are written when Set 3 closes, because they depend on its measurements (wheel gains, `ff_table`, s_w, friction, the real flywheel from Set 2).

**Demo at the end of this set:** a judge spins the box by hand; the operator selects DETUMBLE and the box comes to rest within 5 s (PR-1).

Most of the machinery arrives in Set 3: the rate loop, the closed-loop host simulation (HT-5 and HT-6), and the interlock table. This set turns it into the named demo mode and proves it on the assembled box.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R4.1 detumble-mode: the DETUMBLE wire mode (FD §13.2: the rate loop at ω_cmd = 0, logged separately for PR-1) and its console button
- [ ] R4.2 characterisation: T-2, T-3, T-4, T-7, T-8 on the assembled box (friction, I_b, imbalance, gyro noise); `sizing.py` re-run; gains recomputed (gate G2)
- [ ] R4.3 interlock-proof: T-12 (ESC behaviour on signal loss) and T-13 (every interlock on the real box) with event logs (gate G3 prerequisite)
- [ ] R4.4 pr1-acceptance: T-15, ten hand-spin trials, plus T-14 endurance

## Open questions / decisions before implementing
To be raised when the set is specced.

## DB changes in this set
None.

## Log
- 2026-10-10: Outline created during Set 3 planning.
