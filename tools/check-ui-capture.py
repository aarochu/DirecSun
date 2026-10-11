#!/usr/bin/env python3
"""Checks a frames.jsonl written by tools/ui-turn-recorder.mjs.

Usage:  tools/check-ui-capture.py frames.jsonl [--expect-hidden [--min-hidden-s 30]]
        tools/check-ui-capture.py --selftest

Each line is one JSON object:
  {"t": <ms since the run began>, "page": "operator"|"watcher", "dir": "tx"|"rx", "msg": {...}}
  {"t": ..., "page": ..., "dir": "meta", "what": "visibility", "state": "hidden"|"visible"}

It passes only if, per page:
  - every cmd is acked exactly once, within 1000 ms (p50 and max are printed);
  - the first tlm after each mode event carries that event's mode;
  - every SLEW the page sent ends in slew_complete before 6 s, unless the operator
    interrupted it (a later mode READY or estop) and the box said so;
  - pings go out every 200 +/- 60 ms and every ping gets a pong;
  - a watch-only page receives nothing but tlm until it sends something.

Any HEARTBEAT_LOST mode event fails a capture. With --expect-hidden (the recorder's hidden
scenario) the operator page must have recorded a visibilitychange to hidden for at least
--min-hidden-s seconds, or the run is not a result. Hidden-tab pings are held to the
same 200 +/- 60 ms requirement as every other capture.

It also prints the comms phase (the commonest tlm.t_ms mod 20) and, when the capture
holds visibility records, how long the operator page was hidden.
"""
import collections
import json
import statistics
import sys

ACK_MAX_MS = 1000.0
SLEW_MAX_MS = 6000.0
PING_MS = 200.0
PING_TOL_MS = 60.0


def load(path):
    frames = []
    with open(path) as handle:
        for number, line in enumerate(handle, 1):
            line = line.strip()
            if line:
                try:
                    frames.append(json.loads(line))
                except json.JSONDecodeError as error:
                    raise SystemExit(f"{path}:{number}: not JSON: {error}")
    return frames


