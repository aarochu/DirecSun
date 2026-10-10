"""Motor deck: the 2212 motor mounts on top, screwed from below.

Print orientation: plate on the bed, columns up. The motor screw counterbores
and the Hall bracket nut trap open on the bed face, so no supports are needed.
Material: PETG (the motor runs warm), 4 walls, 40% infill.
Screws: four M3 x 6 into the motor. Longer screws can reach the windings.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
T = MOTOR_PLATE_T
COLUMN_H = Z_POWER - (Z_MOTOR + T)


def hall_screw_xy():
    a = math.radians(HALL_ANGLE)
    return HALL_SCREW_R * math.cos(a), HALL_SCREW_R * math.sin(a)


def build():
    deck = deck_plate(T, COLUMN_H)
    deck = deck.cut(cyl(MOTOR_CENTER_HOLE, -1, T + 1))
    for (x, y) in MOTOR_HOLES:
        deck = deck.cut(cyl(M3_CLEAR, -1, T + 1, x, y))
        deck = deck.cut(cyl(MOTOR_CBORE_D, -0.01, MOTOR_CBORE_DEPTH, x, y))
        deck = deck.cut(trap_bridge(M3_CLEAR, MOTOR_CBORE_D, MOTOR_CBORE_DEPTH, x, y))
    hx, hy = hall_screw_xy()
    deck = deck.cut(cyl(M3_CLEAR, -1, T + 1, hx, hy))
    deck = deck.cut(hex_trap_bed(M3_NUT_AF, M3_CLEAR, M3_NUT_H, hx, hy))
    deck = deck.cut(wire_slots(T))
    # Locating groove for the flywheel guard ring
    g_od = GUARD_ID + 2 * GUARD_WALL + 2 * GUARD_GROOVE_CLEAR
    g_id = GUARD_ID - 2 * GUARD_GROOVE_CLEAR
    deck = deck.cut(cyl(g_od, T - GUARD_GROOVE_DEPTH, T + 1).cut(cyl(g_id, T - GUARD_GROOVE_DEPTH - 1, T + 2)))
    return deck


if __name__ == "__main__":
    export(build(), "motor_deck", os.path.dirname(os.path.abspath(__file__)))
