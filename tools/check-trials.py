#!/usr/bin/env python3
"""Score the hardware trials of R4.4 and R4.5 from a capture of the box's tlm.

    tools/check-trials.py detumble  capture.jsonl [--min-trials 10] [--min-per-direction 5]
    tools/check-trials.py rampdown  capture.jsonl
    tools/check-trials.py slew      capture.jsonl [--legs 20]
    tools/check-trials.py inertia   --r-mm 50 --m1-g 10 --m2-g 20 --m1-runs a.jsonl ... --m2-runs b.jsonl ...
    tools/check-trials.py friction  --ib 1.03e-3 [--iw 6.5e-5] run.jsonl ...
    tools/check-trials.py noise     capture.jsonl [--max-drift 0.5] [--max-rms 0.1]
    tools/check-trials.py vibration capture.jsonl [--max 0.05]
    tools/check-trials.py maxima    capture.jsonl ...
    tools/check-trials.py --selftest

A capture is what `tools/capture-box.py --silent --out` writes: one wire message per
line. Only `tlm` frames are read. Every angle and rate is on the wire frame
(clockwise positive). Exit 0 only if every criterion holds. `--selftest` runs each
subcommand on synthetic traces with a known answer and on planted failures that
must be rejected, because a checker that cannot fail proves nothing (TD section 9).
"""
import argparse
import json
import math
import statistics
import sys

SETTLE_BAND_DEG = 3.0
SLEW_SETTLE_S = 3.0
SLEW_TIMEOUT_S = 6.0
DETUMBLE_PEAK_MIN, DETUMBLE_PEAK_MAX = 270.0, 450.0
DETUMBLE_QUIET_DPS = 5.0
# MOMENTUM_SATURATED this long after the peak means the spin was more than the
# wheel can absorb; earlier, it is the wheel lagging its reference (R4.4 Notes).
DETUMBLE_SAT_GRACE_S = 1.0
GYRO_COUNTS_PER_DPS = 16.4   # MPU-6050 at +-2000 deg/s (FD section 10.1)


def load(path):
    frames = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            m = json.loads(line)
            if m.get("type") == "tlm":
                frames.append(m)
    frames.sort(key=lambda m: m["t_ms"])
    return frames


def fit_line(xs, ys):
    """Least squares; returns slope, intercept, R^2."""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    slope = sxy / sxx
    icpt = my - slope * mx
    ss_tot = sum((y - my) ** 2 for y in ys)
    ss_res = sum((y - (icpt + slope * x)) ** 2 for x, y in zip(xs, ys))
    return slope, icpt, (1 - ss_res / ss_tot) if ss_tot > 0 else 0.0


# ---------------------------------------------------------------- detumble (T-15)

def excursions(frames):
    """Index ranges where |rate| is at or above the quiet band, split by 1 s of quiet."""
    out, start, last_loud = [], None, None
    for i, m in enumerate(frames):
        loud = abs(m["rate_dps"]) >= DETUMBLE_QUIET_DPS
        if loud:
            if start is None:
                start = i
            last_loud = i
        elif start is not None and m["t_ms"] - frames[last_loud]["t_ms"] >= 1000:
            out.append((start, last_loud))
            start = None
    if start is not None:
        out.append((start, last_loud))
    return out