def check(frames, expect_hidden=False, quiet=False, min_hidden_s=30.0):
    problems = []
    say = (lambda *a: None) if quiet else print
    pages = collections.OrderedDict()
    for frame in frames:
        pages.setdefault(frame.get("page", "operator"), []).append(frame)
    end_t = max((f["t"] for f in frames), default=0.0)

    for name, items in pages.items():
        msgs = [f for f in items if f["dir"] in ("tx", "rx")]
        tx = [f for f in msgs if f["dir"] == "tx"]
        rx = [f for f in msgs if f["dir"] == "rx"]

        cmds = [f for f in tx if f["msg"].get("type") == "cmd"]
        acks = collections.defaultdict(list)
        for f in rx:
            if f["msg"].get("type") == "ack":
                acks[f["msg"].get("seq")].append(f)
        latencies = []
        refused = []
        for f in cmds:
            seq = f["msg"].get("seq")
            got = [a for a in acks.get(seq, []) if a["t"] >= f["t"]]
            if len(got) != 1:
                problems.append(f"{name}: cmd seq {seq} ({f['msg'].get('cmd')}) got {len(got)} acks, expected 1")
                continue
            latency = got[0]["t"] - f["t"]
            latencies.append(latency)
            if latency > ACK_MAX_MS:
                problems.append(f"{name}: cmd seq {seq} acked after {latency:.0f} ms, over {ACK_MAX_MS:.0f}")
            if not got[0]["msg"].get("ok"):
                refused.append((f["msg"].get("cmd"), got[0]["msg"].get("reason")))
        if latencies:
            say(f"{name}: {len(cmds)} cmds, ack p50 {statistics.median(latencies):.0f} ms, max {max(latencies):.0f} ms"
                + (f", refused {refused}" if refused else ""))
        else:
            say(f"{name}: no cmds sent")

        stream = [f for f in rx if f["msg"].get("type") == "tlm" or
                  (f["msg"].get("type") == "event" and f["msg"].get("event") == "mode")]
        stale = 0
        checked = 0
        for i, f in enumerate(stream):
            if f["msg"].get("type") != "event":
                continue
            if i + 1 >= len(stream) or stream[i + 1]["msg"].get("type") != "tlm":
                continue
            checked += 1
            if stream[i + 1]["msg"].get("mode") != f["msg"].get("mode"):
                stale += 1
                problems.append(
                    f"{name}: stale tlm at {stream[i + 1]['t']:.0f} ms: mode event {f['msg'].get('mode')} "
                    f"(cause {f['msg'].get('cause')}) was followed by tlm mode {stream[i + 1]['msg'].get('mode')}")
        say(f"{name}: {checked} mode events checked against the next tlm, {stale} stale")

        mode_events = [f for f in rx if f["msg"].get("type") == "event" and f["msg"].get("event") == "mode"]
        for e in mode_events:
            if e["msg"].get("cause") == "HEARTBEAT_LOST":
                problems.append(f"{name}: the box dropped to SAFE with HEARTBEAT_LOST at {e['t']:.0f} ms")
        slews = 0
        for f in cmds:
            m = f["msg"]
            if m.get("cmd") != "mode" or m.get("mode") != "SLEW":
                continue
            ack = next((a for a in acks.get(m.get("seq"), []) if a["t"] >= f["t"]), None)
            if ack is None or not ack["msg"].get("ok"):
                continue
            slews += 1
            ended = next((e for e in mode_events if e["t"] >= ack["t"] and e["msg"].get("mode") != "SLEW"), None)
            if ended is None:
                problems.append(f"{name}: SLEW seq {m.get('seq')} never ended with a mode event")
                continue
            interrupted = any(c["msg"].get("cmd") in ("estop", "disarm") or
                              (c["msg"].get("cmd") == "mode" and c["msg"].get("mode") == "READY")
                              for c in cmds if f["t"] < c["t"] <= ended["t"])
            took = ended["t"] - f["t"]
            if ended["msg"].get("cause") == "slew_complete":
                if took > SLEW_MAX_MS:
                    problems.append(f"{name}: SLEW seq {m.get('seq')} took {took:.0f} ms to slew_complete, over {SLEW_MAX_MS:.0f}")
            elif not interrupted:
                problems.append(f"{name}: SLEW seq {m.get('seq')} ended with cause {ended['msg'].get('cause')}, not slew_complete")
        say(f"{name}: {slews} SLEWs accepted")

        pings = [f for f in tx if f["msg"].get("type") == "ping"]
        pongs = collections.defaultdict(int)
        for f in rx:
            if f["msg"].get("type") == "pong":
                pongs[f["msg"].get("ping_seq")] += 1
        if pings:
            gaps = [b["t"] - a["t"] for a, b in zip(pings, pings[1:])]
            bad = [g for g in gaps if abs(g - PING_MS) > PING_TOL_MS]
            if gaps:
                say(f"{name}: {len(pings)} pings, gap median {statistics.median(gaps):.0f} ms, max {max(gaps):.0f} ms, "
                    f"{len(bad)} outside {PING_MS:.0f} +/- {PING_TOL_MS:.0f}")
            if bad:
                problems.append(f"{name}: {len(bad)} of {len(gaps)} ping gaps outside "
                                f"{PING_MS:.0f} +/- {PING_TOL_MS:.0f} ms (max {max(bad):.0f} ms)")
            missing = [p for p in pings if pongs.get(p["msg"].get("seq"), 0) < 1 and end_t - p["t"] > ACK_MAX_MS]
            if missing:
                problems.append(f"{name}: {len(missing)} pings never got a pong (first seq {missing[0]['msg'].get('seq')})")

        if name == "watcher":
            first_tx = tx[0]["t"] if tx else float("inf")
            early = [f for f in rx if f["t"] < first_tx and f["msg"].get("type") != "tlm"]
            if early:
                problems.append(f"{name}: received {early[0]['msg'].get('type')} before it sent anything "
                                f"({len(early)} non-tlm frames)")
            say(f"{name}: {len(rx)} frames received, {len(early)} non-tlm before its first send")

    tlm = [f["msg"]["t_ms"] for f in frames if f.get("dir") == "rx" and f["msg"].get("type") == "tlm"
           and isinstance(f["msg"].get("t_ms"), (int, float))]
    if tlm:
        phase, count = collections.Counter(int(t) % 20 for t in tlm).most_common(1)[0]
        say(f"comms phase: tlm.t_ms mod 20 is {phase} in {count} of {len(tlm)} tlm frames")

    hidden = [f for f in frames if f.get("dir") == "meta" and f.get("what") == "visibility"]
    if hidden:
        state = None
        since = None
        total = 0.0
        for f in hidden:
            if f["state"] == "hidden" and state != "hidden":
                since = f["t"]
            elif f["state"] == "visible" and state == "hidden":
                total += f["t"] - since
            state = f["state"]
        if state == "hidden":
            total += end_t - since
        say(f"operator page hidden for {total / 1000:.1f} s ({sum(1 for f in hidden if f['state'] == 'hidden')} hidden events)")
        if expect_hidden and total < min_hidden_s * 1000:
            problems.append(f"the operator page was hidden for {total / 1000:.1f} s, under {min_hidden_s:.0f} s, "
                            "so the run did not hide the page long enough to count")
    elif expect_hidden:
        problems.append("no visibility records in the capture, so the run did not hide the page")
    return problems


