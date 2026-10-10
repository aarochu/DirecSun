"""Top deck: closes the stack and carries the Adafruit QMC5883P and BMP280.

Both boards (25.4 x 17.78 mm, Adafruit #6388 and #2651 STEMMA QT) sit on printed
standoffs: the QMC5883P on -Y, the BMP280 on +Y. The deck prints plate-down with
the standoffs up and is installed FLIPPED (rotated 180 degrees about Y), so the
boards hang under the plate inside the 227 mm 2U envelope, chips facing down.
Firmware must account for the magnetometer's flipped Z axis. Each board is held
by four M2 x 8 screws threaded into blind printed pilot holes (M2_PILOT). The
#8-32 lock-nut traps are on the bed face, which becomes the outer top face. Steel screws move
with the magnetometer, so hard- and soft-iron calibration (test T-9) absorbs
them; nylon or brass M2 screws on the QMC5883P are an optional improvement.

Print orientation: plate on the bed, rails up. Material: PLA.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

T = PLATE_T


QMC_Y = -14.0                      # board centres; 10 mm gap between the boards
BMP_Y = 14.0


def build():
    deck = deck_plate(T, 0.0, nut_trap="bottom", trap_depth=ROD_TRAP_DEPTH_TOP)
    for y in (QMC_Y, BMP_Y):
        deck = deck.union(adafruit_standoffs(0.0, y, T))
    for y in (QMC_Y, BMP_Y):
        deck = deck.cut(adafruit_standoff_cuts(0.0, y, T))
    deck = deck.cut(wire_slots(T))
    return deck


if __name__ == "__main__":
    export(build(), "top_deck", os.path.dirname(os.path.abspath(__file__)))
