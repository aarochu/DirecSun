"""Stack every printed part in place with stand-in hardware, check for
collisions, and export a viewing model (not for printing).

Writes assembly.step (coloured, for a CAD viewer) and assembly_view.stl
(for the preview renderer). Prints every pair of solids that overlap.
"""
import os, sys, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stack import *
import bottom_cap, motor_deck, hall_bracket, power_deck, battery_deck, avionics_deck, top_deck, base_hub, flywheel_guard, lock_pin
import battery_holder_body, battery_holder_cap

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- stand-in hardware (approximate, for fit checks only) ----
MOTOR_BODY_H = 31.0        # 2212 mount face to bell top, upper end of sold sizes
WASHER_T = 4.8             # three 2.5 in fender washers, 1.6 mm each
NUT_STACK_H = 8.0          # prop nut + jam nut
SHAFT_D = 5.0


def ring(od, idd, z0, z1, x=0.0, y=0.0):
    return cyl(od, z0, z1, x, y).cut(cyl(idd, z0 - 1, z1 + 1, x, y))


Z_HOLDER = Z_BATTERY + PLATE_T


def holder_frame(solid, end=None):
    """Move a solid from holder-part coordinates into the stack.

    Body coordinates: x across the pack, y up, z along the cells from end A.
    Cap coordinates: same x and y, z outward from the cap's inner face.
    A rotation of 120 deg about (1,1,1) maps x->Y, y->Z, z->X; cap A is then turned 180 deg about Z.
    """
    r = solid.rotate((0, 0, 0), (1, 1, 1), 120)
    if end == "A":
        r = r.rotate((0, 0, 0), (0, 0, 1), 180)
    x = HOLDER_XC + HOLDER_BODY_L / 2 if end == "B" else HOLDER_XC - HOLDER_BODY_L / 2
    return r.translate((x, 0, Z_HOLDER))


def cells(extra_len=0.0):
    """Stand-in AA cells, as long as the free gap between the contact heads (springs at rest)."""
    out = None
    z0 = CONTACT_HEAD_H + 0.05
    z1 = HOLDER_BODY_L - CONTACT_HEAD_H - 0.05 + extra_len
    for (x, y, _) in cell_centres():
        c = cyl(CELL_D, z0, z1, x, y)
        out = c if out is None else out.union(c)
    return holder_frame(out)


def contact_hardware(end):
    """Per contact: M3 x 8 button head inside, washer + wire loop + washer + nut outside; M2 x 8 cap screws."""
    ft, t = HOLDER_FLEX_T, HOLDER_CAP_T
    out = None
    for (x, y, _) in cell_centres():
        h = (cyl(CONTACT_HEAD_D, -CONTACT_HEAD_H, 0, x, y)
             .union(cyl(3.0, 0, 8.0, x, y))
             .union(cyl(7.0, ft + 0.02, ft + 0.02 + 4.4, x, y)))
        out = h if out is None else out.union(h)
    for x in HOLDER_CAP_SCREWS:
        out = out.union(cyl(1.6, t - 8.0, t, x, HOLDER_H / 2)).union(cyl(3.5, t + 0.02, t + 1.3, x, HOLDER_H / 2))
    return holder_frame(out, end)


def placed_parts():
    hb = hall_bracket.build().translate((0, 0, Z_MOTOR + MOTOR_PLATE_T)) \
        .rotate((0, 0, 0), (0, 0, 1), HALL_ANGLE)
    return {
        "base_hub": base_hub.build().translate((0, 0, -HUB_H)),
        "bottom_cap": bottom_cap.build().translate((0, 0, Z_BOTTOM)),
        "motor_deck": motor_deck.build().translate((0, 0, Z_MOTOR)),
        "hall_bracket": hb,
        "flywheel_guard": flywheel_guard.build().translate((0, 0, Z_MOTOR + MOTOR_PLATE_T - GUARD_GROOVE_DEPTH)),
        "power_deck": power_deck.build().translate((0, 0, Z_POWER)),
        "battery_deck": battery_deck.build().translate((0, 0, Z_BATTERY)),
        "battery_holder_body": holder_frame(battery_holder_body.build()),
        "battery_cap_a": holder_frame(battery_holder_cap.build(feet=True), "A"),
        "battery_cap_b": holder_frame(battery_holder_cap.build(feet=False), "B"),
        "avionics_deck": avionics_deck.build().translate((0, 0, Z_AVIONICS)),
        # printed rails-up, installed flipped about Y so the guides hang inside the envelope
        # inserted for spin-up: tab resting on the bottom cap, shaft down into the hub flange
        "lock_pin": lock_pin.build().rotate((0, 0, 0), (0, 1, 0), 90)
            .translate((LOCK_R - lock_pin.axis_offset(), 0, Z_BOTTOM + PLATE_T)),
        "top_deck": top_deck.build().rotate((0, 0, 0), (0, 1, 0), 180).translate((0, 0, Z_TOP + PLATE_T)),
    }