def _frame(t, direction, msg, page="operator"):
    return {"t": float(t), "page": page, "dir": direction, "msg": msg}


def _good_capture():
    """A clean turn: arm, spin-up, one SLEW that completes, pings every 200 ms with pongs."""
    frames = []
    seq = [0]

    def rx(t, msg, page="operator"):
        seq[0] += 1
        frames.append(_frame(t, "rx", {"v": 1, "seq": seq[0], **msg}, page))

    def tx(t, msg, page="operator"):
        frames.append(_frame(t, "tx", {"v": 1, **msg}, page))

    tx(10, {"type": "cmd", "seq": 1, "cmd": "arm"})
    rx(40, {"type": "ack", "seq": 1, "ok": True})
    rx(41, {"type": "event", "event": "mode", "mode": "SPINUP", "cause": "arm", "t_ms": 1})
    rx(60, {"type": "tlm", "mode": "SPINUP", "t_ms": 60})
    rx(6300, {"type": "event", "event": "mode", "mode": "READY", "cause": "spinup_complete", "t_ms": 6300})
    rx(6320, {"type": "tlm", "mode": "READY", "t_ms": 6320})
    tx(7000, {"type": "cmd", "seq": 2, "cmd": "mode", "mode": "SLEW", "psi_ref_deg": 90})
    rx(7030, {"type": "ack", "seq": 2, "ok": True})
    rx(7031, {"type": "event", "event": "mode", "mode": "SLEW", "cause": "mode", "t_ms": 7031})
    rx(7040, {"type": "tlm", "mode": "SLEW", "t_ms": 7040})
    rx(10000, {"type": "event", "event": "mode", "mode": "READY", "cause": "slew_complete", "t_ms": 10000})
    rx(10020, {"type": "tlm", "mode": "READY", "t_ms": 10020})
    for k, t in enumerate(range(0, 10000, 200)):
        tx(t, {"type": "ping", "seq": 100 + k, "t_client_ms": t})
        rx(t + 15, {"type": "pong", "ping_seq": 100 + k, "t_client_ms": t, "t_ms": t})
    rx(5, {"type": "tlm", "mode": "SAFE", "t_ms": 5}, page="watcher")
    tx(100, {"type": "ping", "seq": 99, "t_client_ms": 100}, page="watcher")
    rx(115, {"type": "pong", "ping_seq": 99, "t_client_ms": 100, "t_ms": 115}, page="watcher")
    frames.sort(key=lambda f: f["t"])
    return frames


