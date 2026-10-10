"""PocketADCS sizing calculations.

Every number in Section 3 of the technical design document comes from this
script. Inputs marked [A] are assumptions; replace them with measurements
(Section 12 of the document) and re-run:

    .venv/bin/python docs/sizing.py

Units: SI internally; printed with units.
"""
import math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "cad"))

G = 9.81
RHO_STEEL = 7850.0            # kg/m3
RHO_PLA = 1240.0              # kg/m3
rpm2rad = lambda rpm: rpm * 2 * math.pi / 60
rad2rpm = lambda w: w * 60 / (2 * math.pi)

# ---------------- inputs ----------------
KV = 920.0                    # rpm/V, motor label
POLE_PAIRS = 7                # [A] 12N14P stator typical of 2212 920KV
PRINT_FILL = 0.55             # [A] printed mass / solid mass for 3-4 walls, 20-40% infill on 4-5 mm plates
BELL_MASS, BELL_R = 0.025, 0.014     # [A] rotating bell mass (kg), radius (m)
MOTOR_STATOR_MASS = 0.030     # [A] non-rotating motor mass (kg)
LOW_RPM = 800.0               # [A] below this, sensorless commutation is unreliable
OVERSPEED_MARGIN = 100.0      # interlock trips at cap + this
WASHERS = {                   # name: (OD mm, ID mm, thickness mm, count, bias rpm, cap rpm)  [A] hardware sizes
    "3 x 2.5 in fender washer (baseline)": (63.5, 8.0, 1.6, 3, 2400.0, 4000.0),
    "3 x 50 mm fender washer (fallback)": (50.0, 6.4, 1.5, 3, 3000.0, 6000.0),
    "6 x 37.9 mm washer (on hand)": (37.88, 8.0, 1.5, 6, 3400.0, 6000.0),     # [M] OD; [A] hole 8.0, 1.5 thick
    "9 x 37.9 mm washer (on hand)": (37.88, 8.0, 1.5, 9, 3400.0, 6000.0),
}
HARDWARE = [                  # (name, mass kg, x m, y m, kind, size)   [A] masses from typical listings
    ("8 x AA NiMH cells + 16 contact screw sets", 0.243, 0.003, 0.0, "box", (0.053, 0.063)),   # [A] 27 g per cell, 1.7 g per screw set; printed holder comes from the CAD
    ("ESC", 0.008, 0.034, 0.0, "point", None),
    ("perfboards x3 + parts", 0.045, 0.0, 0.0, "box", (0.050, 0.070)),
    ("motor stator", MOTOR_STATOR_MASS, 0.0, 0.0, "point", None),
    ("wiring, fuse, buck, nuts", 0.035, 0.0, 0.0, "box", (0.080, 0.080)),
]
# Power: motor, ESC, avionics, pack
R_PHASE = 0.12                # [A] ohm, 2212 920KV listings give 0.09 to 0.13
I_NOLOAD = 0.5                # [A] A at 10 V, typical 2212 920KV listing; used at every speed (conservative)
ESC_EFF = 0.90                # [A]
AVIONICS_5V_AVG, AVIONICS_5V_PEAK = 0.15, 0.40   # [A] A on the 5 V rail: XIAO with Wi-Fi on, sensors, Hall, OP550s
BUCK_EFF = 0.85               # [A]
PACK_CELLS = 8
PACK_V_NOM, PACK_V_LOW = 1.2 * PACK_CELLS, 1.1 * PACK_CELLS
PACK_MAH = 2000.0             # [A] Energizer Recharge AA rating; read the label
PACK_USABLE = 0.8             # [A] fraction delivered above 1.1 V per cell at under 2 A
PACK_R = 8 * 0.030 + 0.10     # [A] ohm: 30 mOhm per NiMH cell plus holder contacts and wiring
# Bearing friction: SKF simplified model M = 0.5 * mu * P * d for a deep-groove bearing
MU_DGBB = 0.0015              # [V] SKF constant-coefficient value for deep groove ball bearings
BORE = 0.008                  # m
FRICTION_PLAN = (1e-3, 3e-3)  # [A] N*m planning range (doc Section 3.6): grease, shields, and a printed fit raise it well above the SKF figure
ROD_D, ROD_L = 0.0038, 0.245  # #8-32 threaded rod, modelled as a 3.8 mm solid rod (between root and major diameter)
ROD_R = 0.04575 * math.sqrt(2)  # corner column centre, from the axis


def washer_I(od, idd, t, n):
    ro, ri = od / 2000, idd / 2000
    m = RHO_STEEL * math.pi * (ro ** 2 - ri ** 2) * t / 1000 * n
    return m, 0.5 * m * (ro ** 2 + ri ** 2)


