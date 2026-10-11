#!/usr/bin/env python3
"""Magnetometer (T-9) and sun-sensor (T-10) calibration for the DirecSun box.

    tools/calibrate.py mag [--url ws://direcsun.local/ws] [--seconds 40] [--axis-map +x,-y,-z] [--apply]
    tools/calibrate.py sun [--url ...] [--apply]
    tools/calibrate.py --selftest

`mag`: the box stays in SAFE with the motor off. Turn it by hand through a full
circle, level, once or twice, slowly, while this records `mag_raw` for --seconds.
It fits the hard-iron offset (per sensor axis, counts) and the 2 x 2 soft-iron
matrix in the box's horizontal plane (FD section 11.3) and prints how round the
corrected circle is. With --apply it sends `set_params` with `mag_axis_map` and
`mag_cal`; the box then starts the heading from the compass at its next SAFE
magnetometer step, and drops the HEADING_GYRO flag.

`sun`: you point a lamp at known bearings from the box's pointing axis (clockwise
positive, as the UI shows it) and type each one; the tool averages `sun_a_mv`
and `sun_b_mv` for 1 s at each, builds the table d = (A - B) / (A + B) against
the angle (at most 8 points, d strictly increasing, the angle strictly
monotonic: T-10 wants it monotonic across +-30 deg), and with --apply sends
`sun_cal` and a `light_threshold` of half the dimmest A + B.

Both read the bench fields (`mag_raw`, `sun_a_mv`, `sun_b_mv`), which the box
sends only while `bench_mode` is on. The tool turns bench mode on for the run and
off after, and it never sends `arm`: it cannot turn the motor. T-9 asks for the
fit at three wheel speeds; spinning the wheel is a separate step that needs
Julian's explicit go, and runs this tool only for the recording.

--selftest checks the fits on synthetic data with a known offset and stretch,
checks that bad sun tables are refused with a reason, and that the box (the mock)
accepts what the tool would send and refuses a corrupted one.
"""
import argparse
import asyncio
import importlib.util
import json
import math
import os
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

try:
    from websockets.asyncio.client import connect
except ImportError:
    if not os.environ.get("DIRECSUN_REEXEC"):
        for candidate in (os.environ.get("DIRECSUN_PYTHON"), str(REPO / ".venv" / "bin" / "python")):
            if candidate and os.path.exists(candidate):
                os.environ["DIRECSUN_REEXEC"] = "1"
                os.execv(candidate, [candidate, *sys.argv])
    sys.exit("calibrate: needs websockets; run tools/bootstrap-venv.sh or set DIRECSUN_PYTHON")

SUN_CAL_MAX_POINTS = 8   # kSunCalMaxPoints in the firmware
MIN_MAG_SAMPLES = 150    # at 50 Hz: 3 s of motion
MIN_COVERAGE_DEG = 330.0
DEFAULT_AXIS_MAP = ("+x", "-y", "-z")


# ---- pure fits ---------------------------------------------------------------

def axis_index(name):
    """'+x' -> (0, +1); '-z' -> (2, -1)."""
    return "xyz".index(name[1]), -1 if name[0] == "-" else 1


