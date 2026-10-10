"""Spin-up lock pin: holds the box still while the wheel spins up to bias.

Spinning the wheel from rest to the 2400 rpm bias would turn a free box the
other way at about 2.7 rev/s. With the pin in, the box is pinned to the fixed
base hub and the reaction goes into the bench instead.

Use: turn the box until the hole in the bottom cap (on the +X side, 30 mm out)
lines up with the hole in the base hub flange, reach in through the open side
of the bottom bay, and drop the pin in. Arm and spin up. When the ground
station shows Ready, pull the pin out by its tab. The tab rests on the bottom
cap, so the pin cannot fall through.

Load: at the full 3 A motor torque (31 mN*m) the pin carries about 1 N of
shear at 30 mm radius, far below what a 6 mm PETG pin holds.

Print orientation: lying flat on its milled flat, tab and shaft on the bed, so
the layers run along the pin and bending does not split them. No supports.
Material: PETG, 4 walls, 40% infill.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
CAP_T = PLATE_T                         # bottom cap plate the pin passes through
HUB_GAP = HUB_H - HUB_FLANGE_T          # hub flange top to cap underside (z = 0)
SHAFT_L = CAP_T + HUB_GAP + LOCK_TIP_ENGAGE  # tab face to tip (33 mm with the 31 mm hub)
FLAT = 0.5                              # flat milled along the shaft so it sits on the bed
TIP_CHAMFER = 1.0
TAB_L = 22.0                            # pull tab, along the pin
TAB_W = 16.0
TAB_T = LOCK_PIN_D - FLAT               # tab as thick as the shaft's printed height
FINGER_D = 10.0


def build():
    r = LOCK_PIN_D / 2
    zc = r - FLAT                       # shaft axis height once the flat is on the bed
    shaft = (cq.Workplane("YZ").circle(r).extrude(SHAFT_L - TIP_CHAMFER)
             .faces(">X").workplane().circle(r).workplane(offset=TIP_CHAMFER).circle(r - TIP_CHAMFER).loft()
             .translate((0, 0, zc)))
    tab = box_at(-TAB_L, 0.01, -TAB_W / 2, TAB_W / 2, 0, TAB_T).edges("|Z").fillet(3.0)
    pin = shaft.union(tab)
    pin = pin.cut(box_at(-TAB_L - 1, SHAFT_L + 1, -TAB_W, TAB_W, -5, 0))       # the flat
    pin = pin.cut(cyl(FINGER_D, -1, TAB_T + 1, -TAB_L / 2 - 1, 0))
    return pin


def axis_offset():
    """Distance of the shaft axis above the bed, for placing the pin in the assembly."""
    return LOCK_PIN_D / 2 - FLAT


if __name__ == "__main__":
    export(build(), "lock_pin", os.path.dirname(os.path.abspath(__file__)))
