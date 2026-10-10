"""Battery holder end caps with printed spring contacts (exports cap A and cap B).

Each cell end touches the dome of an M3 x 8 button-head screw from the kit.
The screw sits in a 7.2 mm disc held by a printed spring: two C-shaped slots
leave a 1.2 mm ring joined to the disc on one side and to the cap on the
other. A cell pushes the disc out by CONTACT_PRELOAD (0.45 mm), which gives
about 2 N of contact force [C: guided-beam estimate for PETG; A: modulus
2 GPa]. Outside the cap, each screw takes a washer, a wire loop, a washer,
and a nut; the 7.8 mm skirt keeps that hardware recessed.

Wiring (see cell_centres() in stack.py): at cap B, join the two contacts in
each column. At cap A, join top row columns 1-2, bottom row columns 2-3, and
top row columns 3-4; the pack + is column 1 bottom and the pack - is column 4
bottom, leaving through the side notches. Use flexible stranded wire with a
little slack so the wire does not hold the spring.

Cap A has two feet that screw to the battery deck (M3 x 8 into a nut trap).
Cap B comes off with its three M2 screws to change cells.

Print orientation: inner face on the bed, skirt up. Material: PETG, 3 walls,
40% infill. Clear any first-layer flash from the slots with a knife; each
disc must spring back when pressed.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
T = HOLDER_CAP_T
FT = HOLDER_FLEX_T
SKIRT = HOLDER_SKIRT
SKIRT_WALL = 1.6
SLOT = 0.5 + HOLE_COMP           # prints about 0.5 mm
DISC_R = 3.6                     # holds an M3 nut (6.35 across corners) and an M3 washer
RING = 1.2
BRIDGE_DEG = 30.0                # width of the two joints, in degrees
R1 = DISC_R + SLOT               # inner slot outer radius
R2 = R1 + RING                   # ring outer radius
R3 = R2 + SLOT                   # outer slot outer radius (6.8)
M2_CLEAR = 2.4 + HOLE_COMP
LEAD_NOTCH = (3.0, 13.0)         # y range of the side notches for the pack leads


def c_slot(r_in, r_out, gap_deg, x, y, z0, z1):
    """Annulus with a gap of gap_deg centred on gap_angle 0 (+x) - rotated by the caller."""
    ann = cyl(2 * r_out, z0, z1).cut(cyl(2 * r_in, z0 - 1, z1 + 1))
    g = math.radians(gap_deg / 2)
    big = r_out * 2
    wedge = (cq.Workplane("XY").polyline([(0, 0), (big * math.cos(-g), big * math.sin(-g)),
                                          (big * math.cos(g), big * math.sin(g))]).close()
             .extrude(z1 - z0 + 2).translate((0, 0, z0 - 1)))
    return ann.cut(wedge)


def flexure_cut(x, y):
    """Slots and screw hole for one contact, plus the pocket that thins it to FT from outside."""
    inner = c_slot(DISC_R, R1, BRIDGE_DEG, 0, 0, -1, T + 1)                       # joint at 0 deg
    outer = c_slot(R2, R3, BRIDGE_DEG, 0, 0, -1, T + 1).rotate((0, 0, 0), (0, 0, 1), 180)  # joint at 180 deg
    pocket = cyl(2 * R3, FT, T + 1)
    hole = cyl(M3_CLEAR, -1, T + 1)
    return inner.union(outer).union(pocket).union(hole).translate((x, y, 0))


def build(feet):
    w, h = HOLDER_W, HOLDER_H
    cap = box_at(-w / 2, w / 2, 0, h, 0, T + SKIRT).edges("|Z").fillet(1.0)
    cap = cap.cut(box_at(-w / 2 + SKIRT_WALL, w / 2 - SKIRT_WALL, SKIRT_WALL, h - SKIRT_WALL, T, T + SKIRT + 1))
    for (x, y, _) in cell_centres():
        cap = cap.cut(flexure_cut(x, y))
    for x in HOLDER_CAP_SCREWS:
        cap = cap.cut(cyl(M2_CLEAR, -1, T + 1, x, h / 2))
    for s in (1, -1):
        cap = cap.cut(box_at(s * w / 2 - SKIRT_WALL - 1, s * w / 2 + 1, LEAD_NOTCH[0], LEAD_NOTCH[1], T, T + SKIRT + 1))
    if feet:
        z_end = T + SKIRT + HOLDER_TAB_L
        for s in (1, -1):
            xc = s * HOLDER_TAB_X
            tab = box_at(xc - HOLDER_TAB_W / 2, xc + HOLDER_TAB_W / 2, 0, HOLDER_TAB_T, T - 0.01, z_end)
            hole = (cq.Workplane("XZ").circle(M3_CLEAR / 2).extrude(-(HOLDER_TAB_T + 2))
                    .translate((xc, -1, T + SKIRT + HOLDER_TAB_L / 2)))
            cap = cap.union(tab.cut(hole))
    return cap


def foot_holes():
    """Foot screw centres in cap-local (x across, z outward)."""
    return [(s * HOLDER_TAB_X, HOLDER_CAP_T + HOLDER_SKIRT + HOLDER_TAB_L / 2) for s in (1, -1)]


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    export(build(feet=True), "battery_cap_a", here)
    export(build(feet=False), "battery_cap_b", here)
