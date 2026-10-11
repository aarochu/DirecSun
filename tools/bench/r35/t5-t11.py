#!/usr/bin/env python3
"""R3.5 Tests item 6, T-11: telemetry rate and latency over 10 minutes.

    t5-t11.py --url ws://direcsun.local/ws [--seconds 600]

The item: "T-11: 10 min capture, tlm at 50 Hz with under 1% loss, round-trip p95
under 100 ms."

Runs the repo's own `tools/capture-box.py --seconds 600 --t11` (pings at 5 Hz) and
then `tools/check-schema.py` on the capture. The verdict comes from the capture file:
- loss is counted: frames received (distinct seq) against the frames the box-clock
  span implies at 50 Hz. capture-box instead counts round(gap / 20 ms) - 1 per gap,
  which calls a late frame lost and never credits the catch-up frames after it: it
  fails a lossless stream from a sender whose t_ms jitters (the mock under CPU load,
  test case t5-pass-bursty-sender). Its figure and exit status are still printed;
- a capture that ended early fails: capture-box stops quietly when the box closes the
  connection and then judges only what it got, so a 30 s capture can pass --t11;
- the box seq numbers missing across tlm, pong, and event are reported (exact with
  one client, since the three share the box's counter);
- round trip p50, p95, and max are capture-box's numbers: the capture file does not
  record arrival times, so they cannot be recomputed from it.

t7-timing.py runs this same capture as its streaming client, so one 10-minute run
can serve both T-11 and R3.4's timing item (RUNBOOK step 6).
"""
import argparse
import sys

import r35common as rc


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--seconds", type=float, default=600.0)
    parser.add_argument("--out")
    args = parser.parse_args()
    run = rc.Run("t5-t11", args.out)
    capture = run.dir / "t11.jsonl"
    run.say(f"capture-box --t11 for {args.seconds:.0f} s from {args.url}; keep the laptop awake and in range")
    code, text = rc.run_logged(rc.t11_capture_cmd(args.url, args.seconds, capture), run.dir / "capture-box.txt",
                               timeout=args.seconds + 120)
    for line in text.splitlines()[1:]:
        run.say(f"    {line}")
    tool, mine = rc.judge_t11(run, code, text, capture, args.seconds)
    ok, summaries = rc.check_schema(run, [capture], "t11")
    run.check("check-schema on the capture", ok, "; ".join(summaries.values()))
    failed = any(c["ok"] is False for c in run.checks)
    return run.finish(summary="" if failed else
                      f"{mine['rate_hz']:.2f} Hz, loss {mine['loss'] * 100:.3f} %, p95 {tool.get('p95_ms')} ms "
                      f"over {mine['span_s']:.0f} s")


if __name__ == "__main__":
    sys.exit(main())