def fit_mag(samples, axis_map=DEFAULT_AXIS_MAP):
    """Hard-iron offset (sensor axes) and horizontal soft-iron matrix from raw samples.

    `samples` are (x, y, z) counts taken while the box turned through a circle. The
    offset is the midpoint of each sensor axis; the soft-iron matrix M maps the
    box-frame (X, Y) of the centred samples onto a circle of the same mean radius,
    which is what the firmware applies after the offset and the axis map.
    Returns (offset, soft, report) with report = {"coverage_deg", "roundness", "samples"}.
    """
    if len(samples) < MIN_MAG_SAMPLES:
        raise ValueError(f"only {len(samples)} samples; need at least {MIN_MAG_SAMPLES}: turn the box slowly for longer")
    offset = [(max(s[i] for s in samples) + min(s[i] for s in samples)) / 2.0 for i in range(3)]
    ix, sx = axis_index(axis_map[0])
    iy, sy = axis_index(axis_map[1])
    pts = [(sx * (s[ix] - offset[ix]), sy * (s[iy] - offset[iy])) for s in samples]
    # The offset is per sensor axis and does not depend on the sign of the map.
    mean_r = sum(math.hypot(x, y) for x, y in pts) / len(pts)
    if mean_r < 1.0:
        raise ValueError("no horizontal field: is the sensor level and the axis map right?")
    # Coverage: the largest gap in the bearing of the samples.
    bearings = sorted(math.degrees(math.atan2(y, x)) % 360.0 for x, y in pts)
    gaps = [b - a for a, b in zip(bearings, bearings[1:])] + [bearings[0] + 360.0 - bearings[-1]]
    coverage = 360.0 - max(gaps)
    if coverage < MIN_COVERAGE_DEG:
        raise ValueError(f"the box turned through only {coverage:.0f} deg; need at least {MIN_COVERAGE_DEG:.0f}: "
                         "complete the circle")
    # Covariance of the centred points, then M = s * Sigma^(-1/2).
    n = len(pts)
    a = sum(x * x for x, _ in pts) / n
    b = sum(x * y for x, y in pts) / n
    c = sum(y * y for _, y in pts) / n
    tr, det = a + c, a * c - b * b
    disc = math.sqrt(max(0.0, tr * tr / 4.0 - det))
    l1, l2 = tr / 2.0 + disc, tr / 2.0 - disc
    if l2 <= 1e-9 * max(l1, 1.0):
        raise ValueError("the samples lie on a line: turn the box through a full circle, not back and forth")
    theta = 0.5 * math.atan2(2.0 * b, a - c)
    ct, st = math.cos(theta), math.sin(theta)
    # Sigma^(-1/2) = R diag(l1^-1/2, l2^-1/2) R^T
    p1, p2 = l1 ** -0.5, l2 ** -0.5
    m = [[ct * ct * p1 + st * st * p2, ct * st * (p1 - p2)],
         [ct * st * (p1 - p2), st * st * p1 + ct * ct * p2]]
    scaled = [(m[0][0] * x + m[0][1] * y, m[1][0] * x + m[1][1] * y) for x, y in pts]
    k = mean_r / (sum(math.hypot(x, y) for x, y in scaled) / n)
    soft = [[m[0][0] * k, m[0][1] * k], [m[1][0] * k, m[1][1] * k]]
    radii = [math.hypot(soft[0][0] * x + soft[0][1] * y, soft[1][0] * x + soft[1][1] * y) for x, y in pts]
    mean_corrected = sum(radii) / n
    roundness = (max(radii) - min(radii)) / mean_corrected
    return offset, soft, {"coverage_deg": coverage, "roundness": roundness, "samples": n,
                          "mean_radius": mean_corrected}


def mag_params(offset, soft, axis_map):
    return {"mag_axis_map": list(axis_map),
            "mag_cal": {"offset": [round(v, 2) for v in offset],
                        "soft_iron": [[round(v, 5) for v in row] for row in soft]}}


def build_sun_cal(points):
    """Table for set_params from (bearing_deg clockwise, A_mv, B_mv) measurements.

    Returns (sun_cal, light_threshold) or raises ValueError naming the problem, so a
    lamp that was bumped or a sensor that saturated is caught here, not by the box.
    """
    if len(points) < 2:
        raise ValueError("need at least 2 bearings")
    rows = []
    for angle, a, b in points:
        total = a + b
        if total <= 0:
            raise ValueError(f"bearing {angle}: both sensors read 0 mV: is the lamp on and the cable plugged in?")
        rows.append((angle, (a - b) / total, total))
    rows.sort(key=lambda r: r[1])
    if len(rows) > SUN_CAL_MAX_POINTS:
        raise ValueError(f"{len(rows)} bearings; the box stores at most {SUN_CAL_MAX_POINTS}")
    for (a0, d0, _), (a1, d1, _) in zip(rows, rows[1:]):
        if not d1 > d0:
            raise ValueError(f"bearings {a0} and {a1} read the same d ({d0:.3f}): the sensors cannot tell them apart")
    rising = rows[1][0] > rows[0][0]
    for (a0, d0, _), (a1, d1, _) in zip(rows, rows[1:]):
        if (a1 <= a0) if rising else (a1 >= a0):
            raise ValueError(f"d {d0:.3f} to {d1:.3f} turns the angle back from {a0} to {a1}: "
                             "the response is not monotonic (T-10): check the lamp position and the load resistor")
    table = [{"d": round(d, 4), "deg": angle} for angle, d, _ in rows]
    threshold = round(0.5 * min(total for _, _, total in rows))
    return table, threshold


def sun_params(table, threshold):
    return {"sun_cal": table, "light_threshold": threshold}


# ---- live ---------------------------------------------------------------------

class Session:
    def __init__(self, ws):
        self.ws = ws
        self.seq = 0
        self.latest = None

    async def command(self, name, **fields):
        self.seq += 1
        await self.ws.send(json.dumps({"v": 1, "type": "cmd", "seq": self.seq, "cmd": name, **fields}))
        while True:
            frame = json.loads(await asyncio.wait_for(self.ws.recv(), timeout=5.0))
            if frame.get("type") == "tlm":
                self.latest = frame
            elif frame.get("type") == "ack" and frame.get("seq") == self.seq:
                return frame

    async def set_params(self, params):
        ack = await self.command("set_params", params=params)
        if not ack["ok"]:
            raise RuntimeError(f"the box refused set_params: {ack.get('reason')}")

    async def tlm_for(self, seconds):
        """Telemetry frames for `seconds`."""
        frames = []
        end = asyncio.get_running_loop().time() + seconds
        while asyncio.get_running_loop().time() < end:
            try:
                frame = json.loads(await asyncio.wait_for(self.ws.recv(), timeout=0.5))
            except asyncio.TimeoutError:
                continue
            if frame.get("type") == "tlm":
                self.latest = frame
                frames.append(frame)
        return frames


