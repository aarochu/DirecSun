#!/usr/bin/env python3
"""R3.4 timing-budget item, run on the R3.5 build with Wi-Fi connected.

    t7-timing.py --url ws://direcsun.local/ws --serial-log serial.log [--seconds 600]
    t7-timing.py --serial-log serial.log --log-only          # re-analyse a saved log

The item (changes/3-teleop/R3.4-control-task.md): "Timing budget: loop_us max under
1000 us over 10 min with every sensor read and Wi-Fi connected (FD F-4 'done when')."

The serial log is a live capture of `idf.py monitor` (RUNBOOK step 1) that keeps
growing while this runs. The script:
 1. checks the log is growing (else the monitor is not writing to it, and an empty
    window would say nothing);
 2. notes the log's size, then streams for --seconds with `tools/capture-box.py
    --t11` (one client, pings at 5 Hz, the same capture T-11 needs);
 3. reads only the part of the log written during the stream and parses the
    firmware's once-a-second status lines (firmware/main/services.cpp):
      "status: s N ticks T missed M (total X) loop_us_max L dev_max_us ..."
      "status: mode SAFE ... baro ok|--"
Checks: a status line for at least 95 % of the seconds; the `s` counter never
restarts (no reboot) and no reset or CONTROL_STALL lines; the barometer answered
in every second and imu_invalid stayed 0 (every sensor read); no "wifi:
disconnected" line and the stream got tlm at 50 Hz for the whole window (Wi-Fi
connected); and the verdict: the worst loop_us_max under --budget (1000 us).

It also judges T-11 on the same capture and prints a second RESULT line for it, so
one 10-minute run covers both items. --log-only parses a whole saved log with no
stream (the Wi-Fi condition is then unproven and reported as such).
"""
import argparse
import pathlib
import re
import statistics
import sys
import time

import r35common as rc

STATUS = re.compile(r"\((\d+)\) status: s (\d+) ticks (\d+) missed (\d+) \(total (\d+)\) loop_us_max (\d+) "
                    r"dev_max_us (\d+) p99_dev_us (\d+) dshot sent (\d+) dropped (\d+) imu_invalid (\d+)")
SNAPSHOT = re.compile(r"status: mode (\S+) .* baro (ok|--)")
WIFI_DROP = re.compile(r"wifi: disconnected \(reason (-?\d+)\)")
WS_CLIENT = re.compile(r"ws: client (\d+) (connected|disconnected)")
RESET = re.compile(r"(rst:0x[0-9a-f]+|CONTROL_STALL|Guru Meditation|abort\(\) was called)")
STAGES = re.compile(r"stages: s (\d+) ")


def parse(text):
    lines = rc.strip_ansi(text).splitlines()
    status = []
    first_status = None
    for index, line in enumerate(lines):
        m = STATUS.search(line)
        if m:
            first_status = index if first_status is None else first_status
            v = list(map(int, m.groups()))
            status.append({"boot_ms": v[0], "s": v[1], "ticks": v[2], "missed": v[3], "missed_total": v[4],
                           "loop_us_max": v[5], "dev_max_us": v[6], "p99_dev_us": v[7], "dshot_sent": v[8],
                           "dshot_dropped": v[9], "imu_invalid": v[10]})
    return {
        "lines": len(lines),
        "status": status,
        "baro": [m.group(2) for line in lines if (m := SNAPSHOT.search(line))],
        "modes": sorted({m.group(1) for line in lines if (m := SNAPSHOT.search(line))}),
        "wifi_drops": [m.group(1) for line in lines if (m := WIFI_DROP.search(line))],
        "ws_events": [(m.group(1), m.group(2)) for line in lines if (m := WS_CLIENT.search(line))],
        # A boot before the first status line is the monitor's own reset; only later ones count.
        "resets": [line.strip()[:160] for index, line in enumerate(lines)
                   if RESET.search(line) and first_status is not None and index > first_status],
        "stage_lines": sum(1 for line in lines if STAGES.search(line)),
    }


