"""Bearing fit test: print this first to choose BRG_POCKET_D in stack.py.

Three 7 mm deep pockets at 22.0, 22.2 and 22.4 mm, marked with one, two and
three notches. Press a 608 into each. Pick the one that holds the bearing
firmly without cracking, and set BRG_POCKET_D to it before printing the
bottom cap. Print in the same material and settings as the bottom cap.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

SIZES = [22.0, 22.2, 22.4]
PITCH = 28.0
T = BRG_W + 1.5                    # 1.5 mm floor with the same ledge bore as the cap (BRG_LEDGE_D)
NOTCH = 1.6


def build():
    w = PITCH * len(SIZES)
    plate = box_at(-w / 2, w / 2, -15, 15, 0, T).edges("|Z").fillet(2.0)
    for i, d in enumerate(SIZES):
        x = -w / 2 + PITCH * (i + 0.5)
        plate = plate.cut(cyl(BRG_LEDGE_D, -1, T + 1, x, 0))
        plate = plate.cut(cyl(d, T - BRG_W, T + 1, x, 0))
        for k in range(i + 1):
            nx = x + (k - i / 2) * 3.0
            plate = plate.cut(box_at(nx - NOTCH / 2, nx + NOTCH / 2, -15.5, -13.5, -1, T + 1))
    return plate


if __name__ == "__main__":
    export(build(), "bearing_fit_test", os.path.dirname(os.path.abspath(__file__)))
