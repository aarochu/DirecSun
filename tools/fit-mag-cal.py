#!/usr/bin/env python3
"""Fit the magnetometer calibration for T-9 (TD section 9.1, FD sections 10.2 and 11.3).

    tools/fit-mag-cal.py capture.jsonl [--map +x,-y,-z] [--min-span 270] [--json]
    tools/fit-mag-cal.py --selftest

Turn the box through 360 degrees with the bench console or the UI in bench mode while
`tools/capture-box.py --silent --out capture.jsonl` records `tlm` (bench mode puts
`mag_raw` in every frame). The tool applies the axis map, fits the horizontal circle
the field should trace to the ellipse the data traces (hard-iron offset, then the
2 x 2 soft-iron matrix that makes it a circle), and prints the `set_params` JSON plus
the residual heading error against the box's own gyro heading `psi_deg`, so the check
is independent of the compass it is calibrating.

Pass: residual within 3 degrees (TD T-9). A map that only rotates the axes is absorbed as a constant heading offset and is harmless; a mirrored map (wrong handedness) makes the compass run backwards against the gyro and fails the residual. The soft-iron matrix is normalised to a
unit-radius circle, so the compass heading is unaffected by the field's scale.
"""
import argparse
import json
import math
import sys

AXES = {"x": 0, "y": 1, "z": 2}


def parse_map(text):
    out = []
    for token in text.split(","):
        token = token.strip()
        if len(token) != 2 or token[0] not in "+-" or token[1] not in AXES:
            raise SystemExit(f"bad axis map entry {token!r}; expected like +x,-y,-z")
        out.append((AXES[token[1]], -1 if token[0] == "-" else 1))
    return out


def box_frame(raw, axis_map):
    return [sign * raw[axis] for axis, sign in axis_map]


