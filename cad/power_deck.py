"""Power deck: ESC, 5 V buck, fuse, and power wiring.

A 5 x 7 cm perfboard (buck, fuse holder, connectors) slides into card guides
offset toward -X. The ESC lies along Y on the +X side, held by two zip ties
through two slot pairs, which tolerates ESC sizes from about 28 x 13 to
36 x 18 mm. The motor leads come up through the slot at the ESC's -Y end.

Print orientation: plate on the bed, columns and rails up. The card-guide slot
ceilings are 2 mm overhangs, which print without supports.
Material: PLA or PETG, 3 walls, 20% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
T = PLATE_T
COLUMN_H = Z_BATTERY - (Z_POWER + T)
BOARD_XC = -17.0                     # perfboard centre, shifted to free the +X side
ESC_X = (25.0, 43.0)                 # ESC footprint across X
ESC_Y = (-18.0, 18.0)                # ESC footprint along Y
ZIP_SLOT = (2.5, 5.5)                # (x, y) zip tie slot
ZIP_Y = (-9.0, 9.0)                  # two ties along the ESC
ZIP_MARGIN = 2.5                     # slot centre outside the ESC edge
LEAD_SLOT = (ESC_X[0] + 1, ESC_X[1] - 1, -30.0, -23.0)


def build():
    deck = deck_plate(T, COLUMN_H)
    deck = deck.union(card_guides(BOARD_XC, T))
    for y in ZIP_Y:
        for x in (ESC_X[0] - ZIP_MARGIN, ESC_X[1] + ZIP_MARGIN):
            deck = deck.cut(box_at(x - ZIP_SLOT[0] / 2, x + ZIP_SLOT[0] / 2,
                                   y - ZIP_SLOT[1] / 2, y + ZIP_SLOT[1] / 2, -1, T + 1))
    x0, x1, y0, y1 = LEAD_SLOT
    deck = deck.cut(box_at(x0, x1, y0, y1, -1, T + 1))
    deck = deck.cut(wire_slots(T))
    return deck


if __name__ == "__main__":
    export(build(), "power_deck", os.path.dirname(os.path.abspath(__file__)))