def score_detumble(frames, min_trials=10, min_per_direction=5, out=print):
    rows, valid, failed = [], 0, 0
    per_dir = {1: 0, -1: 0}
    for a, b in excursions(frames):
        seg = frames[a:b + 1]
        peak_i = max(range(len(seg)), key=lambda k: abs(seg[k]["rate_dps"]))
        peak = abs(seg[peak_i]["rate_dps"])
        t0 = seg[peak_i]["t_ms"]
        direction = 1 if seg[peak_i]["rate_dps"] > 0 else -1
        if not DETUMBLE_PEAK_MIN <= peak <= DETUMBLE_PEAK_MAX:
            rows.append((t0, direction, peak, None, "ignored: peak outside 270..450 deg/s"))
            continue
        window = [m for m in frames if t0 <= m["t_ms"] <= t0 + 10000]
        if not window or window[-1]["t_ms"] < t0 + 9900:
            rows.append((t0, direction, peak, None, "invalid: capture ends before t0 + 10 s"))
            continue
        if any(m["mode"] != "DETUMBLE" for m in window):
            rows.append((t0, direction, peak, None, "invalid: mode left DETUMBLE"))
            continue
        saturated = [m["t_ms"] for m in window if "MOMENTUM_SATURATED" in m.get("flags", [])]
        late = [t for t in saturated if t >= t0 + DETUMBLE_SAT_GRACE_S * 1000]
        if late:
            rows.append((t0, direction, peak, None,
                         f"invalid: MOMENTUM_SATURATED {(late[0] - t0) / 1000.0:.2f} s after the peak"))
            continue
        valid += 1
        per_dir[direction] += 1
        loud = [m for m in window if abs(m["rate_dps"]) >= DETUMBLE_QUIET_DPS]
        t_settle = (loud[-1]["t_ms"] - t0) / 1000.0 if loud else 0.0
        ok = t_settle <= 5.0
        failed += 0 if ok else 1
        result = "pass" if ok else "FAIL: still above 5 deg/s after 5 s"
        if saturated:
            result += f" (MOMENTUM_SATURATED until {(saturated[-1] - t0) / 1000.0:.2f} s)"
        rows.append((t0, direction, peak, t_settle, result))
    out("t0_ms    dir  peak_dps  t_settle_s  result")
    for t0, d, peak, ts, res in rows:
        out(f"{t0:<8} {'cw ' if d > 0 else 'ccw'} {peak:8.1f}  {'' if ts is None else f'{ts:10.2f}'}  {res}")
    ok = failed == 0 and valid >= min_trials and all(per_dir[d] >= min_per_direction for d in per_dir)
    out(f"detumble: {valid} valid trial(s) ({per_dir[1]} cw, {per_dir[-1]} ccw), {failed} failed: "
        f"{'PASS' if ok else 'FAIL'}")
    return ok


# ---------------------------------------------------------------- ramp-down (T-13)

def score_rampdown(frames, out=print):
    """The first armed-to-SAFE transition: the open-loop 5 s ramp of FD section 14.3."""
    for i in range(1, len(frames)):
        if frames[i]["mode"] == "SAFE" and frames[i - 1]["mode"] != "SAFE":
            break
    else:
        out("rampdown: no armed-to-SAFE transition in the capture: FAIL")
        return False
    t_cmd = frames[i]["t_ms"]
    t0v = frames[i - 1]["throttle"]
    if t0v < 48:
        out("rampdown: throttle was already zero before SAFE: FAIL")
        return False
    after = [m for m in frames[i:] if m["t_ms"] <= t_cmd + 7000]
    start = next((m for m in after if m["throttle"] < 0.97 * t0v), None)
    zero = next((m for m in after if m["throttle"] == 0), None)
    if start is None or zero is None:
        out("rampdown: throttle never fell to zero in 7 s: FAIL")
        return False
    start_s = (start["t_ms"] - t_cmd) / 1000.0
    ramp_s = (zero["t_ms"] - start["t_ms"]) / 1000.0
    worst = 0.0
    for m in after:
        if m["t_ms"] > zero["t_ms"]:
            break
        frac = 1 - (m["t_ms"] - t_cmd) / 5000.0
        expect = t0v * frac
        if expect < 48:
            expect = 0
        worst = max(worst, abs(m["throttle"] - expect) / t0v)
    ok = start_s <= 1.0 and abs((zero["t_ms"] - t_cmd) / 1000.0 - 5.0) <= 0.3 and worst <= 0.10
    out(f"rampdown: from {t0v}, starts {start_s:.2f} s after SAFE, zero {(zero['t_ms'] - t_cmd) / 1000.0:.2f} s after, "
        f"ramp span {ramp_s:.2f} s, worst deviation from linear {worst * 100:.1f} %: {'PASS' if ok else 'FAIL'}")
    return ok