def solve(a, b):
    """Gauss elimination for small dense systems."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(m[r][i]))
        if abs(m[p][i]) < 1e-12:
            raise SystemExit("the samples do not span a full turn: the ellipse fit is singular")
        m[i], m[p] = m[p], m[i]
        for r in range(i + 1, n):
            f = m[r][i] / m[i][i]
            for c in range(i, n + 1):
                m[r][c] -= f * m[i][c]
    x = [0.0] * n
    for i in range(n - 1, -1, -1):
        x[i] = (m[i][n] - sum(m[i][c] * x[c] for c in range(i + 1, n))) / m[i][i]
    return x


def fit_ellipse(points):
    """Least squares conic a x^2 + b xy + c y^2 + d x + e y = 1; returns centre and the matrix M with
    (p - centre)^T M (p - centre) = 1."""
    rows = [[x * x, x * y, y * y, x, y] for x, y in points]
    ata = [[sum(r[i] * r[j] for r in rows) for j in range(5)] for i in range(5)]
    atb = [sum(r[i] for r in rows) for i in range(5)]
    a, b, c, d, e = solve(ata, atb)
    # centre: gradient zero
    det = 4 * a * c - b * b
    if det <= 0:
        raise SystemExit("the fit is not an ellipse: turn the box through a full circle")
    cx = (b * e - 2 * c * d) / det
    cy = (b * d - 2 * a * e) / det
    scale = 1 + a * cx * cx + b * cx * cy + c * cy * cy   # f at the centre: conic equals scale
    return (cx, cy), [[a / scale, b / (2 * scale)], [b / (2 * scale), c / scale]]


def matrix_sqrt_2x2(m):
    """Principal square root of a symmetric positive definite 2 x 2 matrix."""
    a, b, c = m[0][0], m[0][1], m[1][1]
    tr, det = a + c, a * c - b * b
    s = math.sqrt(det)
    t = math.sqrt(tr + 2 * s)
    return [[(a + s) / t, b / t], [b / t, (c + s) / t]]


def calibration(samples, axis_map):
    pts = []
    for raw in samples:
        bx, by, _ = box_frame(raw, axis_map)
        pts.append((bx, by))
    centre, m = fit_ellipse(pts)
    soft = matrix_sqrt_2x2(m)   # maps the ellipse to the unit circle
    return centre, soft


def heading_of(raw, axis_map, centre, soft, offset_z=0.0):
    bx, by, _ = box_frame(raw, axis_map)
    hx = soft[0][0] * (bx - centre[0]) + soft[0][1] * (by - centre[1])
    hy = soft[1][0] * (bx - centre[0]) + soft[1][1] * (by - centre[1])
    return math.degrees(math.atan2(-hy, hx))


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def residual(samples, psi_wire, axis_map, centre, soft):
    """Worst deviation of the compass heading from the gyro heading, after removing their constant offset.
    The compass is counter-clockwise positive and the wire heading clockwise, so compare with the sign flipped."""
    diffs = [wrap(heading_of(raw, axis_map, centre, soft) - (-psi)) for raw, psi in zip(samples, psi_wire)]
    ref = sorted(diffs)[len(diffs) // 2]
    return max(abs(wrap(d - ref)) for d in diffs)


def span_deg(psi_wire):
    return max(psi_wire) - min(psi_wire)


def run(samples, psi_wire, axis_map, min_span, as_json, out=print):
    if span_deg(psi_wire) < min_span:
        out(f"fit-mag-cal: the gyro heading spans only {span_deg(psi_wire):.0f} deg; turn the box a full 360: FAIL")
        return False
    centre, soft = calibration(samples, axis_map)
    worst = residual(samples, psi_wire, axis_map, centre, soft)
    # The offset is in the box frame, three values; the vertical offset is not observable in a horizontal turn.
    offset = [round(centre[0], 2), round(centre[1], 2), 0.0]
    params = {"mag_cal": {"offset": offset,
                          "soft_iron": [[round(soft[0][0], 6), round(soft[0][1], 6)],
                                        [round(soft[1][0], 6), round(soft[1][1], 6)]]}}
    ok = worst <= 3.0
    if as_json:
        out(json.dumps(params))
    else:
        out("mag_cal " + json.dumps(params["mag_cal"]))
    out(f"fit-mag-cal: {len(samples)} samples, residual against the gyro heading {worst:.2f} deg (limit 3): "
        f"{'PASS' if ok else 'FAIL'}")
    return ok


def load(path):
    samples, psi = [], []
    with open(path) as f:
        for line in f:
            m = json.loads(line) if line.strip() else {}
            if m.get("type") == "tlm" and "mag_raw" in m and m.get("psi_deg") is not None:
                samples.append(m["mag_raw"])
                psi.append(m["psi_deg"])
    if not samples:
        raise SystemExit("fit-mag-cal: no tlm frame with mag_raw (turn bench_mode on)")
    return samples, psi


def selftest():
    sink = lambda *_: None
    problems = []
    axis_map = parse_map("+x,-y,-z")
    # A distorted, offset circle traced by a box turned clockwise (wire psi grows).
    offset = (60.0, -35.0)
    distort = ((1.15, 0.12), (-0.05, 0.85))
    samples, psi = [], []
    for k in range(400):
        p = k * 360.0 / 399   # wire clockwise degrees
        a = math.radians(-p)   # internal counter-clockwise angle
        bx, by = 400 * math.cos(a), -400 * math.sin(a)
        mx = distort[0][0] * bx + distort[0][1] * by + offset[0]
        my = distort[1][0] * bx + distort[1][1] * by + offset[1]
        # box = (+sx, -sy, -sz) -> sensor = (mx, -my, ...)
        samples.append([int(round(mx)), int(round(-my)), -120])
        psi.append(p)
    if not run(samples, psi, axis_map, 270, False, out=sink):
        problems.append("a distorted full turn did not calibrate to within 3 deg")
    centre, soft = calibration(samples, axis_map)
    if abs(centre[0] - offset[0]) > 1.5 or abs(centre[1] - offset[1]) > 1.5:
        problems.append(f"offset recovered as {centre}, expected {offset}")
    # Positive controls: a half turn, a wrong axis map, and a flat spot in the data must each be rejected.
    half = [(s, p) for s, p in zip(samples, psi) if p < 150]
    if run([s for s, _ in half], [p for _, p in half], axis_map, 270, False, out=sink):
        problems.append("a 150 degree turn was accepted")
    if run(samples, psi, parse_map("+x,+y,-z"), 270, False, out=sink):
        problems.append("a mirrored axis map was accepted")
    constant = [[100, 100, 100]] * 400
    try:
        run(constant, psi, axis_map, 270, False, out=sink)
        problems.append("a constant field was accepted")
    except SystemExit:
        pass
    if problems:
        print("fit-mag-cal selftest: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("fit-mag-cal selftest: PASSED, a distorted circle calibrates; a half turn, a mirrored map and a flat field are rejected")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--map", default="+x,-y,-z", help="the axis map in force (FD section 10.2)")
    ap.add_argument("--min-span", type=float, default=270.0, help="least gyro-heading span, deg")
    ap.add_argument("--json", action="store_true", help="print only the set_params object")
    a = ap.parse_args()
    samples, psi = load(a.capture)
    return 0 if run(samples, psi, parse_map(a.map), a.min_span, a.json) else 1


if __name__ == "__main__":
    sys.exit(main())
