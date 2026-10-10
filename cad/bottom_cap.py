"""Bottom cap: the box's floor, carrying both 608 bearings in a central boss.

Print orientation: flat plate on the bed, boss and corner columns pointing up.
No supports. The lower bearing pocket opens on the bed face with a 0.5 mm
45 degree chamfer, so first-layer squish does not narrow its mouth; the flat
ledge above it is a 1.3 mm circular overhang, which prints cleanly.
Material: PETG, 4 walls, 40% infill (the boss carries the whole box).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS (shared values live in stack.py)
# ============================================================
T = PLATE_T
COLUMN_H = Z_MOTOR - (Z_BOTTOM + T)       # columns carry the motor deck
BOSS_FILLET = 3.0


def build():
    cap = deck_plate(T, COLUMN_H, nut_trap="bottom")
    boss = cyl(BOSS_OD, T - 0.01, BOSS_TOP_Z)
    cap = cap.union(boss)
    # Fillet where the boss meets the plate, for stiffness
    cap = cap.edges(cq.selectors.NearestToPointSelector((BOSS_OD / 2 * 0.7071, BOSS_OD / 2 * 0.7071, T))).fillet(BOSS_FILLET)
    # Bearing pockets and the ledge bore between them
    cap = cap.cut(cyl(BRG_POCKET_D, -1, LOWER_POCKET[1]))
    cap = cap.cut(cyl(BRG_LEDGE_D, LOWER_POCKET[1] - 0.01, UPPER_POCKET[0] + 0.01))
    cap = cap.cut(cyl(BRG_POCKET_D, UPPER_POCKET[0], BOSS_TOP_Z + 1))
    # Spin-up lock pin passes through the plate here, down into the base hub
    cap = cap.cut(cyl(LOCK_HOLE_D, -1, T + 1, LOCK_R, 0))
    # 45 deg chamfers where holes open on the bed face: the first layer squishes
    # outward (elephant foot) and would otherwise narrow the bearing pocket mouth
    cap = cap.cut(bed_chamfer(BRG_POCKET_D, BED_CHAMFER))
    cap = cap.cut(bed_chamfer(LOCK_HOLE_D, BED_CHAMFER, LOCK_R, 0))
    return cap


def bed_chamfer(d, c, x=0.0, y=0.0):
    """Cone cutter: diameter d + 2c at z = 0 narrowing to d at z = c (45 deg)."""
    return (cq.Workplane("XY").workplane(offset=-0.01).circle(d / 2 + c + 0.01)
            .workplane(offset=c + 0.01).circle(d / 2).loft().translate((x, y, 0)))


if __name__ == "__main__":
    export(build(), "bottom_cap", os.path.dirname(os.path.abspath(__file__)))
