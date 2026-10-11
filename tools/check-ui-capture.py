#!/usr/bin/env python3
"""Check a capture of the main UI talking to the box (R4.1, FD section 17).

    tools/check-ui-capture.py CAPTURE_DIR      check CAPTURE_DIR/frames.jsonl
    tools/check-ui-capture.py --selftest       planted captures must fail

tools/ui-turn-recorder.mjs writes CAPTURE_DIR: frames.jsonl, one frame per line
as {"t": recorder ms, "page": "operator" | "watch", "dir": "tx" | "rx",
"frame": {...}}, and meta.json with the scenario, the refusal reasons it
expects, and whether its SLEWs are interrupted on purpose.

The capture passes only if all of these hold:

- every command is acked exactly once on its page within 1000 ms, ok or with
  an expected reason (the ack p50 and max are printed);
- the first tlm a page receives after a mode event has that event's mode (the
  R4.1 ordering guarantee);
- every SLEW the UI sends ends in READY with cause slew_complete within 6 s of
  being sent; with "slews_interrupted" it may instead end by the operator's
  own `mode READY` (cause mode) or e-stop (cause ESTOP), never by
  SLEW_TIMEOUT or anything else;
- pings: every gap under 500 ms, a median gap within 200 +- 20 ms, and each ping
  gets its pong (pings in a page's last 300 ms may go unanswered). Julian's
  decision of 2026-10-11 (issue 40): the box's heartbeat is 1 s, so a gap under
  500 ms is safe, and a hidden tab's Worker timer (about 300 ms between pings)
  passes while a throttled tab (1 s) does not;
- no mode event with cause HEARTBEAT_LOST (except in the watch scenario, which
  closes the operator page to provoke it);
- a page that only watches receives nothing but tlm until it sends.

It also prints the comms phase, the most common tlm t_ms modulo 20, so a
hardware run says how exposed the box was to the stale-tlm defect of U-5.
Exit status 0 means the capture passed.
"""
import collections
import json
import pathlib
import statistics
import sys

ACK_TIMEOUT_MS = 1000.0
SLEW_LIMIT_MS = 6000.0
PING_MS, PING_MEDIAN_SLACK_MS, PING_GAP_MAX_MS = 200.0, 20.0, 500.0
CLOSE_GRACE_MS = 300.0


def load(capture_dir):
    capture_dir = pathlib.Path(capture_dir)
    records = [json.loads(line) for line in (capture_dir / "frames.jsonl").read_text().splitlines() if line.strip()]
    meta_path = capture_dir / "meta.json"
    meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
    return records, meta


