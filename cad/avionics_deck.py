"""Avionics deck: Adafruit MPU-6050 on the spin axis, XIAO carrier, sun sensor.

The Adafruit MPU-6050 (#3886, 25.4 x 17.78 mm, chip at the board centre) sits on
four printed standoffs centred on the spin axis, so the gyro is on the axis. It
is held by four M2 x 8 button-head screws threaded straight into printed pilot
holes (M2_PILOT in stack.py), so it can be removed from above; its STEMMA QT connectors face +-X.

The XIAO ESP32-S3 rides on a 50 x 35 mm carrier cut from a 5 x 7 cm perfboard,
which also holds the Hall pull-up, the battery divider, and the OP550 load
resistors. The carrier slides into short card guides from -Y; put the XIAO at
the -Y edge with USB-C facing out so it can be flashed in place.

The sun sensor block on the +X edge holds two OP550 side-looking
phototransistors (5.72 x 4.44 x 1.57 mm body) facing +-45 degrees from +X.
Each drops into its pocket from the top, leads up; the lens sits in a window
in the 1 mm front wall. The nose between the faces shades each sensor from the
other side, so the difference of the two signals gives the sun angle.

Print orientation: plate on the bed. The sensor pockets are vertical slots,
no supports. Material: black PLA for the sensor block, 3 walls, 20% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
T = PLATE_T
COLUMN_H = Z_TOP - (Z_AVIONICS + T)
BLOCK_H = 12.0
BLOCK_X0 = 33.0                    # back of the sensor block
NOSE_BASE_X = 43.5                 # where the +-45 degree faces start
NOSE_HALF = 6.0                    # half-width where the faces start
NOSE_TIP_X = NOSE_BASE_X + NOSE_HALF   # 49.5, just inside the 2U envelope
BLOCK_HALF_W = 10.0
PT_W = 4.44 + 0.3 + HOLE_COMP      # OP550 body width + clearance
PT_T = 1.57 + 0.25 + HOLE_COMP     # body thickness + clearance
PT_LEN = 5.72                      # body length; leads exit the top
FRONT_WALL = 1.0
WINDOW_W = 2.6
WINDOW_H = 3.6                     # lens sits near the far (bottom) end of the body
POCKET_BOTTOM = BLOCK_H - PT_LEN - 0.4
CARRIER_Y0 = -48.0                 # carrier board's outer (-Y) edge; it stops 2 mm short of the MPU board


def sensor_cuts():
    cuts = None
    for sgn in (1, -1):
        ang = sgn * 45.0
        nx, ny = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        mx, my = (NOSE_BASE_X + NOSE_TIP_X) / 2, sgn * NOSE_HALF / 2      # face midpoint
        def placed(depth_from_face0, depth_from_face1, width, z0, z1):
            d0, d1 = depth_from_face0, depth_from_face1
            blk = box_at(-d1, -d0, -width / 2, width / 2, T + z0, T + z1)  # local: +x = outward normal
            return blk.rotate((0, 0, 0), (0, 0, 1), ang).translate((mx, my, 0))
        pocket = placed(FRONT_WALL, FRONT_WALL + PT_T, PT_W, POCKET_BOTTOM, BLOCK_H + 1)
        window = placed(-0.5, FRONT_WALL + 0.01, WINDOW_W, POCKET_BOTTOM + 0.3, POCKET_BOTTOM + 0.3 + WINDOW_H)
        c = pocket.union(window)
        cuts = c if cuts is None else cuts.union(c)
    return cuts


def sun_block():
    pts = [(BLOCK_X0, -BLOCK_HALF_W), (NOSE_BASE_X, -BLOCK_HALF_W), (NOSE_BASE_X, -NOSE_HALF),
           (NOSE_TIP_X, 0), (NOSE_BASE_X, NOSE_HALF), (NOSE_BASE_X, BLOCK_HALF_W), (BLOCK_X0, BLOCK_HALF_W)]
    blk = cq.Workplane("XY").polyline(pts).close().extrude(BLOCK_H + 0.01).translate((0, 0, T - 0.01))
    return blk


def build():
    deck = deck_plate(T, COLUMN_H)
    deck = deck.union(card_guides(0.0, T, y0=CARRIER_Y0, y1=CARRIER_Y0 + CARRIER_L + 2.0))
    deck = deck.union(adafruit_standoffs(0.0, 0.0, T))
    deck = deck.union(sun_block())
    deck = deck.cut(sensor_cuts())
    deck = deck.cut(adafruit_standoff_cuts(0.0, 0.0, T))
    deck = deck.cut(wire_slots(T, sides=("+Y", "-Y", "-X")))
    return deck


if __name__ == "__main__":
    export(build(), "avionics_deck", os.path.dirname(os.path.abspath(__file__)))