# ---------------------------------------------------------------- slew (T-16)

def score_slew(frames, legs=20, out=print):
    segs, i = [], 0
    while i < len(frames):
        if frames[i]["mode"] == "SLEW" and i > 0 and frames[i - 1]["mode"] == "READY":
            j = i
            while j + 1 < len(frames) and frames[j + 1]["mode"] == "SLEW":
                j += 1
            segs.append((i, j))
            i = j + 1
        else:
            i += 1
    rows, bad = [], 0
    for k, (a, b) in enumerate(segs):
        t0 = frames[a - 1]["t_ms"]
        ended_ready = b + 1 < len(frames) and frames[b + 1]["mode"] == "READY"
        target = frames[b]["psi_ref_deg"]
        end_t = frames[segs[k + 1][0] - 1]["t_ms"] if k + 1 < len(segs) else \
            (frames[b + 1]["t_ms"] + 1500 if ended_ready else frames[b]["t_ms"])
        window = [m for m in frames if t0 <= m["t_ms"] <= end_t]
        loud = [m for m in window if abs(m["psi_deg"] - target) > SETTLE_BAND_DEG]
        t_settle = (loud[-1]["t_ms"] - t0) / 1000.0 if loud else 0.0
        dur = (frames[b]["t_ms"] - t0) / 1000.0
        ok = ended_ready and dur < SLEW_TIMEOUT_S - 0.1 and t_settle <= SLEW_SETTLE_S
        bad += 0 if ok else 1
        rows.append((t0, target, dur, t_settle, ok, "" if ended_ready else "did not end in READY"))
    out("t0_ms    target_deg  slew_s  t_settle_s  result")
    for t0, tgt, dur, ts, ok, why in rows:
        out(f"{t0:<8} {tgt:10.1f}  {dur:6.2f}  {ts:10.2f}  {'pass' if ok else 'FAIL ' + why}")
    ok = bad == 0 and len(rows) >= legs
    out(f"slew: {len(rows)} leg(s), {bad} failed (need {legs}): {'PASS' if ok else 'FAIL'}")
    return ok


# ---------------------------------------------------------------- inertia (T-3)

def alpha_of(frames):
    start = next((k for k, m in enumerate(frames) if abs(m["rate_dps"]) > 10), None)
    if start is None:
        raise SystemExit("inertia: no motion above 10 deg/s in the run")
    t0 = frames[start]["t_ms"]
    seg = [m for m in frames[start:] if m["t_ms"] <= t0 + 1500]
    xs = [(m["t_ms"] - t0) / 1000.0 for m in seg]
    ys = [math.radians(m["rate_dps"]) for m in seg]
    slope, _, r2 = fit_line(xs, ys)
    return abs(slope), r2


def inertia_from(alpha1, alpha2, m1, m2, r, g=9.81):
    return ((m2 - m1) * g * r - (alpha2 * m2 - alpha1 * m1) * r * r) / (alpha2 - alpha1)


def score_inertia(runs1, runs2, m1_g, m2_g, r_mm, out=print):
    r, m1, m2 = r_mm / 1000.0, m1_g / 1000.0, m2_g / 1000.0
    ok = True
    alphas = []
    for label, runs in (("m1", runs1), ("m2", runs2)):
        vals = []
        for path_frames in runs:
            a, r2 = alpha_of(path_frames)
            vals.append(a)
            good = r2 >= 0.99
            ok &= good
            out(f"{label}: alpha {a:.3f} rad/s^2, R^2 {r2:.4f}{'' if good else '  FAIL: R^2 under 0.99, repeat the run'}")
        alphas.append(statistics.median(vals))
    ib = inertia_from(alphas[0], alphas[1], m1, m2, r)
    inband = 0.72e-3 <= ib <= 1.34e-3
    out(f"inertia: I_b = {ib:.4e} kg m^2 (TD band 0.72e-3 to 1.34e-3): {'PASS' if ok and inband else 'FAIL'}")
    return ok and inband