def hardware():
    mface = Z_MOTOR + MOTOR_PLATE_T
    bell_top = mface + MOTOR_BODY_H
    hw = {
        f"bolt_M8x{BOLT_LEN:.0f}": cyl(7.9, -HUB_H + M8_HEAD_H, -HUB_H + M8_HEAD_H + BOLT_LEN)
            .union(hex_prism(13.0, -HUB_H, -HUB_H + M8_HEAD_H - 0.05)),
        "bearing_lower": ring(BRG_OD, 8.0, LOWER_POCKET[0], LOWER_POCKET[1]),
        "bearing_upper": ring(BRG_OD, 8.0, UPPER_POCKET[0], UPPER_POCKET[1]),
        "motor_2212": cyl(BELL_D, mface + 0.05, bell_top),
        "washer_flywheel": ring(63.5, 8.0, bell_top, bell_top + WASHER_T),
        "shaft_and_nuts": cyl(SHAFT_D, bell_top, bell_top + WASHER_T + NUT_STACK_H + 3)
            .union(cyl(9.0, bell_top + WASHER_T, bell_top + WASHER_T + NUT_STACK_H)),
        "esc": box_at(power_deck.ESC_X[0], power_deck.ESC_X[1], power_deck.ESC_Y[0], power_deck.ESC_Y[1],
                      Z_POWER + PLATE_T + 0.05, Z_POWER + PLATE_T + 6),
        "aa_cells": cells(),
        "contacts_cap_a": contact_hardware("A"),
        "contacts_cap_b": contact_hardware("B"),
    }
    for k, (x, y) in enumerate(battery_deck.foot_points()):
        zt = Z_HOLDER + HOLDER_TAB_T
        hw[f"foot_screw{k}"] = (cyl(3.0, zt - 8.0, zt, x, y).union(cyl(5.7, zt + 0.02, zt + 1.65, x, y))
                                .union(hex_prism(5.5, Z_BATTERY + 0.05, Z_BATTERY + 2.4, x, y).cut(cyl(3.2, Z_BATTERY - 1, Z_BATTERY + 3, x, y))))
    z0 = Z_POWER + PLATE_T + GUIDE_SLOT_Z + 0.2
    hw["board_power"] = box_at(power_deck.BOARD_XC - BOARD_W / 2, power_deck.BOARD_XC + BOARD_W / 2, -35, 35, z0, z0 + BOARD_T)
    # XIAO carrier: 50 x 35 mm perfboard in the short guides, XIAO at its -Y edge
    za = Z_AVIONICS + PLATE_T + GUIDE_SLOT_Z + 0.2
    cy0 = avionics_deck.CARRIER_Y0
    hw["board_xiao_carrier"] = box_at(-BOARD_W / 2, BOARD_W / 2, cy0, cy0 + CARRIER_L, za, za + BOARD_T)
    hw["xiao_s3"] = box_at(-8.9, 8.9, cy0, cy0 + 21.0, za + BOARD_T + 2.5, za + BOARD_T + 6.0)
    # Adafruit boards: real outline and hole pattern, plus a nylon M2.5 screw through each hole
    def ada(name, xc, yc, zb, flip=False):
        b = box_at(xc - ADA_BOARD[0] / 2, xc + ADA_BOARD[0] / 2, yc - ADA_BOARD[1] / 2, yc + ADA_BOARD[1] / 2, zb, zb + 1.6)
        parts_z = (zb - 3.0, zb) if flip else (zb + 1.6, zb + 4.6)          # chip and connectors side
        comp = box_at(xc - 9, xc + 9, yc - 3, yc + 3, *parts_z)
        for (hx, hy) in ADA_HOLES:
            b = b.cut(cyl(2.5, zb - 1, zb + 3, xc + hx, yc + hy))
        hw[name] = b.union(comp)
        # M2 x 8 screw: modelled at its 1.6 mm thread root, which must stay inside the printed pilot (M2_PILOT)
        for k, (hx, hy) in enumerate(ADA_HOLES):
            if flip:
                hw[f"{name}_screw{k}"] = cyl(1.6, zb, zb + M2_SCREW_L, xc + hx, yc + hy)
            else:
                hw[f"{name}_screw{k}"] = cyl(1.6, zb + 1.6 - M2_SCREW_L, zb + 1.6, xc + hx, yc + hy)
    ada("mpu6050", 0.0, 0.0, Z_AVIONICS + PLATE_T + ADA_STANDOFF_H + 0.02)
    zt = Z_TOP - ADA_STANDOFF_H - 1.6 - 0.02                 # flipped top deck: boards hang below the standoffs
    ada("qmc5883p", 0.0, top_deck.QMC_Y, zt, flip=True)
    ada("bmp280", 0.0, top_deck.BMP_Y, zt, flip=True)
    for i, (x, y) in enumerate(corner_points()):
        hw[f"rod_{i}"] = cyl(4.17, Z_BOTTOM - 1, STACK_H + 3, x, y)
    return hw


