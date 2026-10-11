#!/usr/bin/env python3
"""Record what the DirecSun box sends, for the R3.5 and T-11 checks.

    tools/capture-box.py [--url ws://direcsun.local/ws] [--seconds 60] [--out capture.jsonl]
                         [--silent] [--t11]

Connects to the box (or tools/mock-box.py), sends a ping at 5 Hz unless --silent,
and writes every received frame to a JSON-lines file, one message per line. At the
end it prints the counts by message type, the telemetry rate, the telemetry lost
(the frames the box's t_ms span should hold, minus the distinct t_ms received;
tlm, event, and pong share the box's seq counter, so seq cannot say), and
round-trip percentiles from the pongs. A capture that ends early, because the box
closed the connection or went silent, fails.

    tools/check-schema.py capture.jsonl     # every captured frame against the schema

--silent never sends anything: the client is a read-only viewer, so it must
receive only tlm (R3.5). The script exits 1 if anything else arrives.

--t11 applies the T-11 pass criteria (TD section 9.2): tlm at 50 Hz with under 1 %
loss, and round-trip p95 under 100 ms. The script exits 1 if they are not met.

Needs websockets. If this interpreter lacks it, the script re-runs itself with
$DIRECSUN_PYTHON or the repo's .venv (tools/bootstrap-venv.sh).
"""
import argparse
import asyncio
import json
import os
import pathlib
import statistics
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent

try:
    from websockets.asyncio.client import connect
    from websockets.exceptions import ConnectionClosed
except ImportError:
    if not os.environ.get("DIRECSUN_REEXEC"):
        for candidate in (os.environ.get("DIRECSUN_PYTHON"), str(REPO / ".venv" / "bin" / "python")):
            if candidate and os.path.exists(candidate) and os.path.abspath(candidate) != os.path.abspath(sys.executable):
                os.environ["DIRECSUN_REEXEC"] = "1"
                os.execv(candidate, [candidate, __file__, *sys.argv[1:]])
    sys.exit("capture-box: needs the websockets package. Run tools/bootstrap-venv.sh or set DIRECSUN_PYTHON.")

TLM_PERIOD_MS = 20.0     # 50 Hz
PING_PERIOD_S = 0.2      # 5 Hz


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


async def capture(args):
    received = []
    sent_at = {}
    rtts_ms = []
    closed = None
    started = time.monotonic()
    async with connect(args.url, open_timeout=10) as ws:
        async def pinger():
            seq = 0
            while True:
                seq += 1
                now_ms = (time.monotonic() - started) * 1000.0
                sent_at[seq] = now_ms
                await ws.send(json.dumps({"v": 1, "type": "ping", "seq": seq, "t_client_ms": round(now_ms, 3)}))
                await asyncio.sleep(PING_PERIOD_S)

        ping_task = None if args.silent else asyncio.ensure_future(pinger())
        deadline = started + args.seconds
        try:
            while time.monotonic() < deadline:
                try:
                    text = await asyncio.wait_for(ws.recv(), timeout=max(0.01, deadline - time.monotonic()))
                except asyncio.TimeoutError:
                    break
                arrived_ms = (time.monotonic() - started) * 1000.0
                received.append(text)
                try:
                    message = json.loads(text)
                except json.JSONDecodeError:
                    continue
                if message.get("type") == "pong" and message.get("ping_seq") in sent_at:
                    rtts_ms.append(arrived_ms - sent_at.pop(message["ping_seq"]))
        except ConnectionClosed as exc:
            closed = str(exc)
            print(f"capture-box: the box closed the connection: {exc}", file=sys.stderr)
        finally:
            if ping_task:
                ping_task.cancel()
    return received, rtts_ms, time.monotonic() - started, closed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--out", default="capture.jsonl")
    parser.add_argument("--silent", action="store_true", help="never send: a read-only viewer must receive only tlm")
    parser.add_argument("--t11", action="store_true", help="apply the T-11 pass criteria")
    args = parser.parse_args()

    received, rtts_ms, elapsed, closed = asyncio.run(capture(args))
    pathlib.Path(args.out).write_text("".join(line + "\n" for line in received))

    kinds = {}
    tlm_times = []
    for text in received:
        try:
            message = json.loads(text)
        except json.JSONDecodeError:
            kinds["(not JSON)"] = kinds.get("(not JSON)", 0) + 1
            continue
        kind = message.get("type", "?")
        kinds[kind] = kinds.get(kind, 0) + 1
        if kind == "tlm" and isinstance(message.get("t_ms"), int):
            tlm_times.append(message["t_ms"])

    max_gap_ms = 0
    backwards = 0
    for previous, current in zip(tlm_times, tlm_times[1:]):
        gap = current - previous
        if gap < 0:
            backwards += 1
            continue
        max_gap_ms = max(max_gap_ms, gap)
    tlm_count = len(tlm_times)
    # Loss over the box's own clock: the frames its t_ms span should hold, minus
    # the distinct frames received. Summing round(gap / period) - 1 per gap would
    # count jitter (a 30 ms gap, then a 10 ms one) as a lost frame.
    distinct = len(set(tlm_times))
    expected = round((max(tlm_times) - min(tlm_times)) / TLM_PERIOD_MS) + 1 if tlm_times else 0
    lost = max(0, expected - distinct)
    loss = lost / expected if expected else 1.0
    span_s = (tlm_times[-1] - tlm_times[0]) / 1000.0 if tlm_count > 1 else 0.0
    rate = (tlm_count - 1) / span_s if span_s > 0 else 0.0

    print(f"capture-box: {len(received)} frames in {elapsed:.1f} s written to {args.out}")
    print(f"capture-box: by type {dict(sorted(kinds.items()))}")
    print(f"capture-box: tlm {tlm_count} frames, {rate:.2f} Hz over the box's t_ms, {lost} lost "
          f"({loss * 100:.2f} %), largest gap {max_gap_ms} ms, t_ms went backwards {backwards} time(s)")
    if rtts_ms:
        print(f"capture-box: round trip from {len(rtts_ms)} pongs: p50 {percentile(rtts_ms, 0.5):.1f} ms, "
              f"p95 {percentile(rtts_ms, 0.95):.1f} ms, max {max(rtts_ms):.1f} ms, "
              f"mean {statistics.fmean(rtts_ms):.1f} ms")

    problems = []
    if tlm_count == 0:
        problems.append("no tlm received")
    if closed is not None or elapsed < 0.98 * args.seconds:
        problems.append(f"the capture ended after {elapsed:.1f} s of {args.seconds:.0f} s"
                        + (f" (the box closed the connection: {closed})" if closed is not None else ""))
    if backwards:
        problems.append("t_ms went backwards")
    if args.silent and set(kinds) - {"tlm"}:
        problems.append(f"a read-only client received {sorted(set(kinds) - {'tlm'})}")
    if args.t11:
        if not 49.0 <= rate <= 51.0:
            problems.append(f"tlm rate {rate:.2f} Hz is not 50 Hz")
        if loss >= 0.01:
            problems.append(f"tlm loss {loss * 100:.2f} % is not under 1 %")
        p95 = percentile(rtts_ms, 0.95)
        if p95 is None or p95 >= 100.0:
            problems.append(f"round-trip p95 {p95} ms is not under 100 ms")
    for problem in problems:
        print(f"capture-box: FAILED, {problem}", file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