def printed_parts_inertia():
    """Mass and Iz of the printed box parts from the CAD, scaled by PRINT_FILL."""
    from OCP.GProp import GProp_GProps
    from OCP.BRepGProp import BRepGProp
    from OCP.gp import gp_Ax1, gp_Pnt, gp_Dir
    import assembly
    parts = assembly.placed_parts()
    out = {}
    for name, wp in parts.items():
        if name == "base_hub":
            continue                          # fixed to the bench, does not rotate
        p = GProp_GProps()
        BRepGProp.VolumeProperties_s(wp.val().wrapped, p)
        vol = p.Mass() * 1e-9                 # mm3 -> m3
        iz = p.MomentOfInertia(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1))) * 1e-15  # mm5 -> m5
        zc = p.CentreOfMass().Z() / 1000
        k = RHO_PLA * PRINT_FILL
        out[name] = (vol * k, iz * k, zc)
    return out


def main():
    global kt
    kt = 60 / (2 * math.pi * KV)
    print(f"Motor torque constant Kt = {kt*1000:.2f} mN*m/A  (ideal, from KV)")

    printed = printed_parts_inertia()
    m_print = sum(v[0] for v in printed.values())
    i_print = sum(v[1] for v in printed.values())
    zc_print = sum(v[0] * v[2] for v in printed.values()) / m_print
    print(f"Printed rotating parts: {m_print*1000:.0f} g, Iz {i_print:.2e} kg*m2, CG z {zc_print*1000:.0f} mm")

    m_hw, i_hw = 0.0, 0.0
    for name, m, x, y, kind, size in HARDWARE:
        i = m * (x * x + y * y)
        if kind == "box":
            a, b = size
            i += m * (a * a + b * b) / 12
        m_hw += m; i_hw += i
    m_rod = RHO_STEEL * math.pi * (ROD_D / 2) ** 2 * ROD_L
    i_rod = 4 * m_rod * ROD_R ** 2
    print(f"Hardware (non-wheel): {m_hw*1000:.0f} g, Iz {i_hw:.2e}; rods 4 x {m_rod*1000:.1f} g, Iz {i_rod:.2e}")

    for wname, (od, idd, t, n, BIAS_RPM, CAP_RPM) in WASHERS.items():
        m_w, i_w = washer_I(od, idd, t, n)
        i_wheel = i_w + BELL_MASS * BELL_R ** 2
        m_box = m_print + m_hw + 4 * m_rod + m_w + BELL_MASS
        i_box = i_print + i_hw + i_rod                    # wheel excluded: it is the actuator
        print(f"\n=== Flywheel: {wname}, bias {BIAS_RPM:.0f} rpm, cap {CAP_RPM:.0f} rpm ===")
        print(f"  washer mass {m_w*1000:.0f} g, wheel Iz (washers+bell) {i_wheel:.2e} kg*m2, rim speed at cap "
              f"{rpm2rad(CAP_RPM)*od/2000:.1f} m/s")
        print(f"  box mass (incl. wheel) {m_box*1000:.0f} g, weight {m_box*G:.2f} N, box Iz (excl. wheel) {i_box:.2e} kg*m2")
        print(f"  inertia ratio wheel/box {i_wheel/i_box:.3f}")
        # Momentum: (I_b + I_w) w_b + I_w W = const, W = wheel speed relative to the box.
        i_tot = i_box + i_wheel
        dw = rpm2rad(1000)
        print(f"  box rate per 1000 rpm wheel change: {math.degrees(i_wheel*dw/i_tot):.0f} deg/s")
        up = i_wheel * rpm2rad(CAP_RPM - BIAS_RPM) / i_tot
        down = i_wheel * rpm2rad(BIAS_RPM - LOW_RPM) / i_tot
        print(f"  absorbable box spin from bias: {up/(2*math.pi):.2f} rev/s one way, {down/(2*math.pi):.2f} rev/s the other")
        for amps in (1, 3):
            tau = kt * amps
            # Box: I_b w_b' = -tau.  Wheel: I_w (w_b' + W') = tau  ->  W' = tau (1/I_w + 1/I_b)
            print(f"  at {amps} A: torque {tau*1000:.1f} mN*m, box accel {math.degrees(tau/i_box):.0f} deg/s2, "
                  f"wheel accel relative to box {rad2rpm(tau*(1/i_wheel + 1/i_box)):.0f} rpm/s")
        # 90 deg rest-to-rest slew, bang-bang, torque-limited at 1 A
        tau = kt * 1.0; th = math.pi / 2
        t_slew = 2 * math.sqrt(th / (tau / i_box))
        w_peak = tau / i_box * t_slew / 2
        print(f"  90 deg bang-bang slew at 1 A: {t_slew:.2f} s, peak box rate {math.degrees(w_peak):.0f} deg/s, "
              f"wheel excursion {rad2rpm(w_peak*i_tot/i_wheel):.0f} rpm")
        for rpm in (BIAS_RPM, CAP_RPM):
            w = rpm2rad(rpm)
            e_j = 0.5 * i_wheel * w * w
            f01 = m_w * 0.0001 * w * w
            f02 = m_w * 0.0002 * w * w
            print(f"  {rpm:.0f} rpm: energy {e_j:.1f} J, imbalance force at 0.1 mm {f01:.2f} N, at 0.2 mm {f02:.2f} N, "
                  f"vibration {rpm/60:.0f} Hz, Hall {2*rpm/60:.0f} pulses/s, eRPM {rpm*POLE_PAIRS:.0f}")
        trip = CAP_RPM + OVERSPEED_MARGIN
        print(f"  overspeed trip {trip:.0f} rpm: energy {0.5*i_wheel*rpm2rad(trip)**2:.2f} J")
        e_worst = (idd - 5.0) / 2 / 1000          # washer hole on an M5 thread, fully off-centre
        print(f"  worst-case uncentred stack ({e_worst*1000:.2f} mm on M5): force at bias "
              f"{m_w*e_worst*rpm2rad(BIAS_RPM)**2:.1f} N, at cap {m_w*e_worst*rpm2rad(CAP_RPM)**2:.1f} N")
        print(f"  momentum at cap {i_wheel*rpm2rad(CAP_RPM):.4f} N*m*s")
        # wheel stopped completely (e-stop or Safe): all its momentum ends up in the box
        for label, rpm in (("bias", BIAS_RPM), ("cap", CAP_RPM)):
            wb = i_wheel * rpm2rad(rpm) / i_tot
            print(f"  wheel stopped from {label}: box reaches {wb/(2*math.pi):.2f} rev/s; "
                  f"board 35 mm from axis sees {wb*wb*0.035/G:.2f} g; box corner speed {wb*ROD_R:.2f} m/s")
        # overspeed overshoot from speed-sensing lag at full (3 A) torque
        lag = 0.030
        wdot = kt * 3.0 * (1 / i_wheel + 1 / i_box)
        over = CAP_RPM + OVERSPEED_MARGIN + rad2rpm(wdot) * lag
        print(f"  overspeed with {lag*1000:.0f} ms sensing lag at 3 A: {over:.0f} rpm, "
              f"energy {0.5*i_wheel*rpm2rad(over)**2:.2f} J")
        # gravity torque if the axle is tilted and the CG is off-axis
        for e_mm, tilt_deg in ((5, 1),):
            tg = m_box * G * e_mm / 1000 * math.sin(math.radians(tilt_deg))
            print(f"  gravity torque, CG {e_mm} mm off axis, axle tilted {tilt_deg} deg: {tg*1000:.2f} mN*m")
        # Can the box turn itself? Bearing friction against motor torque.
        m_skf = 0.5 * MU_DGBB * m_box * G * BORE        # whole box weight on the lower bearing
        h_margin = i_wheel * rpm2rad(min(CAP_RPM - BIAS_RPM, BIAS_RPM - LOW_RPM))
        print(f"  bearing friction: SKF estimate {m_skf*1000:.3f} mN*m; planning range "
              f"{FRICTION_PLAN[0]*1000:.0f} to {FRICTION_PLAN[1]*1000:.0f} mN*m")
        for fr in FRICTION_PLAN:
            print(f"  friction {fr*1000:.0f} mN*m: motor torque at 1 A is {kt/fr:.1f} x, at 3 A {3*kt/fr:.1f} x; "
                  f"{fr/kt*1000:.0f} mA of motor current to overcome it; while the box turns, it uses the "
                  f"bias-to-limit wheel margin in {h_margin/fr:.1f} s")
        print(f"  gravity torque above, held at the worst heading: uses the wheel margin in {h_margin/tg:.0f} s")
        # Battery current. Motor model: V = I R + w/KV, I_in = I + I0; ESC efficiency on top.
        def batt_amps(rpm, i_motor, v_pack):
            w = rpm2rad(rpm)
            p_motor = (i_motor + I_NOLOAD) * (i_motor * R_PHASE + w * kt)
            p_avi = 5.0 * AVIONICS_5V_AVG / BUCK_EFF
            return (p_motor / ESC_EFF + p_avi) / v_pack, p_motor / ESC_EFF / v_pack
        cases = [("held at bias", BIAS_RPM, 0.0), ("held at cap", CAP_RPM, 0.0),
                 ("1 A manoeuvre at cap", CAP_RPM, 1.0), ("3 A peak at cap", CAP_RPM, 3.0)]
        for label, rpm, im in cases:
            tot, mot = batt_amps(rpm, im, PACK_V_NOM)
            print(f"  battery current, {label}: {tot:.2f} A total ({mot:.2f} A motor), sag {tot*PACK_R:.2f} V")
        peak = batt_amps(CAP_RPM, 3.0, PACK_V_LOW)[0] + 5.0 * (AVIONICS_5V_PEAK - AVIONICS_5V_AVG) / BUCK_EFF / PACK_V_LOW
        v_need = rpm2rad(CAP_RPM) * kt + 3.0 * R_PHASE
        print(f"  worst case at the 8.8 V cutoff: {peak:.2f} A, pack at {PACK_V_LOW - peak*PACK_R:.2f} V under load; "
              f"cap at 3 A needs {v_need:.2f} V on the motor, {v_need/(PACK_V_LOW - peak*PACK_R)*100:.0f} % duty")
        # Demo duty cycle: idle at bias, 25 % of the time a 1 A manoeuvre near the cap
        avg = 0.75 * batt_amps(BIAS_RPM, 0.0, PACK_V_NOM)[0] + 0.25 * batt_amps(CAP_RPM, 1.0, PACK_V_NOM)[0]
        hours = PACK_MAH / 1000 * PACK_USABLE / avg
        print(f"  demo average {avg:.2f} A -> {hours:.1f} h per charge ({PACK_MAH:.0f} mAh, {PACK_USABLE*100:.0f} % usable)")

    # One-way spin endurance: friction drains the wheel margin only while the box turns, so the time
    # the box can keep turning one way is (wheel inertia x usable speed range) / friction torque.
    print("\nOne-way spin endurance (s) = I_w x usable wheel range / friction torque")
    print("  (the box's own spin rate uses part of the range: rpm of wheel change per 0.5 rev/s of box rate is listed per wheel)")
    E_LIMIT = 6.0                                  # PR-4: stored energy under 6 J at the overspeed trip
    od, idd, t, n, bias, cap = WASHERS["3 x 2.5 in fender washer (baseline)"]
    configs = []
    for nw, label in ((3, "3 washers"), (4, "4 washers"), (5, "5 washers")):
        i_w = washer_I(od, idd, t, nw)[1] + BELL_MASS * BELL_R ** 2
        trip = min(rad2rpm(math.sqrt(2 * E_LIMIT / i_w)), cap + OVERSPEED_MARGIN)
        c = trip - OVERSPEED_MARGIN
        configs.append((f"{label}, bias {bias:.0f}, either way", i_w, c, min(c - bias, bias - LOW_RPM)))
        configs.append((f"{label}, cap {c:.0f}, start low at {LOW_RPM + 200:.0f} rpm (box against wheel)", i_w, c, c - (LOW_RPM + 200)))
        configs.append((f"{label}, cap {c:.0f}, start high at {c - 200:.0f} rpm (box with wheel)", i_w, c, (c - 200) - (LOW_RPM + 200)))
    frictions = (0.1e-3, 0.3e-3, 1e-3, 3e-3)
    print("  " + "configuration".ljust(110) + "".join(f"{f*1000:>9.1f} mN*m" for f in frictions))
    for label, i_w, c, rng in configs:
        h = i_w * rpm2rad(rng)
        i_box_now = i_print + i_hw + i_rod
        per_half = rad2rpm(math.pi * (i_box_now + i_w) / i_w)
        print("  " + f"{label} ({rng:.0f} rpm range; 0.5 rev/s costs {per_half:.0f} rpm)".ljust(110) + "".join(f"{h/f:>14.0f}" for f in frictions))

    # bearing friction and the coin test
    print("\nCoin test: string at 50 mm radius -> 1 g hanging mass = "
          f"{0.001*G*0.050*1000:.2f} mN*m")
    # structural: a 20 N push at the top of the box
    push, arm = 20.0, 0.227 + 0.004
    m = push * arm
    z_boss = math.pi * (30 ** 4 - 22 ** 4) / (32 * 30)
    print(f"20 N push at the top: moment {m:.2f} N*m, bearing radial load ~{m/0.025:.0f} N each "
          f"(608-2Z C0 = 1370 N), boss bending stress {m*1000/z_boss:.1f} MPa, "
          f"M8 bending stress {m*1000/(math.pi*8**3/32):.0f} MPa")
    # DShot timing
    for rate, bit_us in (("DShot300", 1e3 / 300), ("DShot600", 1e3 / 600)):
        print(f"{rate}: frame {16*bit_us:.1f} us -> max ~{1e6/(16*bit_us+20):.0f} frames/s with a 20 us gap")


if __name__ == "__main__":
    main()