async def bench_on(session):
    await session.tlm_for(0.3)
    if session.latest is None or session.latest["mode"] != "SAFE":
        raise RuntimeError("the box is not in SAFE: calibration never runs armed")
    await session.set_params({"bench_mode": True})
    await session.tlm_for(0.3)
    if "mag_raw" not in session.latest:
        raise RuntimeError("no bench fields in tlm: bench mode did not turn on")


async def live_mag(args):
    async with connect(args.url) as ws:
        session = Session(ws)
        await bench_on(session)
        print(f"recording {args.seconds:.0f} s: turn the box slowly through a full circle, level ...", flush=True)
        frames = await session.tlm_for(args.seconds)
        samples = [tuple(f["mag_raw"]) for f in frames if f.get("mag_fresh", True) and "mag_raw" in f]
        axis_map = tuple(args.axis_map.split(","))
        offset, soft, report = fit_mag(samples, axis_map)
        print(f"{report['samples']} samples, {report['coverage_deg']:.0f} deg covered")
        print(f"offset {[round(v, 1) for v in offset]} counts, soft-iron {[[round(v, 4) for v in r] for r in soft]}")
        print(f"corrected circle: radius {report['mean_radius']:.0f} counts, out of round by "
              f"{100 * report['roundness']:.1f} % (about {math.degrees(math.asin(min(1.0, report['roundness'] / 2))):.1f} deg "
              "of heading error; T-9 wants within 3 deg after calibration)")
        params = mag_params(offset, soft, axis_map)
        if args.apply:
            await session.set_params({"bench_mode": False, **params})
            print("applied: the box now starts its heading from the compass in SAFE")
        else:
            await session.set_params({"bench_mode": False})
            print("not applied (use --apply); this is what would be sent:")
            print(json.dumps(params))


async def live_sun(args):
    async with connect(args.url) as ws:
        session = Session(ws)
        await bench_on(session)
        points = []
        print("point the lamp at a known bearing from the box's pointing axis (clockwise positive, degrees);")
        print("type the bearing and press Enter, or just Enter to finish.", flush=True)
        loop = asyncio.get_running_loop()
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line.strip():
                break
            try:
                angle = float(line)
            except ValueError:
                print("not a number")
                continue
            frames = await session.tlm_for(1.0)
            a = sum(f["sun_a_mv"] for f in frames) / len(frames)
            b = sum(f["sun_b_mv"] for f in frames) / len(frames)
            points.append((angle, a, b))
            print(f"  {angle:+.1f} deg: A {a:.0f} mV, B {b:.0f} mV, d {(a - b) / max(1.0, a + b):+.3f}", flush=True)
        table, threshold = build_sun_cal(points)
        print(json.dumps(sun_params(table, threshold)))
        if args.apply:
            await session.set_params({"bench_mode": False, **sun_params(table, threshold)})
            print("applied")
        else:
            await session.set_params({"bench_mode": False})
            print("not applied (use --apply)")


# ---- selftest ----------------------------------------------------------------

def synthetic_mag(offset, stretch, angle_deg, n=400, radius=420.0, hard_z=-35.0):
    """Raw samples of a box turning once, with the default axis map (+x, -y, -z)."""
    ca, sa = math.cos(math.radians(angle_deg)), math.sin(math.radians(angle_deg))
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        bx, by = radius * math.cos(t), radius * math.sin(t)
        # soft iron: stretch along a rotated axis
        u, v = ca * bx + sa * by, -sa * bx + ca * by
        u, v = u * stretch[0], v * stretch[1]
        bx, by = ca * u - sa * v, sa * u + ca * v
        # default map: box X = +x, Y = -y; the hard-iron offset is in sensor axes
        out.append((bx + offset[0], -by + offset[1], 300.0 + hard_z + offset[2]))
    return out


