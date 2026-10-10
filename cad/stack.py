"""Shared parameters and geometry helpers for the DirecSun (formerly PocketADCS) printed 2U stack.

Every printed part script imports this module so the stack stays consistent.
All dimensions are millimetres. Each part is modelled in its print
orientation with z = 0 on the print bed; `assembly.py` moves parts into place.

Assembly frame: z = 0 is the underside of the bottom cap, +z points up along
the spin axis, x/y are the faces of the 2U box. +X is the sun sensor face.
"""
import math
import cadquery as cq

# ============================================================
# PARAMETERS
# ============================================================
# --- 2U envelope (CubeSat Design Specification: 100 x 100 x 227, 8.5 mm rails)
SIDE = 100.0
HALF = SIDE / 2
RAIL = 8.5                      # corner column width, matches CubeSat rail width
COL_XY = HALF - RAIL / 2        # 45.75, centre of each corner column
EDGE_CHAMFER = 1.0              # vertical outer edges of plates

# --- Printer hole shrinkage. The bearing fit test (Oct 9 2026) held a 22.0 mm 608 only in the
#     22.4 mm pocket, so this printer makes vertical holes about 0.4 mm small. Every hole, slot, and
#     nut trap that something must pass through or sit in adds HOLE_COMP. Leave Orca's "X-Y hole
#     compensation" at 0 so the correction is not applied twice. Check with hole_gauge.py.
HOLE_COMP = 0.4                 # [M] 22 mm bearing pocket; hole_gauge (Oct 10): 8.7 and 15.3 mm holes fit as intended
LAYER_H = 0.2                   # print layer height; sets the sacrificial bridge layers over bed-face traps

# --- Stack fasteners: four #8-32 threaded rods (4.17 mm major) run through every corner column,
#     clamped by #8-32 nylon-insert lock nuts (11/32 in = 8.73 mm across flats; height assumed 4.4 mm,
#     measure and update). Home Depot had no M3 rod.
ROD_D = 4.4 + HOLE_COMP         # #8-32 clearance, drawn 4.8. hole_ladder (Oct 10): 4.6 slides but scrapes over 6 mm,
                                # 5.0 is loose; the deck columns are 35-55 mm long, so 4.8 avoids binding
ROD_NUT_AF = 9.0 + HOLE_COMP    # 8.73 + clearance
ROD_NUT_H = 4.4                 # [A] nylon-insert lock nut height; measure
ROD_TRAP_DEPTH_TOP = 2.0        # top deck is only 4 mm thick: the nut sits proud, the pocket stops it turning
M3_CLEAR = 3.4 + HOLE_COMP
M3_NUT_AF = 5.8 + HOLE_COMP     # M3 nut is 5.5 across flats, + clearance
M3_NUT_H = 2.6
M3_SELF_TAP = 2.6 + HOLE_COMP               # hole for an M3 screw tapping into plastic

# --- Deck plates: (name, bottom z in assembly, thickness)
PLATE_T = 4.0
MOTOR_PLATE_T = 5.0
Z_BOTTOM, Z_MOTOR, Z_POWER, Z_BATTERY, Z_AVIONICS, Z_TOP = 0.0, 38.0, 95.0, 126.0, 165.0, 223.0
# Z_AVIONICS raised 155 -> 165 for two stacked 4 x AA NiMH holders (about 31 mm tall)
STACK_H = Z_TOP + PLATE_T       # 227.0, a standard 2U height

# --- 608 bearing (8 x 22 x 7) and the bearing post
BRG_OD = 22.0
BRG_W = 7.0
BRG_POCKET_D = 22.4             # [M] bearing_fit_test: the 608 holds only in the 22.4 mm (three-notch) pocket
BED_CHAMFER = 0.5              # 45 deg lead-in on holes that open on the bed face (elephant foot)
BRG_LEDGE_D = 19.6              # outer-ring land: above SKF D2 19.2 (shield edge), below housing abutment max 20
BOSS_OD = 30.0
BOSS_TOP_Z = 32.0               # boss rises from the bottom cap into the box
LOWER_POCKET = (0.0, BRG_W)     # z range in the bottom cap
UPPER_POCKET = (BOSS_TOP_Z - BRG_W, BOSS_TOP_Z)

