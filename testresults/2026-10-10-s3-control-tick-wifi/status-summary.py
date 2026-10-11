"""Summarise the firmware's 1 Hz status lines in a raw serial capture (.txt or .txt.gz).

Usage: python3 status-summary.py <capture>...
Reports, for the last boot in each file: the first second, then the worst tick
(loop_us_max) range after it, seconds over 1000 us, missed ticks, invalid IMU
reads, jitter, ticks per 10 s window, the DShot frame rate, and barometer health.
"""
import gzip, re, sys, statistics
RX = re.compile(r"status: s (\d+) ticks (\d+) missed (\d+) \(total (\d+)\) loop_us_max (\d+) dev_max_us (\d+) p99_dev_us (\d+) dshot sent (\d+) dropped (\d+) imu_invalid (\d+)")
for path in sys.argv[1:]:
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt", errors="replace") as fh:
        text = fh.read()
    lines = text.splitlines()
    ver = re.findall(r"App version: +(\S+)", text)
    rows = [tuple(map(int, m.groups())) for m in map(RX.search, lines) if m]
    boots = [i for i, r in enumerate(rows) if r[0] == 1]
    print(f"== {path}: versions {sorted(set(ver))}, 'rst:0x' lines {text.count('rst:0x')}, status rows {len(rows)}, boots seen (s=1) {len(boots)}")
    if not rows:
        continue
    for b in boots:
        r = rows[b]
        print(f"   boot s1: ticks {r[1]} missed {r[2]} loop_us_max {r[4]} dev_max {r[5]} p99 {r[6]} imu_invalid {r[9]}")
    # use the last boot's run
    start = boots[-1] if boots else 0
    run = rows[start:]
    after = run[1:] if run and run[0][0] == 1 else run
    lm = sorted(x[4] for x in after)
    if not lm:
        continue
    print(f"   last run: {len(run)} s; after s1: loop_us_max min {lm[0]} median {statistics.median(lm)} max {lm[-1]}, >1000: {sum(x > 1000 for x in lm)}")
    print(f"   missed after s1: {sum(x[2] for x in after)}; total counter at end {run[-1][3]}; imu_invalid after s1 {sum(x[9] for x in after)}; dropped at end {run[-1][8]}")
    print(f"   p99 dev max {max(x[6] for x in after)}, dev max {max(x[5] for x in after)}; ticks/s {min(x[1] for x in after)}-{max(x[1] for x in after)}")
    w = [sum(x[1] for x in after[i:i+10]) for i in range(len(after) - 9)]
    if w: print(f"   10 s windows after s1: {min(w)}-{max(w)}")
    if len(run) > 1:
        rate = (run[-1][7] - run[0][7]) / (len(run) - 1)
        print(f"   dshot sent rate {rate:.1f}/s")
    baro_ok = sum(1 for l in lines if "status: mode" in l and "baro ok" in l)
    baro_any = sum(1 for l in lines if "status: mode" in l)
    stages = sum(1 for l in lines if "stages: s " in l)
    print(f"   mode lines {baro_any}, baro ok {baro_ok}, stage lines {stages}; E/W lines after boot: {sum(1 for l in lines if re.search(r' [EW] \((\d{4,})\)', l))}")
