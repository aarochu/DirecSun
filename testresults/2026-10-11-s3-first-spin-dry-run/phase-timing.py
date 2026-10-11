"""Tick timing per time window in a timestamped monitor capture: phase-timing.py <file> <start> <end> <label> [...]"""
import gzip, re, sys, statistics
L = (gzip.open if sys.argv[1].endswith('.gz') else open)(sys.argv[1], 'rt', errors='replace').read().splitlines()
rows = []
for l in L:
    m = re.search(r'^(\S+) I \((\d+)\) status: s (\d+) ticks (\d+) missed (\d+) \(total (\d+)\) loop_us_max (\d+) dev_max_us (\d+) p99_dev_us (\d+) dshot sent (\d+) dropped (\d+) imu_invalid (\d+)', l)
    if m: rows.append((m.group(1),) + tuple(map(int, m.groups()[1:])))
# t ms s ticks missed total loop dev p99 sent dropped imu
a = sys.argv[2:]
for i in range(0, len(a), 3):
    lo, hi, label = a[i:i+3]
    r = [x for x in rows if lo <= x[0] < hi]
    if not r: print(label, 'none'); continue
    lm = [x[6] for x in r]
    print(f"{label} [{r[0][0][:8]}-{r[-1][0][:8]}]: {len(r)} s, loop_us_max min {min(lm)} median {statistics.median(lm)} max {max(lm)}, >1000 {sum(v > 1000 for v in lm)}, missed {sum(x[4] for x in r)}, p99 max {max(x[8] for x in r)}, dev max {max(x[7] for x in r)}, dropped at end {r[-1][10]}, imu_invalid {sum(x[11] for x in r)}, ticks/s {min(x[3] for x in r)}-{max(x[3] for x in r)}")
    for x in r:
        if x[6] > 1000: print('   over 1000 µs:', x[0], 's', x[2], 'loop', x[6], 'dev', x[7], 'missed', x[4])