def check(records, meta, out=print):
    """Returns the list of problems; prints the summary through `out`."""
    problems = []
    expected_refusals = set(meta.get("expected_refusals", []))
    interrupted = bool(meta.get("slews_interrupted", False))
    # The watch scenario closes the operator page on purpose and reads the loss
    # from the watch page; every other scenario must never lose the heartbeat.
    heartbeat_lost_expected = meta.get("scenario") == "watch"
    if not records:
        return ["the capture is empty"]
    pages = sorted({r["page"] for r in records})
    ack_latencies = []
    ping_stats = []
    counts = collections.Counter()

    for page in pages:
        rows = [r for r in records if r["page"] == page]
        tx = [r for r in rows if r["dir"] == "tx"]
        rx = [r for r in rows if r["dir"] == "rx"]
        last_t = rows[-1]["t"]

        # A page receives nothing but tlm until it sends.
        first_tx_t = tx[0]["t"] if tx else float("inf")
        for r in rx:
            if r["t"] < first_tx_t and r["frame"].get("type") != "tlm":
                problems.append(f"{page}: received {r['frame'].get('type')} at {r['t']:.0f} ms before sending anything")
                break

        # Every command acked once, in time, ok or expected.
        acks = collections.defaultdict(list)
        for r in rx:
            if r["frame"].get("type") == "ack":
                acks[r["frame"].get("seq")].append(r)
        for r in tx:
            f = r["frame"]
            if f.get("type") != "cmd":
                continue
            counts["commands"] += 1
            got = acks.get(f["seq"], [])
            if len(got) != 1:
                problems.append(f"{page}: {f['cmd']} seq {f['seq']} acked {len(got)} times")
                continue
            latency = got[0]["t"] - r["t"]
            ack_latencies.append(latency)
            if latency > ACK_TIMEOUT_MS:
                problems.append(f"{page}: {f['cmd']} seq {f['seq']} acked after {latency:.0f} ms")
            a = got[0]["frame"]
            if not a.get("ok") and a.get("reason") not in expected_refusals:
                problems.append(f"{page}: {f['cmd']} seq {f['seq']} refused: {a.get('reason')}")

        # The first tlm after each mode event has that event's mode.
        pending = None
        for r in rx:
            f = r["frame"]
            if f.get("type") == "event" and f.get("event") == "mode":
                counts["mode events"] += 1
                pending = r
            elif f.get("type") == "tlm" and pending is not None:
                if f.get("mode") != pending["frame"]["mode"]:
                    problems.append(f"{page}: mode event {pending['frame']['mode']}/{pending['frame']['cause']} "
                                    f"at {pending['t']:.0f} ms followed by a tlm in {f.get('mode')}")
                pending = None

        # Every SLEW ends in slew_complete within 6 s (or as the operator meant).
        for r in tx:
            f = r["frame"]
            if f.get("type") != "cmd" or f.get("cmd") != "mode" or f.get("mode") != "SLEW":
                continue
            ack = next((a for a in acks.get(f["seq"], [])), None)
            if ack is None or not ack["frame"].get("ok"):
                continue
            counts["slews"] += 1
            end = next((e for e in rx if e["t"] > r["t"] and e["frame"].get("type") == "event"
                        and e["frame"].get("event") == "mode" and e["frame"].get("mode") != "SLEW"), None)
            target = f.get("psi_ref_deg")
            if end is None:
                problems.append(f"{page}: SLEW to {target} sent at {r['t']:.0f} ms never ended")
                continue
            cause = end["frame"].get("cause")
            took = end["t"] - r["t"]
            ok_causes = {"slew_complete"} | ({"mode", "ESTOP"} if interrupted else set())
            if cause not in ok_causes or (cause == "slew_complete" and end["frame"].get("mode") != "READY"):
                problems.append(f"{page}: SLEW to {target} ended {end['frame'].get('mode')}/{cause}")
            elif took > SLEW_LIMIT_MS:
                problems.append(f"{page}: SLEW to {target} took {took:.0f} ms")

        # Pings: every gap under 500 ms, a median within 200 +- 20 ms, each with its pong.
        pings = [r for r in tx if r["frame"].get("type") == "ping"]
        pongs = {r["frame"].get("ping_seq") for r in rx if r["frame"].get("type") == "pong"}
        counts["pings"] += len(pings)
        gaps = [b["t"] - a["t"] for a, b in zip(pings, pings[1:])]
        if gaps:
            worst = max(gaps)
            median = statistics.median(gaps)
            ping_stats.append((page, median, worst))
            if worst >= PING_GAP_MAX_MS:
                at = pings[1 + gaps.index(worst)]["t"]
                problems.append(f"{page}: ping gap {worst:.0f} ms at {at:.0f} ms (limit {PING_GAP_MAX_MS:.0f} ms)")
            if abs(median - PING_MS) > PING_MEDIAN_SLACK_MS:
                problems.append(f"{page}: median ping gap {median:.0f} ms, not {PING_MS:.0f} +- {PING_MEDIAN_SLACK_MS:.0f} ms")
        for r in ([] if heartbeat_lost_expected else rx):
            if r["frame"].get("type") == "event" and r["frame"].get("cause") == "HEARTBEAT_LOST":
                problems.append(f"{page}: the box dropped to SAFE with HEARTBEAT_LOST at {r['t']:.0f} ms")
                break
        for p in pings:
            if p["frame"]["seq"] not in pongs and last_t - p["t"] > CLOSE_GRACE_MS:
                problems.append(f"{page}: ping seq {p['frame']['seq']} at {p['t']:.0f} ms got no pong")
                break

    tlm_t = [r["frame"]["t_ms"] for r in records if r["dir"] == "rx" and r["frame"].get("type") == "tlm"
             and isinstance(r["frame"].get("t_ms"), (int, float))]
    if tlm_t:
        phase, n = collections.Counter(int(t) % 20 for t in tlm_t).most_common(1)[0]
        out(f"check-ui-capture: comms phase {phase} ms (tlm t_ms mod 20, {n} of {len(tlm_t)} frames)")
    if ack_latencies:
        out(f"check-ui-capture: acks p50 {statistics.median(ack_latencies):.1f} ms, "
            f"max {max(ack_latencies):.1f} ms, over {len(ack_latencies)} commands")
    for page, median, worst in ping_stats:
        out(f"check-ui-capture: {page} pings median {median:.0f} ms, max gap {worst:.0f} ms")
    out("check-ui-capture: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    if counts["commands"] == 0:
        problems.append("no command in the capture: the recorder captured nothing useful")
    return problems


def _synthetic_capture():
    """A clean operator turn, at 50 Hz tlm with the comms phase at 10 ms."""
    recs = []
    t = 0.0
    mode = "SAFE"
    seq = 0
    box_t = 1010

    def rx(frame):
        recs.append({"t": t, "page": "operator", "dir": "rx", "frame": frame})

    def tx(frame):
        recs.append({"t": t, "page": "operator", "dir": "tx", "frame": frame})

    def tlm():
        nonlocal box_t
        rx({"v": 1, "type": "tlm", "seq": 0, "t_ms": box_t, "mode": mode})
        box_t += 20

    script = [(0, "arm", None), (2000, "mode", 90), (5500, "mode", 0)]
    next_ping = 0.0
    events = {}
    while t < 9000:
        if t >= next_ping:
            seq += 1
            tx({"v": 1, "type": "ping", "seq": seq, "t_client_ms": t})
            rx({"v": 1, "type": "pong", "seq": 0, "ping_seq": seq, "t_client_ms": t, "t_ms": box_t})
            next_ping += 200.0
        if script and t >= script[0][0]:
            _when, cmd, target = script.pop(0)
            seq += 1
            frame = {"v": 1, "type": "cmd", "seq": seq, "cmd": cmd}
            if cmd == "mode":
                frame.update(mode="SLEW", psi_ref_deg=target)
            tx(frame)
            t += 15
            rx({"v": 1, "type": "ack", "seq": seq, "ok": True})
            mode = "SPINUP" if cmd == "arm" else "SLEW"
            rx({"v": 1, "type": "event", "event": "mode", "mode": mode, "cause": cmd})
            events[t + 1500] = ("READY", "spinup_complete" if cmd == "arm" else "slew_complete")
        due = sorted(k for k in events if k <= t)
        for k in due:
            mode, cause = events.pop(k)
            rx({"v": 1, "type": "event", "event": "mode", "mode": mode, "cause": cause})
        tlm()
        t += 20
    return recs


def selftest():
    quiet = lambda _line: None
    clean = _synthetic_capture()
    problems = check(clean, {}, quiet)
    if problems:
        print(f"check-ui-capture selftest: FAILED, the clean capture failed: {problems}", file=sys.stderr)
        return 1

    # Planted defect 1: the tlm after slew_complete still says SLEW (U-5).
    stale = json.loads(json.dumps(clean))
    i = next(i for i, r in enumerate(stale) if r["frame"].get("cause") == "slew_complete")
    j = next(j for j in range(i + 1, len(stale)) if stale[j]["frame"].get("type") == "tlm")
    stale[j]["frame"]["mode"] = "SLEW"
    # Planted defect 2: one command's ack is missing.
    missing = json.loads(json.dumps(clean))
    k = next(k for k, r in enumerate(missing) if r["frame"].get("type") == "ack")
    del missing[k]

    # Planted defect 3: one 600 ms gap between pings (a throttled tab).
    gap = json.loads(json.dumps(clean))
    pings = [r for r in gap if r["frame"].get("type") == "ping"]
    for r in gap:
        if r["frame"].get("type") == "ping" and r["t"] >= pings[10]["t"]:
            r["t"] += 400.0
    # Planted defect 4: a steady 300 ms cadence (a hidden tab on a page timer).
    slow = json.loads(json.dumps(clean))
    n = 0
    for r in slow:
        if r["frame"].get("type") == "ping":
            r["t"] = n * 300.0
            n += 1
    # Planted defect 5: the box lost the heartbeat.
    lost = json.loads(json.dumps(clean))
    lost.append({"t": lost[-1]["t"], "page": "operator", "dir": "rx",
                 "frame": {"v": 1, "type": "event", "event": "mode", "mode": "SAFE", "cause": "HEARTBEAT_LOST"}})

    # Boundary controls: the limits must not be tighter than the decision.
    ok_gap = json.loads(json.dumps(clean))
    ok_pings = [r for r in ok_gap if r["frame"].get("type") == "ping"]
    for r in ok_gap:
        if r["frame"].get("type") == "ping" and r["t"] >= ok_pings[10]["t"]:
            r["t"] += 250.0  # one 450 ms gap, under the 500 ms limit
    problems = check(ok_gap, {}, quiet)
    if problems:
        print(f"check-ui-capture selftest: FAILED, a single 450 ms ping gap was rejected: {problems}", file=sys.stderr)
        return 1

    failures = 0
    for name, capture, needle in (("stale tlm after slew_complete", stale, "followed by a tlm in SLEW"),
                                  ("missing ack", missing, "acked 0 times"),
                                  ("600 ms ping gap", gap, "ping gap"),
                                  ("300 ms ping cadence", slow, "median ping gap"),
                                  ("HEARTBEAT_LOST", lost, "HEARTBEAT_LOST")):
        got = check(capture, {}, quiet)
        if any(needle in p for p in got):
            print(f"check-ui-capture selftest: PASSED, the planted {name} was detected")
        else:
            print(f"check-ui-capture selftest: FAILED, the planted {name} passed: {got}", file=sys.stderr)
            failures += 1
    return 1 if failures else 0


def main(argv):
    if argv == ["--selftest"]:
        return selftest()
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 64
    records, meta = load(argv[0])
    problems = check(records, meta)
    for p in problems:
        print(f"check-ui-capture: FAILED, {p}", file=sys.stderr)
    if problems:
        return 1
    print(f"check-ui-capture: PASSED, scenario {meta.get('scenario', '?')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