# ---------------------------------------------------------------- friction (T-4)

def friction_of(frames, ib, iw):
    peak_i = max(range(len(frames)), key=lambda k: abs(frames[k]["rate_dps"]))
    sign = 1 if frames[peak_i]["rate_dps"] > 0 else -1
    seg = [m for m in frames[peak_i:] if 20 <= sign * m["rate_dps"] <= 100]
    if len(seg) < 5:
        raise SystemExit("friction: no decay between 100 and 20 deg/s in the run")
    xs = [m["t_ms"] / 1000.0 for m in seg]
    ys = [math.radians(sign * m["rate_dps"]) for m in seg]
    slope, _, r2_lin = fit_line(xs, ys)
    _, _, r2_exp = fit_line(xs, [math.log(y) for y in ys])
    return (ib + iw) * abs(slope) * 1000.0, r2_lin, r2_exp


def score_friction(runs, ib, iw, out=print):
    vals = []
    for fr in runs:
        t, rl, re = friction_of(fr, ib, iw)
        vals.append(t)
        out(f"friction: {t:.3f} mN m  (linear R^2 {rl:.3f}, exponential R^2 {re:.3f}: "
            f"{'Coulomb-like' if rl >= re else 'viscous-like'})")
    med = statistics.median(vals)
    ok = med < 2.0
    out(f"friction: median {med:.3f} mN m, worst {max(vals):.3f} (TD limit 2): {'PASS' if ok else 'FAIL'}")
    return ok


# ---------------------------------------------------------------- noise, vibration (T-8, T-7)

