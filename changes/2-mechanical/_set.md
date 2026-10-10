# Revision Set 2: Mechanical

Bootstrap: read `changes/CONTEXT.md` first for project invariants.
This file is the index and roll-up log for set 2. Per-feature specs live in the
sibling `R2.M-*` files; load only the feature(s) you are working on.

**Goal:** the printed parts the firmware sets need on real hardware. That means a flywheel that carries the Hall magnets, a Hall bracket that faces them, the parts held for measurements, and a safety analysis rewritten for a printed rotating part. This set never gates a firmware merge (decided 2026-10-10). Design runs in CadQuery through `tools/build-cad.sh`. Printing and measuring happen at the bench.

**Branch:** `feat/set2-mechanical`, stacked on `feat/set1-foundation` like Set 3, and rebased onto `main` once Set 1 lands.

**Order:**
1. R2.1 and R2.3 together: the flywheel geometry feeds the stress and energy numbers.
2. R2.2 after R2.1: the bracket faces the wheel's magnets.
3. R2.4 whenever its measurements arrive.

Every feature waits on a measurement in the open questions below.

## Status
<!-- markers: [ ] not started · [~] in progress · [t] tests passing, awaiting merge · [x] merged -->
- [ ] R2.1 flywheel: printed PETG wheel with the seven washers sealed in and two Hall magnets in pockets, plus a hub interface for the M6 shaft
- [ ] R2.2 hall-bracket: Hall bracket repositioned to face the wheel's magnets at the 2.5 mm gap
- [ ] R2.3 rotating-part-safety: TD invariant 8.3, the hazard table, and PR-4 rewritten for the printed wheel; sizing re-run with the real wheel
- [ ] R2.4 held-parts: motor_deck, power_deck, flywheel_guard, and avionics_deck regenerated from measurements; print plan updated
- [x] R2.5 ui-model-export: the CAD assembly exported as the main UI's `/assets/direcsun.glb` (after PR #1 is merged)

## Open questions / decisions before implementing
1. **Magnets (R2.1, R2.2):** diameter, thickness, count on hand, and how the south face is identified (a compass, or the A1104 on the breadboard: the south pole pulls its output low). Needed for the pocket size and the sensor gap. Recommendation: two magnets in diametrically opposite pockets on the wheel's underside, south face down, sealed in at the same print pause as the washers.
2. **Motor top (R2.1):** whether there is a raised collar around the shaft; whether the bell has screw holes, and if so their pattern and thread; the thread handedness; and whether the motor came with a prop nut, prop adapter, or self-tightening props. TD §4.5 lists these as the open S-15 measurements. Recommendation, depending on what exists: if the bell has screw holes, the wheel's hub screws to the bell, which is the strongest and best-centred option. If not, a printed hub threads onto the M6 shaft with one nut and thread-locker. Measure before choosing.
3. **Motor height (R2.4):** mounting face to the top of the spokes. Plates 4 and higher stay valid only if it is 37 mm or less (cad/README).
4. **Washer stack test result (R2.1):** did the washers press down with firm hand pressure, sit flat, and clear the nozzle on the lid layer? The cad/README asks for exactly this report.
5. **Perfboard (R2.4):** the actual width and thickness of the boards in hand (the guides assume 50 × 1.6 mm).

## DB changes in this set
None.

## Log
- 2026-10-10: Set scaffolded. Decisions from Julian: printed PETG flywheel with sealed washers (invariant 8.3 to be revised); CAD never gates firmware; magnets too large to attach to the bell as originally planned.
- 2026-10-10: R2.5 added after Aaron's frontend (PR #1) became the main UI. It falls back to a schematic box until `direcsun.glb` exists. Also noted for Aaron: the landing page's flywheel text (clamped washers, nothing printed spins) predates the printed-wheel decision, and should follow R2.1 and R2.3 once they land.
