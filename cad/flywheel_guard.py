"""Flywheel guard: slotted ring around the motor and washer flywheel.

With the baseline 2.5 in washers the rim moves at about 8 m/s at the 2400 rpm
bias and 13 m/s at the 4000 rpm cap (docs/sizing.py), and the box sides are open. This ring keeps fingers out and
catches a washer if the nut ever backs off. The 5 mm slots let judges see the
wheel spin and let the Hall sensor leads out.

The ring sits in a 1 mm groove in the motor deck and is clamped from above by
the power deck plate, so it needs no screws. It stops 0.5 mm short of the
power deck so the corner columns, not the ring, carry the rod clamp.
The notch at the bottom lets the motor leads out toward the ESC lead slot.

Print orientation: upright, no supports (each slot top is a 5 mm bridge).
Material: PETG, 3 walls, 30% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
OD = GUARD_ID + 2 * GUARD_WALL
H = Z_POWER - (Z_MOTOR + MOTOR_PLATE_T) + GUARD_GROOVE_DEPTH - GUARD_TOP_GAP
SLOT_W = 5.0                    # keeps fingers out
SLOT_Z = (10.0, H - 6.0)
SLOT_COUNT = 15
LEAD_NOTCH_ANGLE = -38.0        # toward the ESC lead slot in the power deck
LEAD_NOTCH_W = 14.0
LEAD_NOTCH_H = 9.0


def radial_cut(angle_deg, width, z0, z1):
    blk = box_at(GUARD_ID / 2 - 2, OD / 2 + 2, -width / 2, width / 2, z0, z1)
    return blk.rotate((0, 0, 0), (0, 0, 1), angle_deg)


def build():
    ring = cyl(OD, 0, H).cut(cyl(GUARD_ID, -1, H + 1))
    ring = ring.cut(radial_cut(LEAD_NOTCH_ANGLE, LEAD_NOTCH_W, -1, LEAD_NOTCH_H))
    for k in range(SLOT_COUNT):
        a = LEAD_NOTCH_ANGLE + 360.0 * (k + 0.5) / SLOT_COUNT
        ring = ring.cut(radial_cut(a, SLOT_W, SLOT_Z[0], SLOT_Z[1]))
    return ring


if __name__ == "__main__":
    export(build(), "flywheel_guard", os.path.dirname(os.path.abspath(__file__)))