def score_noise(frames, max_drift, max_rms, out=print):
    if not frames or "gyro_raw" not in frames[0]:
        raise SystemExit("noise: the capture has no gyro_raw field (bench_mode must be on)")
    t0 = frames[0]["t_ms"]
    blocks = {}
    for m in frames:
        blocks.setdefault(int((m["t_ms"] - t0) // 10000), []).append(m["gyro_raw"][2] / GYRO_COUNTS_PER_DPS)
    full = [v for k, v in sorted(blocks.items()) if len(v) >= 400]
    if len(full) < 6:
        raise SystemExit(f"noise: need six full 10 s blocks, have {len(full)}")
    means = [statistics.fmean(b) for b in full]
    drift = max(means) - min(means)
    sq = [(x - mu) ** 2 for b, mu in zip(full, means) for x in b]
    rms = math.sqrt(sum(sq) / len(sq))
    ok = drift < max_drift and rms < max_rms
    out(f"noise: bias drift {drift:.3f} deg/s (limit {max_drift}), noise RMS {rms:.3f} deg/s (limit {max_rms}): "
        f"{'PASS' if ok else 'FAIL'}")
    return ok


def score_vibration(frames, limit, out=print):
    vals = [m["accel_rms_g"] for m in frames if "accel_rms_g" in m]
    if not vals:
        raise SystemExit("vibration: the capture has no accel_rms_g field (bench_mode must be on)")
    mean = statistics.fmean(vals)
    ok = 0 < mean < limit
    out(f"vibration: mean accel_rms_g {mean:.4f} over {len(vals)} frames (limit {limit}): {'PASS' if ok else 'FAIL'}")
    return ok


def score_maxima(captures, out=print):
    top, events = 0.0, 0
    for fr in captures:
        top = max([top] + [m["wheel_rpm"] for m in fr if m.get("wheel_rpm") is not None])
        events += sum(1 for m in fr if m["mode"] == "SAFE" and any(f in m.get("flags", []) for f in
                      ("OVERSPEED", "UNDERSPEED", "BATTERY_LOW", "IMU_SILENT", "TILT", "HALL_SILENT", "CONTROL_STALL")))
    ok = top < 4000
    out(f"maxima: max wheel_rpm {top:.0f} (must be under 4000), frames with a latched fault: {events}: "
        f"{'PASS' if ok else 'FAIL'}")
    return ok


# ---------------------------------------------------------------- self-test

def _tlm(t_ms, **kw):
    m = {"type": "tlm", "t_ms": t_ms, "mode": "READY", "rate_dps": 0.0, "psi_deg": 0.0, "psi_ref_deg": None,
         "throttle": 0, "wheel_rpm": 2400, "flags": []}
    m.update(kw)
    return m


def _detumble_trace(n, settle_s, peak=360.0, saturated_s=()):
    frames, t = [], 0
    for k in range(n):
        sign = 1 if k % 2 == 0 else -1
        for _ in range(100):   # 2 s of rest
            frames.append(_tlm(t, mode="DETUMBLE")); t += 20
        for step in range(600):   # 12 s: spin, decay linearly to rest
            sec = step * 0.02
            r = peak * max(0.0, 1 - sec / settle_s) if step else peak
            flags = ["MOMENTUM_SATURATED"] if saturated_s and saturated_s[0] <= sec < saturated_s[1] else []
            frames.append(_tlm(t, mode="DETUMBLE", rate_dps=sign * r, flags=flags)); t += 20
    return frames


def selftest():
    sink = lambda *_: None
    problems = []

    def expect(name, got, want):
        if got != want:
            problems.append(f"{name}: got {got}, wanted {want}")

    expect("detumble 10 trials settling in 3 s", score_detumble(_detumble_trace(10, 3.0), out=sink), True)
    expect("detumble slow 6 s", score_detumble(_detumble_trace(10, 6.0), out=sink), False)
    # Tight bracket on the 5 s limit: a linear decay crosses 5 deg/s at settle_s * (1 - 5/360).
    expect("detumble under 5 deg/s at 4.8 s", score_detumble(_detumble_trace(10, 4.87), out=sink), True)
    expect("detumble under 5 deg/s at 5.2 s", score_detumble(_detumble_trace(10, 5.28), out=sink), False)
    expect("detumble too few", score_detumble(_detumble_trace(8, 3.0), out=sink), False)
    expect("detumble peak out of window", score_detumble(_detumble_trace(10, 3.0, peak=500.0), out=sink), False)
    one_sided = _detumble_trace(10, 3.0)
    for m in one_sided:
        m["rate_dps"] = abs(m["rate_dps"])
    expect("detumble one direction only", score_detumble(one_sided, out=sink), False)
    # The wheel lagging its reference: a brief clamp early in every trial still counts.
    expect("detumble with a clamp from 0.3 to 0.5 s",
           score_detumble(_detumble_trace(10, 3.0, saturated_s=(0.3, 0.5)), out=sink), True)
    # A clamp still set at 1.0 s or later: every trial invalid, so the run fails.
    expect("detumble with a clamp from 0.3 to 1.1 s",
           score_detumble(_detumble_trace(10, 3.0, saturated_s=(0.3, 1.1)), out=sink), False)
    expect("detumble with a clamp from 1.5 to 2.0 s",
           score_detumble(_detumble_trace(10, 3.0, saturated_s=(1.5, 2.0)), out=sink), False)

    def ramp(start_delay, span, thr=1000):
        fr = [_tlm(t, throttle=thr) for t in range(0, 1000, 20)]
        t = 1000
        for _ in range(int(start_delay / 0.02)):
            fr.append(_tlm(t, mode="SAFE", throttle=thr)); t += 20
        steps = int(span / 0.02)
        for k in range(steps):
            v = thr * (1 - k / steps)
            fr.append(_tlm(t, mode="SAFE", throttle=int(v) if v >= 48 else 0)); t += 20
        for _ in range(100):
            fr.append(_tlm(t, mode="SAFE", throttle=0)); t += 20
        return fr
    expect("rampdown linear 5 s", score_rampdown(ramp(0.1, 4.9), out=sink), True)
    expect("rampdown 2 s", score_rampdown(ramp(0.1, 1.9), out=sink), False)
    expect("rampdown starts 1.4 s late", score_rampdown(ramp(1.4, 3.6), out=sink), False)

    def slews(settle_s, n=4):
        fr, t = [], 0
        for k in range(n):
            tgt = 90.0 if k % 2 == 0 else 0.0
            frm = 0.0 if k % 2 == 0 else 90.0
            for _ in range(25):
                fr.append(_tlm(t, psi_deg=frm)); t += 20
            for step in range(int((settle_s + 0.5) * 50)):   # SLEW, error falling to 3 deg at settle_s
                sec = step * 0.02
                err = (tgt - frm) * math.exp(-sec * math.log(30.0) / settle_s)
                fr.append(_tlm(t, mode="SLEW", psi_deg=tgt - err, psi_ref_deg=tgt)); t += 20
            for _ in range(75):
                fr.append(_tlm(t, psi_deg=tgt)); t += 20
        return fr
    expect("slew fast", score_slew(slews(2.0), legs=4, out=sink), True)
    expect("slew slow", score_slew(slews(4.5), legs=4, out=sink), False)
    expect("slew settles at 2.5 s", score_slew(slews(2.5), legs=4, out=sink), True)
    expect("slew settles at 3.4 s", score_slew(slews(3.4), legs=4, out=sink), False)
    expect("slew too few legs", score_slew(slews(2.0, n=2), legs=4, out=sink), False)

    # inertia: build runs from the equations of motion with I_b = 1.03e-3 and 2 mN m of friction.
    def inertia_run(m_kg, ib=1.03e-3, tf=2e-3, r=0.05, g=9.81):
        a = (m_kg * g * r - tf) / (ib + m_kg * r * r)
        return [_tlm(t, mode="SAFE", rate_dps=math.degrees(a * t / 1000.0)) for t in range(0, 2500, 20)]
    ok = score_inertia([inertia_run(0.010)] * 3, [inertia_run(0.020)] * 3, 10, 20, 50, out=sink)
    expect("inertia 1.03e-3", ok, True)
    ib = inertia_from(*[alpha_of(inertia_run(m))[0] for m in (0.010, 0.020)], 0.010, 0.020, 0.05)
    expect("inertia recovered within 1 %", abs(ib - 1.03e-3) / 1.03e-3 < 0.01, True)
    expect("inertia 2x too big", score_inertia([inertia_run(0.010, ib=2.1e-3)] * 3,
                                               [inertia_run(0.020, ib=2.1e-3)] * 3, 10, 20, 50, out=sink), False)
    noisy = [dict(m, rate_dps=m["rate_dps"] * (1 + 0.3 * (k % 2))) for k, m in enumerate(inertia_run(0.010))]
    expect("inertia refuses a ragged fit", score_inertia([noisy] * 3, [inertia_run(0.020)] * 3, 10, 20, 50, out=sink), False)

    def decay(tf_nm, ib=1.03e-3, iw=6.5e-5):
        d = tf_nm / (ib + iw)
        return [_tlm(t, mode="SAFE", rate_dps=max(0.0, 115.0 - math.degrees(d) * t / 1000.0)) for t in range(0, 4000, 20)]
    expect("friction 1.5 mN m", score_friction([decay(1.5e-3)] * 3, 1.03e-3, 6.5e-5, out=sink), True)
    t, _, _ = friction_of(decay(1.5e-3), 1.03e-3, 6.5e-5)
    expect("friction recovered within 5 %", abs(t - 1.5) / 1.5 < 0.05, True)
    expect("friction 2.5 mN m", score_friction([decay(2.5e-3)] * 3, 1.03e-3, 6.5e-5, out=sink), False)

    def gyro(noise_dps, drift_dps=0.0):
        import random
        rnd = random.Random(1)
        return [_tlm(t, gyro_raw=[0, 0, round((rnd.gauss(0, noise_dps) + drift_dps * t / 60000.0) * GYRO_COUNTS_PER_DPS)])
                for t in range(0, 60000, 20)]
    expect("noise 0.05 deg/s", score_noise(gyro(0.05), 0.5, 0.1, out=sink), True)
    expect("noise 0.15 deg/s fails the 0.1 limit", score_noise(gyro(0.15), 0.5, 0.1, out=sink), False)
    expect("noise 0.15 deg/s passes the 0.3 limit", score_noise(gyro(0.15), 0.5, 0.3, out=sink), True)
    said = []
    score_noise(gyro(0.15), 0.5, 0.3, out=said.append)
    rms = float(said[-1].split("noise RMS ")[1].split()[0])
    expect(f"noise 0.15 deg/s recovered within 5 % (read {rms})", abs(rms - 0.15) / 0.15 < 0.05, True)
    expect("drift 0.8 deg/s", score_noise(gyro(0.05, drift_dps=1.5), 0.5, 0.1, out=sink), False)
    for name, call in (("noise", lambda: score_noise([_tlm(0)], 0.5, 0.1, out=sink)),
                       ("vibration", lambda: score_vibration([_tlm(0)], 0.05, out=sink))):
        try:
            call()
            problems.append(f"{name} accepted a capture without the bench field")
        except SystemExit:
            pass
    expect("vibration 0.03 g", score_vibration([_tlm(t, accel_rms_g=0.03) for t in range(50)], 0.05, out=sink), True)
    expect("vibration 0.0 g", score_vibration([_tlm(t, accel_rms_g=0.0) for t in range(50)], 0.05, out=sink), False)
    expect("maxima under cap", score_maxima([[_tlm(0, wheel_rpm=3900)]], out=sink), True)
    expect("maxima over cap", score_maxima([[_tlm(0, wheel_rpm=4100)]], out=sink), False)
    if problems:
        print("check-trials selftest: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("check-trials selftest: PASSED, every subcommand recovers its known answer and rejects its planted failure")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("detumble"); d.add_argument("capture")
    d.add_argument("--min-trials", type=int, default=10); d.add_argument("--min-per-direction", type=int, default=5)
    sub.add_parser("rampdown").add_argument("capture")
    s = sub.add_parser("slew"); s.add_argument("capture"); s.add_argument("--legs", type=int, default=20)
    i = sub.add_parser("inertia")
    i.add_argument("--r-mm", type=float, required=True); i.add_argument("--m1-g", type=float, required=True)
    i.add_argument("--m2-g", type=float, required=True)
    i.add_argument("--m1-runs", nargs="+", required=True); i.add_argument("--m2-runs", nargs="+", required=True)
    f = sub.add_parser("friction"); f.add_argument("runs", nargs="+")
    f.add_argument("--ib", type=float, required=True); f.add_argument("--iw", type=float, default=6.5e-5)
    n = sub.add_parser("noise"); n.add_argument("capture")
    n.add_argument("--max-drift", type=float, default=0.5); n.add_argument("--max-rms", type=float, default=0.1)
    v = sub.add_parser("vibration"); v.add_argument("capture"); v.add_argument("--max", type=float, default=0.05)
    sub.add_parser("maxima").add_argument("captures", nargs="+")
    a = ap.parse_args()
    if a.cmd == "detumble":
        ok = score_detumble(load(a.capture), a.min_trials, a.min_per_direction)
    elif a.cmd == "rampdown":
        ok = score_rampdown(load(a.capture))
    elif a.cmd == "slew":
        ok = score_slew(load(a.capture), a.legs)
    elif a.cmd == "inertia":
        ok = score_inertia([load(p) for p in a.m1_runs], [load(p) for p in a.m2_runs], a.m1_g, a.m2_g, a.r_mm)
    elif a.cmd == "friction":
        ok = score_friction([load(p) for p in a.runs], a.ib, a.iw)
    elif a.cmd == "noise":
        ok = score_noise(load(a.capture), a.max_drift, a.max_rms)
    elif a.cmd == "vibration":
        ok = score_vibration(load(a.capture), a.max)
    else:
        ok = score_maxima([load(p) for p in a.captures])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