# --- M8 bolt and base hub
M8_CLEAR = 8.3 + HOLE_COMP
M8_HEAD_AF = 13.3 + HOLE_COMP  # M8 hex head is 13 across flats, + clearance
M8_HEAD_H = 5.6
INNER_RACE_PAD_D = 11.0         # SKF shaft abutment range 10 to 12 mm (inner ring land d1 12.15)
HUB_H = 31.0                    # hub bottom sits at z = -HUB_H in the assembly; sized so the M8 x 60 tip clears the upper bearing
HUB_FLANGE_T = 8.0              # base hub flange thickness
BOLT_LEN = 60.0                 # M8 x 60, tip ends 3.4 mm below the motor deck

# --- 2212 motor (16 x 19 mm M3 pattern; bell about 28 mm across)
MOTOR_HOLES = [(8.0, 0.0), (-8.0, 0.0), (0.0, 9.5), (0.0, -9.5)]
MOTOR_CBORE_D = 6.2 + HOLE_COMP             # M3 socket head from below
MOTOR_CBORE_DEPTH = 3.0         # leaves 2 mm under the motor: use M3 x 6 screws
MOTOR_CENTER_HOLE = 10.0        # clears the shaft end and circlip under the motor
BELL_D = 28.0
HALL_ANGLE = 45.0               # direction of the Hall bracket from the motor axis
HALL_SCREW_R = 28.0

# --- Spin-up lock pin: holds the box to the fixed base hub while the wheel spins up
LOCK_R = 30.0                   # radius of the pin from the spin axis, on +X
LOCK_PIN_D = 6.0                # printed pin diameter
LOCK_HOLE_D = 6.5 + HOLE_COMP   # clearance hole in the bottom cap and the hub
LOCK_HUB_DEPTH = 7.0            # blind hole depth in the base hub flange
LOCK_TIP_ENGAGE = 6.0           # how far the pin tip enters the hub hole

# --- Printed 8 x AA NiMH holder (battery_holder_body.py, battery_holder_cap.py)
#     Cells lie along X in two rows of four, wired in series by alternating their direction.
#     Contacts are M3 x 8 button-head screws (screw kit) on printed spring rings in each end cap.
CELL_D = 14.5                   # IEC LR6 maximum diameter
CELL_L = 50.17                  # [M] tip to tip, Energizer Recharge AA (Oct 10); body alone 48.84
CELL_PITCH = 15.0               # centre spacing; neighbouring bores overlap slightly
CELL_BORE_D = 14.9 + HOLE_COMP  # prints about 14.9 for a 14.5 mm cell
HOLDER_WALL = 1.2
HOLDER_W = 3 * CELL_PITCH + CELL_BORE_D + 2 * HOLDER_WALL   # along assembly Y
HOLDER_H = CELL_PITCH + CELL_BORE_D + 2 * HOLDER_WALL       # height
CONTACT_HEAD_D, CONTACT_HEAD_H = 5.7, 1.65                  # ISO 7380 M3 button head
CONTACT_PRELOAD = 0.45          # each spring ring is pushed out this far by a cell of length CELL_L
HOLDER_BODY_L = CELL_L + 2 * CONTACT_HEAD_H - 2 * CONTACT_PRELOAD
HOLDER_CAP_T = 2.4              # cap plate
HOLDER_FLEX_T = 1.6             # spring ring thickness (8 layers at 0.2 mm); 1.4 returned but felt weak (Oct 10)
HOLDER_SKIRT = 7.8              # recess for the nut, washers and wire loop outside each contact
HOLDER_TAB_L, HOLDER_TAB_W, HOLDER_TAB_T = 7.0, 10.0, 3.0   # cap A feet, M3 x 8 down into the deck
HOLDER_TAB_X = 10.0             # feet centred at +/- this across the cap
HOLDER_XC = 3.0                 # body centre in the battery deck (cap A, with the leads, faces -X)
HOLDER_CAP_SCREWS = (-15.0, 0.0, 15.0)   # M2 x 8 per cap, in the gaps between bores at mid-height


