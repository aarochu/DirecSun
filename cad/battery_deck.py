"""Battery deck: carries the printed 8 x AA holder.

The holder (battery_holder_body.py and battery_holder_cap.py) sits on the
plate between two low side rails. Cap A, at -X, has two feet; an M3 x 8
screw through each foot threads into a nut pressed into a trap on the
underside of this plate. The pack leads leave cap A through its side
notches and drop through the wire slots at the +/-Y edges to the power deck.
The bay above the plate is 35 mm; the holder is 32.7 mm tall.

Print orientation: plate on the bed. Material: PLA, 3 walls, 20% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *
import battery_holder_cap

# ============================================================
# PARAMETERS
# ============================================================
T = PLATE_T
COLUMN_H = Z_AVIONICS - (Z_BATTERY + T)
RAIL_CLEAR = 0.3                 # per side, holder to rail
RAIL_W, RAIL_H = 1.6, 4.0
M3_NUT_H = 2.6


def foot_points():
    """Cap A foot screw centres in deck coordinates (cap A is the cap B frame turned 180 deg about z)."""
    x_end = HOLDER_XC - HOLDER_BODY_L / 2
    return [(x_end - zc, -xc) for (xc, zc) in battery_holder_cap.foot_holes()]


def build():
    deck = deck_plate(T, COLUMN_H)
    x0, x1 = HOLDER_XC - HOLDER_BODY_L / 2, HOLDER_XC + HOLDER_BODY_L / 2
    yi = HOLDER_W / 2 + RAIL_CLEAR
    for s in (1, -1):
        y0, y1 = sorted((s * yi, s * (yi + RAIL_W)))
        deck = deck.union(box_at(x0, x1, y0, y1, T - 0.01, T + RAIL_H))
    for (x, y) in foot_points():
        deck = deck.cut(cyl(M3_CLEAR, -1, T + 1, x, y))
        deck = deck.cut(hex_trap_bed(M3_NUT_AF, M3_CLEAR, M3_NUT_H, x, y))
    deck = deck.cut(wire_slots(T))
    return deck


if __name__ == "__main__":
    export(build(), "battery_deck", os.path.dirname(os.path.abspath(__file__)))
