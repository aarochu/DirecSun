"""Segment monitor captures by mode, throttle, supply on (vbat > 5 V), latch, flags and baro: segment.py <file>"""
import gzip, re, sys
L = (gzip.open if sys.argv[1].endswith('.gz') else open)(sys.argv[1], 'rt', errors='replace').read().replace('\r', '').splitlines()
L = [re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', l) for l in L]
prev = None; seg = None; drops = []; missed = 0; inv = 0; lm = []
for l in L:
    m = re.search(r'^(\S+) I \((\d+)\) status: s (\d+) ticks (\d+) missed (\d+) \(total (\d+)\) loop_us_max (\d+) .*dshot sent (\d+) dropped (\d+) imu_invalid (\d+)', l)
    if m:
        t, ms, s, ti, mi, tot, lmax, sent, dr, ii = m.groups()
        if prev is not None and int(s) != prev + 1:
            print('  status counter jump at', t, 's', prev, '->', s)
        prev = int(s); drops.append(int(dr)); missed += int(mi); inv += int(ii); lm.append(int(lmax))
        continue
    m = re.search(r'^(\S+) I \((\d+)\) status: mode (\S+) throttle (\S+) vbat (\S+) .*latched (\S+) flags (\S+) baro (\S+)', l)
    if m:
        t, ms, mode, thr, v, lat, fl, baro = m.groups()
        key = (mode, thr if mode == 'SAFE' and thr == '0' else 'var', float(v) > 5, lat, fl, baro)
        if seg is None or seg[0] != key:
            if seg: print(' ', seg[1], '->', seg[2], seg[0], 'n', seg[3], 'vbat', seg[4], '-', seg[5])
            seg = [key, t, t, 0, 99, 0]
        seg[2] = t; seg[3] += 1; seg[4] = min(seg[4], float(v)); seg[5] = max(seg[5], float(v))
if seg: print(' ', seg[1], '->', seg[2], seg[0], 'n', seg[3], 'vbat', seg[4], '-', seg[5])
print('  status rows', len(lm), 'dropped max', max(drops) if drops else None, 'missed in rows', missed, 'imu_invalid sum', inv, 'loop_us_max range', (min(lm), max(lm)) if lm else None)
print('  monitor errors:', [l[:100] for l in L if re.search(r'Error|Waiting for the device|lock port', l)][:5])
