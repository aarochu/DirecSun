"""Hole gauge: print this before the decks to confirm HOLE_COMP in stack.py.

Every hole is drawn exactly as the real parts draw it, so a fit here is a fit
there. From -X to +X, top face up:
  1. #8-32 rod hole (ROD_D), with the lock-nut trap underneath (ROD_NUT_AF)
  2. M3 clearance hole with an M3 nut trap underneath
  3. M2 pilot, blind (an M2 x 8 screw should thread in firmly by hand)
  4. M8 clearance hole (the bolt should slide through)
  5. Lock pin hole (the printed pin should drop in)
  6. AA bore, 8 mm deep (a cell should slide in without force or wobble)
  7. One battery-cap spring contact (press the disc: it should move and spring back)
If any is tight, raise HOLE_COMP; if loose, lower it. Print in the material
and settings of the real parts.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *
import battery_holder_cap as bc

T = 4.0
PITCH = 19.0


def build():
    n = 7
    w = PITCH * n
    plate = box_at(-w / 2, w / 2, -11, 11, 0, T).edges("|Z").fillet(2.0)
    xs = [-w / 2 + PITCH * (i + 0.5) for i in range(n)]
    plate = plate.cut(cyl(ROD_D, -1, T + 1, xs[0], 0)).cut(hex_trap_bed(ROD_NUT_AF, ROD_D, 2.5, xs[0], 0))
    plate = plate.cut(cyl(M3_CLEAR, -1, T + 1, xs[1], 0)).cut(hex_trap_bed(M3_NUT_AF, M3_CLEAR, 2.6, xs[1], 0))
    plate = plate.cut(cyl(M2_PILOT, M2_PILOT_FLOOR, T + 1, xs[2], 0))
    plate = plate.cut(cyl(M8_CLEAR, -1, T + 1, xs[3], 0))
    plate = plate.cut(cyl(LOCK_HOLE_D, -1, T + 1, xs[4], 0))
    ring_ = cyl(CELL_BORE_D + 2.4, T - 0.01, T + 8, xs[5], 0).cut(cyl(CELL_BORE_D, -1, T + 9, xs[5], 0))
    plate = plate.cut(cyl(CELL_BORE_D, -1, T + 1, xs[5], 0)).union(ring_)
    # spring contact sample: same plate and spring thickness as the cap
    plate = plate.cut(box_at(xs[6] - 8.5, xs[6] + 8.5, -8.5, 8.5, HOLDER_CAP_T, T + 1))
    plate = plate.cut(bc.flexure_cut(xs[6], 0))
    for i, x in enumerate(xs):                        # i + 1 notches on the -Y edge, as in the fit test
        for k in range(i + 1):
            nx = x + (k - i / 2) * 2.2
            plate = plate.cut(box_at(nx - 0.6, nx + 0.6, -11.5, -10.0, -1, T + 1))
    return plate


if __name__ == "__main__":
    export(build(), "hole_gauge", os.path.dirname(os.path.abspath(__file__)))