def judge(run, parsed, seconds, budget, log_only):
    st = parsed["status"]
    need = 1 if log_only else int(0.95 * seconds)
    run.check("status lines in the window", len(st) >= need,
              f"{len(st)} status lines (need {need}); {parsed['lines']} log lines read"
              + ("" if st else ": is the monitor running, writing to this file, on the R3.5 flight build?"))
    if not st:
        return None
    restarts = [(a["s"], b["s"]) for a, b in zip(st, st[1:]) if b["s"] <= a["s"]]
    gaps = [(a["s"], b["s"]) for a, b in zip(st, st[1:]) if b["s"] > a["s"] + 1]
    run.check("no reboot in the window", not restarts and not parsed["resets"],
              f"s {st[0]['s']}..{st[-1]['s']}; restarts {restarts[:3]}; reset lines {parsed['resets'][:3]}")
    run.check("status seconds missing from the log", None, f"{len(gaps)} gap(s) {gaps[:5]}")
    if parsed["stage_lines"]:
        run.check("flight build", False, f"{parsed['stage_lines']} 'stages:' lines: this is the diagnostic build "
                  "(CONFIG_DIRECSUN_BENCH_STAGE_TIMING); judge the item on the flight build")
    baro_bad = sum(1 for b in parsed["baro"] if b != "ok")
    run.check("every sensor read: barometer answered every second", bool(parsed["baro"]) and baro_bad == 0,
              f"baro ok in {len(parsed['baro']) - baro_bad} of {len(parsed['baro'])} status lines")
    imu_invalid = sum(x["imu_invalid"] for x in st)
    run.check("every sensor read: no invalid IMU reads", imu_invalid == 0, f"imu_invalid sum {imu_invalid}")
    run.check("modes during the window", None, f"{parsed['modes']}")
    if log_only:
        run.check("Wi-Fi connected and a client streaming", None,
                  f"--log-only cannot prove it: wifi drops {len(parsed['wifi_drops'])}, ws client lines "
                  f"{parsed['ws_events'][:6]}")
    else:
        run.check("Wi-Fi stayed connected", not parsed["wifi_drops"],
                  f"{len(parsed['wifi_drops'])} 'wifi: disconnected' line(s) {parsed['wifi_drops'][:5]}")
    loops = [x["loop_us_max"] for x in st]
    worst = max(loops)
    at = st[loops.index(worst)]["s"]
    ordered = sorted(loops)
    p99 = ordered[min(len(ordered) - 1, int(round(0.99 * (len(ordered) - 1))))]
    missed = sum(x["missed"] for x in st)
    run.check("missed ticks in the window", None, f"{missed} (sum of per-second counts)")
    run.check("dshot dropped in the window", None, f"{sum(x['dshot_dropped'] for x in st)}")
    run.check(f"worst loop_us_max under {budget} us", worst < budget,
              f"worst {worst} us at s {at}; median {statistics.median(loops):.0f}, p99 {p99}, "
              f"{sum(1 for v in loops if v >= budget)} second(s) at or over {budget}, "
              f"{sum(1 for v in loops if v >= budget - 50)} within 50 us of it; over {len(st)} s")
    return {"worst": worst, "at": at, "seconds": len(st), "median": statistics.median(loops), "p99": p99}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--serial-log", required=True)
    parser.add_argument("--seconds", type=float, default=600.0)
    parser.add_argument("--budget", type=int, default=1000)
    parser.add_argument("--log-only", action="store_true", help="parse the whole saved log; no stream")
    parser.add_argument("--out")
    args = parser.parse_args()
    run = rc.Run("t7-timing", args.out)
    log_path = pathlib.Path(args.serial_log)
    if not log_path.is_file():
        run.check("serial log exists", False, f"{log_path} not found")
        return run.finish()

    if args.log_only:
        text = log_path.read_text(errors="replace")
        (run.dir / "window.txt").write_text(text)
        result = judge(run, parse(text), args.seconds, args.budget, True)
        return run.finish(summary=f"worst loop_us_max {result['worst']} us at s {result['at']} over {result['seconds']} s "
                                  "(log only: Wi-Fi not proven)" if result and not any(c["ok"] is False for c in run.checks) else "")

    size0 = log_path.stat().st_size
    time.sleep(3.0)
    offset = log_path.stat().st_size
    run.check("serial log is growing", offset > size0,
              f"{offset - size0} bytes in 3 s" + ("" if offset > size0 else
                                                 ": start the monitor capture first (RUNBOOK step 1)"))
    if offset <= size0:
        return run.finish()
    run.say(f"streaming {args.seconds:.0f} s from {args.url} (capture-box --t11); serial window starts at byte {offset}")
    capture = run.dir / "stream.jsonl"
    code, text = rc.run_logged(rc.t11_capture_cmd(args.url, args.seconds, capture), run.dir / "capture-box.txt",
                               timeout=args.seconds + 120)
    for line in text.splitlines()[1:]:
        run.say(f"    {line}")
    time.sleep(2.5)
    with open(log_path, "rb") as f:
        f.seek(offset)
        window = f.read().decode(errors="replace")
    (run.dir / "window.txt").write_text(window)
    run.say(f"serial window: {len(window)} bytes saved to window.txt")

    mine = rc.recount_tlm(capture) if capture.exists() else rc.recount_tlm_empty()
    run.check("a client streamed for the whole window", mine["span_s"] >= 0.98 * args.seconds
              and mine["tlm"] >= 0.98 * 50 * args.seconds,
              f"{mine['tlm']} tlm over {mine['span_s']:.1f} s of box time (capture-box exit {code})")
    result = judge(run, parse(window), args.seconds, args.budget, False)

    t11 = rc.Run("t5-t11", parent=run)
    run.say("T-11 on the same capture:")
    rc.judge_t11(t11, code, text, capture, args.seconds)
    t11_code = t11.finish()
    failed = any(c["ok"] is False for c in run.checks)
    code = run.finish(summary="" if failed or not result else
                      f"worst loop_us_max {result['worst']} us at s {result['at']} over {result['seconds']} s, "
                      "Wi-Fi connected, one client streaming")
    return code or t11_code


if __name__ == "__main__":
    sys.exit(main())
