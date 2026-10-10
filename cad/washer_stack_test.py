"""Washer stack test: the flywheel's washer pocket on its own, to rehearse the print pause.

The final flywheel seals the seven on-hand washers (37.88 mm across; four with
a 10.59 mm hole and 1.44 mm thick, three with an 8.93 mm hole and 1.27 mm thick;
the stack measures 9.91 mm) inside the print. This test has the same pocket,
post, and pause height, and a thin lid that can be cut off to get the washers
back.

The post is stepped: the four 10.6 mm-hole washers go on first, then the
three 8.9 mm-hole washers. Each section carries six crush ribs slightly larger than the
washer hole as printed. Pressing a washer down shaves the ribs to fit, which centres the
washer whatever size the printer makes the post, and the ribs grip it
against turning.

Use: slice at 0.2 mm layers with Z hop on travel. Add a pause in Orca on the
first layer above PAUSE_Z: with the K1C profile that is layer 62 of 64, shown
as 12.32 mm on Orca's layer slider (the pocket walls end on layer 61, 12.12 mm).
After adding it, re-slice, check that the exported .gcode contains PAUSE, and
send that .gcode (not the STL) to the printer. At the pause,
press the four 10.6 mm-hole washers down the ribs, then the three 8.9 mm-hole ones,
check that nothing stands above the pocket rim, and resume. Afterwards: shake
it next to your ear, then cut the lid open with a knife to recover the
washers. Report whether the washers went down with firm hand pressure, sat
flat, and whether the nozzle touched them.

Material: PETG, same settings as the flywheel will use (100% infill).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *

# ============================================================
# PARAMETERS
# ============================================================
WASHER_OD = 37.88               # [M] both types
BIG_HOLE, BIG_T, BIG_N = 10.59, 1.44, 4         # [M] lower washers; smallest hole of the four (Oct 10)
SMALL_HOLE, SMALL_T, SMALL_N = 8.93, 1.27, 3    # [M] upper washers (Oct 10; first estimate 9.5 was high)
STACK_H = 9.91                  # [M] seven washers stacked, calipers
FLOOR = 1.6                     # 8 layers under the washers
PAUSE_Z = 12.2                  # pocket top. Test 2 (Oct 10): at 11.8 the first lid layer printed onto the steel.
                                # The sliced floor ends at 1.52, so the stack tops out at 11.43-11.6; the lid's first
                                # layer now starts at 12.12, 0.5-0.7 mm above it, and bridges instead
LID = 0.6                       # 3 layers, cut away after the test
POCKET_D = WASHER_OD + 0.4 + HOLE_COMP          # prints about 0.4 mm over the washer
WALL = 2.4
STEP_Z = FLOOR + BIG_N * BIG_T - 0.3            # top of the lower section, 0.3 mm under the fourth big washer's top
RIB_N = 6
RIB_W = 0.9                     # rib width at the post surface
# Rib tip diameter as drawn. First test (Oct 10): drawn 11.30 printed 11.18-11.38 (lower) and
# drawn 9.80 printed 9.53-9.64 (upper; the 0.3 mm tips print short). Targets are hole + 0.15 mm
# across the ribs as printed, corrected by those offsets.
RIB_TIP_D_BIG = 10.76           # prints about 10.74 on a 10.59 hole
RIB_TIP_D_SMALL = 9.30          # prints about 9.08 on an 8.93 hole
CORE_UNDER = 0.8                # core post diameter below the washer hole


def ribbed_post(hole, tip_d, z0, z1):
    core = cyl(hole - CORE_UNDER, z0, z1)
    r_tip = tip_d / 2
    r_root = (hole - CORE_UNDER) / 2 - 0.2
    ribs = None
    for k in range(RIB_N):
        a = 360.0 * k / RIB_N
        rib = (cq.Workplane("XY").polyline([(r_root, -RIB_W / 2), (r_tip, -0.15), (r_tip, 0.15), (r_root, RIB_W / 2)])
               .close().extrude(z1 - z0).translate((0, 0, z0)).rotate((0, 0, 0), (0, 0, 1), a))
        ribs = rib if ribs is None else ribs.union(rib)
    return core.union(ribs)


def build():
    od = POCKET_D + 2 * WALL
    body = cyl(od, 0, PAUSE_Z + LID)
    body = body.cut(cyl(POCKET_D, FLOOR, PAUSE_Z))
    body = body.union(ribbed_post(BIG_HOLE, RIB_TIP_D_BIG, FLOOR - 0.01, STEP_Z))
    body = body.union(ribbed_post(SMALL_HOLE, RIB_TIP_D_SMALL, STEP_Z - 0.01, PAUSE_Z + 0.01))
    return body


def washer_stack(z0=FLOOR):
    """Stand-in washers in place, for the fit check below."""
    out, z = None, z0
    for hole, t, n in ((BIG_HOLE, BIG_T, BIG_N), (SMALL_HOLE, SMALL_T, SMALL_N)):
        for _ in range(n):
            w = cyl(WASHER_OD, z, z + t).cut(cyl(hole, z - 1, z + t + 1))
            out = w if out is None else out.union(w)
            z += t
    return out, z


if __name__ == "__main__":
    export(build(), "washer_stack_test", os.path.dirname(os.path.abspath(__file__)))