def selftest():
    failures = []

    def expect(label, frames, should_pass, needle=None):
        problems = check(frames, quiet=True)
        ok = not problems
        if ok != should_pass:
            failures.append(f"{label}: expected {'pass' if should_pass else 'fail'}, got {problems or 'pass'}")
        elif needle and not any(needle in p for p in problems):
            failures.append(f"{label}: failed for the wrong reason: {problems}")
        print(f"check-ui-capture selftest: {label}: {'passes' if ok else 'fails'}" + (f" ({problems[0]})" if problems else ""))

    good = _good_capture()
    expect("clean capture", good, True)

    stale = [dict(f) for f in good]
    for f in stale:
        if f["dir"] == "rx" and f["msg"].get("type") == "tlm" and f["t"] == 10020:
            f["msg"] = {**f["msg"], "mode": "SLEW"}
    expect("planted stale tlm after slew_complete", stale, False, "stale tlm")

    unacked = [f for f in good if not (f["dir"] == "rx" and f["msg"].get("type") == "ack" and f["msg"].get("seq") == 2)]
    expect("planted missing ack", unacked, False, "got 0 acks")

    late = [dict(f) for f in good]
    for f in late:
        if f["dir"] == "rx" and f["msg"].get("type") == "ack" and f["msg"].get("seq") == 1:
            f["t"] = 1500.0
    expect("planted late ack", late, False, "acked after")

    slow = [dict(f) for f in good]
    for f in slow:
        if f["dir"] == "rx" and f["msg"].get("type") == "event" and f["msg"].get("cause") == "slew_complete":
            f["t"] = 14000.0
    slow_tlm = sorted(slow, key=lambda f: f["t"])
    expect("planted SLEW over 6 s", slow_tlm, False, "to slew_complete")

    sparse = [f for f in good if not (f["dir"] == "tx" and f["msg"].get("type") == "ping"
                                       and f["page"] == "operator" and 3000 < f["t"] < 6000)]
    expect("planted ping gap (a throttled tab)", sparse, False, "ping gaps")

    nopong = [f for f in good if not (f["dir"] == "rx" and f["msg"].get("type") == "pong" and f["msg"].get("ping_seq") == 110)]
    expect("planted missing pong", nopong, False, "never got a pong")

    chatty = good + [_frame(50, "rx", {"v": 1, "type": "event", "event": "notice", "seq": 1}, page="watcher")]
    chatty.sort(key=lambda f: f["t"])
    expect("planted event to a watch-only page", chatty, False, "before it sent anything")

    lost = good + [_frame(9000, "rx", {"v": 1, "type": "event", "event": "mode", "mode": "SAFE",
                                       "cause": "HEARTBEAT_LOST", "seq": 1, "t_ms": 9000})]
    lost.sort(key=lambda f: f["t"])
    expect("planted HEARTBEAT_LOST", lost, False, "HEARTBEAT_LOST")

    def hidden_run(gap_ms, hidden_s):
        frames = [f for f in good if not (f["dir"] == "tx" and f["msg"].get("type") == "ping" and f["page"] == "operator")
                  and not (f["dir"] == "rx" and f["msg"].get("type") == "pong" and f["page"] == "operator")]
        t, k = 0, 0
        while t < 10000:
            frames.append(_frame(t, "tx", {"v": 1, "type": "ping", "seq": 500 + k, "t_client_ms": t}))
            frames.append(_frame(t + 15, "rx", {"v": 1, "type": "pong", "ping_seq": 500 + k, "t_client_ms": t, "t_ms": t}))
            t += gap_ms
            k += 1
        frames.append({"t": 0.0, "page": "operator", "dir": "meta", "what": "visibility", "state": "visible"})
        if hidden_s:
            frames.append({"t": 100.0, "page": "operator", "dir": "meta", "what": "visibility", "state": "hidden"})
            frames.append({"t": 100.0 + hidden_s * 1000, "page": "operator", "dir": "meta", "what": "visibility", "state": "visible"})
        frames.sort(key=lambda f: f["t"])
        return frames

    def expect_hidden(label, frames, should_pass, needle=None):
        problems = check(frames, expect_hidden=True, quiet=True, min_hidden_s=8.0)
        ok = not problems
        if ok != should_pass or (needle and not any(needle in p for p in problems)):
            failures.append(f"{label}: expected {'pass' if should_pass else 'fail'}, got {problems or 'pass'}")
        print(f"check-ui-capture selftest: {label}: {'passes' if ok else 'fails'}" + (f" ({problems[0]})" if problems else ""))

    expect_hidden("hidden run with 225 ms Worker pings", hidden_run(225, 9.5), True)
    expect_hidden("planted hidden run with 300 ms pings", hidden_run(300, 9.5), False, "ping gaps")
    expect_hidden("planted hidden run with 1 s pings (page timer throttled)", hidden_run(1000, 9.5), False, "ping gaps")
    expect_hidden("planted hidden run that never hid the page", hidden_run(200, 0), False, "hidden for")

    if failures:
        print("check-ui-capture selftest: FAILED", file=sys.stderr)
        for line in failures:
            print(f"  {line}", file=sys.stderr)
        return 1
    print("check-ui-capture selftest: ok, every planted capture fails for its own reason and the clean one passes")
    return 0


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Checks a frames.jsonl from tools/ui-turn-recorder.mjs.")
    parser.add_argument("frames", nargs="?")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--expect-hidden", action="store_true")
    parser.add_argument("--min-hidden-s", type=float, default=30.0)
    args = parser.parse_args()
    if args.selftest:
        sys.exit(selftest())
    if not args.frames:
        parser.error("frames.jsonl is required")
    problems = check(load(args.frames), expect_hidden=args.expect_hidden, min_hidden_s=args.min_hidden_s)
    if problems:
        print("check-ui-capture: FAILED", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        sys.exit(1)
    print("check-ui-capture: ok")


if __name__ == "__main__":
    main()