def interference_report(solids):
    hits = []
    names = list(solids)
    for a, b in itertools.combinations(names, 2):
        sa, sb = solids[a].val(), solids[b].val()
        ba, bb = sa.BoundingBox(), sb.BoundingBox()
        if (ba.xmax < bb.xmin or bb.xmax < ba.xmin or ba.ymax < bb.ymin or bb.ymax < ba.ymin
                or ba.zmax < bb.zmin or bb.zmax < ba.zmin):
            continue
        common = sa.intersect(sb)
        v = common.Volume() if common is not None else 0.0
        if v > 0.5:
            hits.append((a, b, v))
    return hits


if __name__ == "__main__":
    parts = placed_parts()
    hw = hardware()
    allsolids = {**parts, **hw}
    hits = interference_report(allsolids)
    if hits:
        for a, b, v in hits:
            print(f"OVERLAP {a} <-> {b}: {v:.1f} mm3")
    else:
        print(f"No overlaps among {len(allsolids)} solids")

    # clearances that matter
    bolt_tip = -HUB_H + M8_HEAD_H + BOLT_LEN
    bell_top = Z_MOTOR + MOTOR_PLATE_T + MOTOR_BODY_H
    print(f"bolt tip z={bolt_tip:.1f}, motor deck underside z={Z_MOTOR:.1f}, gap {Z_MOTOR - bolt_tip:.1f} mm")
    print(f"bolt engagement in upper bearing: {min(bolt_tip, UPPER_POCKET[1]) - UPPER_POCKET[0]:.1f} of {BRG_W} mm")
    nut_top = bell_top + WASHER_T + NUT_STACK_H
    print(f"flywheel nut top z={nut_top:.1f}, power deck underside z={Z_POWER:.1f}, gap {Z_POWER - nut_top:.1f} mm")
    lp = parts["lock_pin"].val().BoundingBox()
    flange_top = -HUB_H + base_hub.FLANGE_T
    print(f"lock pin tip z={lp.zmin:.1f}, hub flange top z={flange_top:.1f}, engagement {flange_top - lp.zmin:.1f} mm, "
          f"hole bottom z={flange_top - LOCK_HUB_DEPTH:.1f}; tab top z={lp.zmax:.1f} vs motor deck z={Z_MOTOR:.1f}")
    print(f"Hall post top z={Z_MOTOR + MOTOR_PLATE_T + hall_bracket.POST_H:.1f}, washer underside z={bell_top:.1f}")
    print(f"room above power board {Z_BATTERY - (Z_POWER + PLATE_T + GUIDE_SLOT_Z + BOARD_T):.1f} mm, "
          f"between MPU-6050 parts and top-deck boards {hw['qmc5883p'].val().BoundingBox().zmin - hw['mpu6050'].val().BoundingBox().zmax:.1f} mm")
    # measured from the placed solids, not from constants
    printed_z = [s.val().BoundingBox() for n, s in parts.items() if n != "base_hub"]
    zmax = max(b.zmax for b in printed_z); zmin_hub = parts["base_hub"].val().BoundingBox().zmin
    hb = parts["battery_holder_body"].val().BoundingBox()
    print(f"battery holder body x {hb.xmin:.1f}..{hb.xmax:.1f}, y {hb.ymin:.1f}..{hb.ymax:.1f}, z {hb.zmin:.1f}..{hb.zmax:.1f}; "
          f"top gap to avionics deck {Z_AVIONICS - hb.zmax:.1f} mm")
    ca = parts["battery_cap_a"].val().BoundingBox(); cb = parts["battery_cap_b"].val().BoundingBox()
    print(f"battery caps: A x {ca.xmin:.1f}..{ca.xmax:.1f}, B x {cb.xmin:.1f}..{cb.xmax:.1f} (deck edge +/-{HALF:.0f})")
    print(f"box top z={zmax:.1f} mm (2U limit {STACK_H:.1f}), overall from hub base {zmax - zmin_hub:.1f} mm")
    if zmax > STACK_H + 0.01:
        print("ENVELOPE VIOLATION: printed parts exceed the 2U height")

    # positive controls: the check must report collisions that are known to exist
    controls = {
        "battery holder 2.5 mm too tall vs avionics deck": {"avionics_deck": parts["avionics_deck"],
            "probe": box_at(HOLDER_XC - 20, HOLDER_XC + 20, -HOLDER_W / 2, HOLDER_W / 2, Z_HOLDER + 0.05, Z_HOLDER + HOLDER_H + 2.5)},
        "AA cells 1 mm too long vs the contact heads": {"contacts_cap_b": hw["contacts_cap_b"], "probe": cells(extra_len=1.0)},
        "78 mm wheel vs guard ring": {"flywheel_guard": parts["flywheel_guard"],
            "probe": ring(78.0, 8.0, bell_top, bell_top + WASHER_T)},
        "MPU-6050 board 1 mm low vs its standoffs": {"avionics_deck": parts["avionics_deck"],
            "probe": box_at(-12.7, 12.7, -8.89, 8.89, Z_AVIONICS + PLATE_T + ADA_STANDOFF_H - 1.0, Z_AVIONICS + PLATE_T + ADA_STANDOFF_H + 0.6)},
        "screw 1.5 mm off the hole pattern vs avionics deck": {"avionics_deck": parts["avionics_deck"],
            "probe": cyl(1.6, Z_AVIONICS + 1.0, Z_AVIONICS + PLATE_T + ADA_STANDOFF_H, 10.16 + 1.5, 6.35)},
        "M2 screw 1 mm too long vs the pilot floor": {"avionics_deck": parts["avionics_deck"],
            "probe": cyl(1.6, Z_AVIONICS + M2_PILOT_FLOOR - 1.0, Z_AVIONICS + PLATE_T + ADA_STANDOFF_H, 10.16, 6.35)},
    }
    for label, pair in controls.items():
        found = interference_report(pair)
        print(f"positive control, {label}: {'DETECTED' if found else 'NOT DETECTED - CHECK IS BROKEN'}")
        if not found:
            sys.exit(2)

    # viewing exports
    colours = {"base_hub": "gray", "lock_pin": "red", "bottom_cap": "orange", "motor_deck": "orange", "hall_bracket": "orange", "flywheel_guard": "orange",
               "power_deck": "orange", "battery_deck": "orange", "battery_holder_body": "orange", "battery_cap_a": "orange", "battery_cap_b": "orange", "avionics_deck": "orange", "top_deck": "orange"}
    asm = cq.Assembly()
    for n, s in allsolids.items():
        asm.add(s, name=n, color=cq.Color(colours.get(n, "lightsteelblue")))
    asm.export(os.path.join(HERE, "assembly.step"))
    combined = None
    for n, s in allsolids.items():
        if n.startswith("rod_"):
            continue
        combined = s if combined is None else combined.add(s)
    comp = cq.Compound.makeCompound([o for o in combined.vals()])
    cq.exporters.export(cq.Workplane().add(comp), os.path.join(HERE, "assembly_view.stl"),
                        tolerance=0.05, angularTolerance=0.2)
    print("wrote assembly.step and assembly_view.stl")