def selftest():
    problems = []
    offset_true = (140.0, -95.0, 60.0)
    samples = synthetic_mag(offset_true, (1.25, 0.8), 30.0)
    offset, soft, report = fit_mag(samples)
    for axis in (0, 1):
        if abs(offset[axis] - offset_true[axis]) > 2.0:
            problems.append(f"hard-iron axis {axis}: fitted {offset[axis]:.1f}, true {offset_true[axis]}")
    if report["roundness"] > 0.02:
        problems.append(f"the corrected circle is {100 * report['roundness']:.1f} % out of round")
    raw_spread = max(math.hypot(x - offset[0], y - offset[1]) for x, y, _ in samples) \
        / min(math.hypot(x - offset[0], y - offset[1]) for x, y, _ in samples)
    if raw_spread < 1.4:
        problems.append("the synthetic data was not stretched: the control proves nothing")
    # An uncalibrated stretch gives a heading error; the fit removes it.
    heading_errors = []
    for x, y, _ in samples:
        bx, by = x - offset[0], -(y - offset[1])
        cx, cy = soft[0][0] * bx + soft[0][1] * by, soft[1][0] * bx + soft[1][1] * by
        heading_errors.append(abs(((math.degrees(math.atan2(by, bx)) - math.degrees(math.atan2(cy, cx)) + 180) % 360) - 180))
    print(f"calibrate selftest: hard iron ({offset[0]:.1f}, {offset[1]:.1f}) for ({offset_true[0]}, {offset_true[1]}), "
          f"{100 * report['roundness']:.2f} % out of round (raw {raw_spread:.2f}:1), "
          f"largest correction {max(heading_errors):.1f} deg")
    # Positive control: a half circle and a back-and-forth line are refused.
    for name, bad in (("half circle", samples[:200]), ("line", [(500.0 + 100.0 * math.sin(i / 7.0), 0.0, 0.0)
                                                              for i in range(400)])):
        try:
            fit_mag(bad)
            problems.append(f"a {name} was accepted")
        except ValueError:
            pass
    try:
        fit_mag(samples[:20])
        problems.append("20 samples were accepted")
    except ValueError:
        pass

    # Sun table: a sensible response builds, a sloppy one is refused with a reason.
    good = [(a, 1000 + 12 * a, 1000 - 12 * a) for a in (-40, -20, 0, 20, 40)]
    table, threshold = build_sun_cal(good)
    if [p["deg"] for p in table] != [-40, -20, 0, 20, 40] or threshold != 1000:
        problems.append(f"good sun table came out as {table} / {threshold}")
    for name, bad in (("a bumped lamp", good[:2] + [(0, 1500, 500)] + good[3:]),
                      ("a dark pair", good + [(60, 0, 0)]),
                      ("nine bearings", [(a, 1000 + 8 * a, 1000 - 8 * a) for a in range(-40, 41, 10)]),
                      ("identical readings", good + [(10, 1000, 1000)])):
        try:
            build_sun_cal(bad)
            problems.append(f"{name} was accepted")
        except ValueError:
            pass

    # The box accepts what the tool sends, and refuses a corrupted calibration.
    spec = importlib.util.spec_from_file_location("mock_box", REPO / "tools" / "mock-box.py")
    mock = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mock)

    class Conn:
        pass

    box = mock.Box(mock.Schema(), {"throttle"})
    conn = Conn()

    def send(seq, params):
        box.receive(conn, json.dumps({"v": 1, "type": "cmd", "seq": seq, "cmd": "set_params", "params": params}))
        return next(m for _t, m in reversed(box.outbox) if m["type"] == "ack" and m["seq"] == seq)

    ack = send(1, {**mag_params(offset, soft, DEFAULT_AXIS_MAP), **sun_params(table, threshold)})
    if not ack["ok"]:
        problems.append(f"the box refused the tool's calibration: {ack.get('reason')}")
    broken = mag_params(offset, [[1.0, 2.0], [0.5, 1.0]], DEFAULT_AXIS_MAP)
    if send(2, broken)["ok"]:
        problems.append("the box accepted a singular soft-iron matrix")
    if send(3, {"sun_cal": list(reversed(table))})["ok"]:
        problems.append("the box accepted a sun table with d decreasing")

    for problem in problems:
        print(f"calibrate selftest: FAILED, {problem}", file=sys.stderr)
    if not problems:
        print("calibrate selftest: PASSED (fits recover the offset and the stretch; bad data refused; "
              "the box accepts the tool's calibration and refuses a corrupted one)")
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("mode", nargs="?", choices=("mag", "sun"))
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--seconds", type=float, default=40.0, help="mag: how long to record")
    parser.add_argument("--axis-map", default=",".join(DEFAULT_AXIS_MAP),
                        help="mag: box X,Y,Z as sensor axes (default: the flipped top deck)")
    parser.add_argument("--apply", action="store_true", help="send the result to the box (default: print it)")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        sys.exit(selftest())
    if args.mode is None:
        parser.error("choose mag or sun, or --selftest")
    try:
        asyncio.run(live_mag(args) if args.mode == "mag" else live_sun(args))
    except (ValueError, RuntimeError) as exc:
        sys.exit(f"calibrate: {exc}")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
