"""Hall bracket: holds the A1104 Hall switch beside the spinning bell.

The A1104 (SIP-3 "UA" package, about 4.1 x 3.0 x 1.6 mm) drops into a pocket
from the top, branded face toward the motor, leads sticking up. Nominal
geometry: magnet face at 16 mm radius, 1.5 mm air, 1 mm plastic wall, sensor
face at 18.5 mm, so 2.5 mm from magnet face to sensor face. The A1104 is a
unipolar switch: both bell magnets must face south-out. The slotted foot slides
radially +-4 mm, then an M3 x 12 screw and the nut trapped under the motor deck
lock it.

Modelled with the motor axis at the origin and the bracket on +X; the assembly
rotates it to HALL_ANGLE. Print orientation: foot on the bed, post up.
Material: PETG.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
MAGNET_T = 2.0            # small magnet glued to the bell
GAP = 2.5                 # magnet face to sensor face, nominal
SENSOR_R = BELL_D / 2 + MAGNET_T + GAP   # radial position of the sensor face
WALL = 1.0                # plastic between sensor and magnet
POCKET_W = 4.4 + HOLE_COMP  # A1104 body 4.06 wide + clearance
POCKET_D = 1.9 + HOLE_COMP  # body 1.57 thick + clearance
SENSOR_H = 3.0            # body height
SENSOR_CENTER_Z = 15.0    # above the deck top: mid-height of the bell
POST_T = 6.0
POST_W = 10.0
POST_H = 22.0             # stays below the washer flywheel
FOOT_T = 4.0
FOOT_END = 35.0
SLOT_RANGE = (24.0, 32.0)


def build():
    r0 = SENSOR_R - WALL
    post = box_at(r0, r0 + POST_T, -POST_W / 2, POST_W / 2, 0, POST_H)
    foot = box_at(r0, FOOT_END, -POST_W / 2, POST_W / 2, 0, FOOT_T)
    b = post.union(foot).edges("|Z").fillet(1.0)
    pocket_bottom = SENSOR_CENTER_Z - SENSOR_H / 2
    b = b.cut(box_at(SENSOR_R, SENSOR_R + POCKET_D, -POCKET_W / 2, POCKET_W / 2, pocket_bottom, POST_H + 1))
    slot = (cq.Workplane("XY").center((SLOT_RANGE[0] + SLOT_RANGE[1]) / 2, 0)
            .slot2D(SLOT_RANGE[1] - SLOT_RANGE[0] + M3_CLEAR, M3_CLEAR).extrude(FOOT_T + 2).translate((0, 0, -1)))
    return b.cut(slot)


if __name__ == "__main__":
    export(build(), "hall_bracket", os.path.dirname(os.path.abspath(__file__)))
