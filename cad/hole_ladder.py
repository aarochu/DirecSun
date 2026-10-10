"""Hole ladder: a short follow-up to hole_gauge for the two sizes it got wrong.

From -X to +X, top face up (notches on the -Y edge count 1 to 6):
  1-3. M2 pilots drawn 2.3, 2.5, 2.7 mm, blind with a 0.8 mm floor like the real
       standoffs. Pick the one an M2 x 8 screw threads into firmly with a
       screwdriver without splitting the boss; set M2_PILOT in stack.py to it.
  4-6. #8-32 rod holes drawn 4.6, 4.8, 5.0 mm, each with the lock-nut trap
       underneath and the new bridge layers over it. Pick the tightest the rod
       slides through; set ROD_D. The trap ceilings should print without strings.
The current stack.py values are 2.5 (M2_PILOT) and 4.8 (ROD_D).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

T = 6.0
PITCH = 14.0
M2_SIZES = (2.3, 2.5, 2.7)
ROD_SIZES = (4.6, 4.8, 5.0)
BOSS_D = ADA_STANDOFF_OD


def build():
    n = len(M2_SIZES) + len(ROD_SIZES)
    w = PITCH * n
    plate = box_at(-w / 2, w / 2, -8, 8, 0, 3.0).edges("|Z").fillet(2.0)
    xs = [-w / 2 + PITCH * (i + 0.5) for i in range(n)]
    for x, d in zip(xs[:3], M2_SIZES):
        plate = plate.union(cyl(BOSS_D, 2.99, T, x, 0))              # standoff-sized boss, as on the decks
        plate = plate.cut(cyl(d, M2_PILOT_FLOOR, T + 1, x, 0))
    for x, d in zip(xs[3:], ROD_SIZES):
        plate = plate.union(cyl(12.5, 2.99, T, x, 0))                # wide enough for the bridge slot
        plate = plate.cut(cyl(d, -1, T + 1, x, 0)).cut(hex_trap_bed(ROD_NUT_AF, d, ROD_NUT_H + 0.2, x, 0))
    for i, x in enumerate(xs):
        for k in range(i + 1):
            nx = x + (k - i / 2) * 2.0
            plate = plate.cut(box_at(nx - 0.5, nx + 0.5, -8.5, -7.0, -1, 4))
    return plate


if __name__ == "__main__":
    export(build(), "hole_ladder", os.path.dirname(os.path.abspath(__file__)))