def cell_centres():
    """(across, up) centre of each cell bore in the holder cross-section, and whether + faces cap A.

    Index order is the series order: column 1 bottom, column 1 top, column 2 top, column 2 bottom, ...
    Neighbours are bridged at cap B within each column and at cap A along a row; the pack +
    is column 1 bottom and the pack - is column 4 bottom, both at cap A.
    """
    out = []
    for i in range(4):
        rows = (0, 1) if i % 2 == 0 else (1, 0)
        for j in rows:
            x = (i - 1.5) * CELL_PITCH
            y = HOLDER_WALL + CELL_BORE_D / 2 + j * CELL_PITCH
            out.append((x, y, (i + j) % 2 == 0))
    return out


# --- Flywheel guard: a slotted ring clamped between the motor and power decks
GUARD_ID = 76.0                 # clears the 50 mm washers and the Hall bracket foot (r 35)
GUARD_WALL = 2.4
GUARD_GROOVE_DEPTH = 1.0        # locating groove in the motor deck top
GUARD_GROOVE_CLEAR = 0.3        # per side
GUARD_TOP_GAP = 0.5             # ring stops short of the power deck so the columns carry the clamp

# --- Perfboard card guides (standard 5 x 7 cm protoboard, 1.6 mm thick)
BOARD_W = 50.0                  # across the guides (X)
BOARD_L = 70.0                  # slide direction (Y); board enters from -Y
BOARD_T = 1.6
GUIDE_CLEAR = 0.4 + HOLE_COMP   # total side-to-side play; slots print small like holes
GUIDE_DEPTH = 2.0               # how far the board edge sits in each rail
GUIDE_RAIL_W = 5.0
GUIDE_SLOT_Z = 6.0              # board underside height above the plate: room for solder joints
GUIDE_SLOT_H = 2.0
GUIDE_RAIL_H = 10.0
GUIDE_Y0 = -36.5                # open end
GUIDE_Y1 = 37.0                 # closed end with a stop

# --- Adafruit STEMMA QT sensor boards (MPU-6050 #3886, QMC5883P #6388, BMP280 #2651)
# Source: Adafruit Eagle board files on GitHub (Adafruit-MPU6050-PCB, Adafruit-QMC5883P-Triple-
# Axis-Magnetometer-PCB, Adafruit-BMP280-Breakout-PCB "STEMMA QT"): all three are 25.40 x 17.78 mm,
# four 2.5 mm plated holes 2.54 mm in from each corner, sensor chip at the board centre,
# STEMMA QT connectors centred on the two short edges.
ADA_BOARD = (25.40, 17.78)
ADA_HOLES = [(sx * 10.16, sy * 6.35) for sx in (1, -1) for sy in (1, -1)]   # from board centre
ADA_STANDOFF_OD = 5.5
ADA_STANDOFF_H = 4.0            # board underside above the plate
M2_PILOT = 2.4                  # M2 x 8 button-head screw threads straight into this printed hole. hole_ladder (Oct 10):
                                # 2.3 does not take the screw, 2.5 takes it with play; 2.4 so the threads bite
M2_PILOT_FLOOR = 0.8            # blind: plastic left on the plate's bed face
M2_SCREW_L = 8.0

# --- XIAO carrier: a 50 x 35 mm piece cut from a 5 x 7 cm perfboard, in short card guides
CARRIER_L = 35.0

# --- Wire pass-through slots at the middle of the +-Y edges
WIRE_SLOT = (24.0, 7.0)         # (along the edge, depth)
WIRE_SLOT_INSET = 43.5          # slot centre distance from the axis


