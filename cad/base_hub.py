"""Base hub: holds the M8 bolt upright and supports the lower bearing's inner race.

The M8 x 60 bolt head sits in a hex trap underneath, so the bolt cannot turn.
The 11 mm pad on top touches only the lower 608's inner ring, which carries
the weight of the box. Screw the hub to a board with four wood screws, then
clamp the board to the bench and shim it level.

Print orientation: flange on the bed. Material: PETG, 4 walls, 50% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
FLANGE_D = 90.0
FLANGE_T = HUB_FLANGE_T
GUIDE_D = 26.0
PAD_H = 4.0                        # gap between the hub and the box underside
GUIDE_TOP = HUB_H - PAD_H
WOOD_SCREW_D = 4.5 + HOLE_COMP       # clearance for a 4 mm wood screw
WOOD_SCREW_CSK = 9.0
WOOD_SCREW_R = 36.0


def build():
    hub = cyl(FLANGE_D, 0, FLANGE_T).edges(">Z").chamfer(1.0)
    hub = hub.union(cyl(GUIDE_D, FLANGE_T - 0.01, GUIDE_TOP))
    hub = hub.edges(cq.selectors.NearestToPointSelector((GUIDE_D / 2 * 0.7071, GUIDE_D / 2 * 0.7071, FLANGE_T))).fillet(3.0)
    hub = hub.union(cyl(INNER_RACE_PAD_D, GUIDE_TOP - 0.01, HUB_H))
    hub = hub.cut(cyl(M8_CLEAR, -1, HUB_H + 1))
    hub = hub.cut(hex_trap_bed(M8_HEAD_AF, M8_CLEAR, M8_HEAD_H))
    for k in range(4):
        a = math.radians(45 + 90 * k)
        x, y = WOOD_SCREW_R * math.cos(a), WOOD_SCREW_R * math.sin(a)
        hub = hub.cut(cyl(WOOD_SCREW_D, -1, FLANGE_T + 1, x, y))
        csk = (cq.Workplane("XY").circle(WOOD_SCREW_D / 2).workplane(offset=(WOOD_SCREW_CSK - WOOD_SCREW_D) / 2)
               .circle(WOOD_SCREW_CSK / 2).loft().translate((x, y, FLANGE_T - (WOOD_SCREW_CSK - WOOD_SCREW_D) / 2 + 0.01)))
        hub = hub.cut(csk)
    # Blind hole for the spin-up lock pin, with a 1 mm lead-in chamfer
    hub = hub.cut(cyl(LOCK_HOLE_D, FLANGE_T - LOCK_HUB_DEPTH, FLANGE_T + 1, LOCK_R, 0))
    lead = (cq.Workplane("XY").circle(LOCK_HOLE_D / 2).workplane(offset=1.0).circle(LOCK_HOLE_D / 2 + 1.0)
            .loft().translate((LOCK_R, 0, FLANGE_T - 1.0 + 0.01)))
    hub = hub.cut(lead)
    return hub


if __name__ == "__main__":
    export(build(), "base_hub", os.path.dirname(os.path.abspath(__file__)))