# ============================================================
# HELPERS
# ============================================================
def corner_points():
    return [(sx * COL_XY, sy * COL_XY) for sx in (1, -1) for sy in (1, -1)]


def box_at(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box from corner coordinates."""
    return (cq.Workplane("XY")
            .box(x1 - x0, y1 - y0, z1 - z0, centered=False)
            .translate((x0, y0, z0)))


def cyl(d, z0, z1, x=0.0, y=0.0):
    return cq.Workplane("XY").circle(d / 2).extrude(z1 - z0).translate((x, y, z0))


def hex_prism(af, z0, z1, x=0.0, y=0.0):
    return (cq.Workplane("XY").polygon(6, af / math.cos(math.pi / 6))
            .extrude(z1 - z0).translate((x, y, z0)))


def trap_bridge(hole_d, span, z_roof, x=0.0, y=0.0, angle=0.0):
    """Two sacrificial bridge layers over a pocket that opens on the bed face.

    Without them the pocket ceiling is a bridge with a hole in it, which strings. Layer 1
    bridges straight across the pocket and leaves a slot as wide as the hole; layer 2 bridges
    across that slot and leaves a square; the round hole continues above. Cut this together
    with the pocket. span: largest pocket width (across corners for a hex); angle: slot direction, deg.
    """
    a = box_at(-span / 2, span / 2, -hole_d / 2, hole_d / 2, z_roof - 0.01, z_roof + LAYER_H)
    b = box_at(-hole_d / 2, hole_d / 2, -hole_d / 2, hole_d / 2, z_roof - 0.01, z_roof + 2 * LAYER_H)
    return a.union(b).rotate((0, 0, 0), (0, 0, 1), angle).translate((x, y, 0))


def hex_trap_bed(af, hole_d, depth, x=0.0, y=0.0, hex_angle=0.0, slot_angle=0.0):
    """Hex nut trap on the bed face (z = 0 up to depth) with bridge layers under the hole above it."""
    hx = hex_prism(af, -0.01, depth).rotate((0, 0, 0), (0, 0, 1), hex_angle).translate((x, y, 0))
    return hx.union(trap_bridge(hole_d, af / math.cos(math.pi / 6), depth, x, y, slot_angle))


# Corner rod traps: the #8-32 lock nut (8.73 mm across flats) is wider than the 8.5 mm column, so it
# always reaches past the plate edges: 0.79 mm past the X edge and 0.12 mm past the Y edge in this
# orientation (no rotation does better than 0.62 mm on both). It is cosmetic: nothing sits within
# a millimetre of the box corners. The bridge slot runs along the corner diagonal and is clipped
# to the pocket and the column, so it never opens a new gap in the column walls.
ROD_TRAP_HEX_ANGLE = 0.0


def rod_trap(x, y, depth):
    hx = hex_prism(ROD_NUT_AF, -0.01, depth).rotate((0, 0, 0), (0, 0, 1), ROD_TRAP_HEX_ANGLE).translate((x, y, 0))
    footprint = (hex_prism(ROD_NUT_AF, depth - 1, depth + 1).rotate((0, 0, 0), (0, 0, 1), ROD_TRAP_HEX_ANGLE)
                 .translate((x, y, 0)).intersect(box_at(x - RAIL / 2, x + RAIL / 2, y - RAIL / 2, y + RAIL / 2, depth - 1, depth + 1)))
    slot = trap_bridge(ROD_D, ROD_NUT_AF / math.cos(math.pi / 6), depth, x, y, 45.0 if x * y > 0 else -45.0)
    return hx.union(slot.intersect(footprint))


def deck_plate(t, column_h=0.0, nut_trap=None, trap_depth=ROD_NUT_H + 0.2):
    """Square 100 mm plate with optional corner columns and rod holes.

    nut_trap: None, "bottom" (recess on the bed face) or "top". A trap deeper than
    the plate runs up into the corner column above it.
    """
    body = (cq.Workplane("XY")
            .box(SIDE, SIDE, t, centered=(True, True, False))
            .edges("|Z").chamfer(EDGE_CHAMFER))
    if column_h > 0:
        for (x, y) in corner_points():
            body = body.union(box_at(x - RAIL / 2, x + RAIL / 2, y - RAIL / 2, y + RAIL / 2,
                                     t - 0.01, t + column_h))
    top = t + column_h
    for (x, y) in corner_points():
        body = body.cut(cyl(ROD_D, -1, top + 1, x, y))
        if nut_trap == "bottom":
            body = body.cut(rod_trap(x, y, trap_depth))
        elif nut_trap == "top":
            body = body.cut(hex_prism(ROD_NUT_AF, top - trap_depth, top + 0.01, x, y))
    return body


def adafruit_standoffs(xc, yc, t):
    """Four standoffs for an Adafruit 25.4 x 17.78 mm board centred at (xc, yc), on a plate of thickness t."""
    s = None
    for (hx, hy) in ADA_HOLES:
        b = cyl(ADA_STANDOFF_OD, t - 0.01, t + ADA_STANDOFF_H, xc + hx, yc + hy)
        s = b if s is None else s.union(b)
    return s


def adafruit_standoff_cuts(xc, yc, t):
    """Blind pilot holes for M2 x 8 screws that thread directly into the standoff and plate."""
    c = None
    for (hx, hy) in ADA_HOLES:
        h = cyl(M2_PILOT, M2_PILOT_FLOOR, t + ADA_STANDOFF_H + 1, xc + hx, yc + hy)
        c = h if c is None else c.union(h)
    return c


def card_guides(xc, z_base, y0=None, y1=None):
    """Two rails that hold a 50 mm wide perfboard sliding in from -Y, stopping at y1."""
    y0 = GUIDE_Y0 if y0 is None else y0
    y1 = GUIDE_Y1 if y1 is None else y1
    off = BOARD_W / 2 + GUIDE_CLEAR / 2 - GUIDE_DEPTH      # rail inner face
    rails = None
    for s in (1, -1):
        x0 = xc + s * off
        x1 = xc + s * (off + GUIDE_RAIL_W)
        r = box_at(min(x0, x1), max(x0, x1), y0, y1, z_base - 0.01, z_base + GUIDE_RAIL_H)
        rails = r if rails is None else rails.union(r)
    slot_half = BOARD_W / 2 + GUIDE_CLEAR / 2
    slot = box_at(xc - slot_half, xc + slot_half, y0 - 1, y1 - 2.0,
                  z_base + GUIDE_SLOT_Z, z_base + GUIDE_SLOT_Z + GUIDE_SLOT_H)
    return rails.cut(slot)


def wire_slots(t, sides=("+Y", "-Y")):
    cut = None
    a, b = WIRE_SLOT
    for s in sides:
        if s in ("+Y", "-Y"):
            sign = 1 if s == "+Y" else -1
            c = box_at(-a / 2, a / 2, sign * WIRE_SLOT_INSET - b / 2, sign * WIRE_SLOT_INSET + b / 2, -1, t + 1)
        else:
            sign = 1 if s == "+X" else -1
            c = box_at(sign * WIRE_SLOT_INSET - b / 2, sign * WIRE_SLOT_INSET + b / 2, -a / 2, a / 2, -1, t + 1)
        cut = c if cut is None else cut.union(c)
    return cut


def export(part, name, here):
    import os
    stl = os.path.join(here, f"{name}.stl")
    step = os.path.join(here, f"{name}.step")
    cq.exporters.export(part, stl, tolerance=0.01, angularTolerance=0.1)
    cq.exporters.export(part, step)
    bb = part.val().BoundingBox()
    print(f"{name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm, volume {part.val().Volume()/1000:.1f} cm3")
    return stl
