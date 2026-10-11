#!/usr/bin/env python3
"""Check a UI turn recorder's timestamped WebSocket frames.

Each JSONL row has at_ms, client, direction (send or recv), and frame. This is
an observation of the UI, not a substitute for the box's own test results.
"""

import argparse
import json
import statistics
import sys
from pathlib import Path


def load(path):
    rows = []
    for line_number, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip():
            continue
        row = json.loads(line)
        if not isinstance(row.get("at_ms"), (int, float)) or not isinstance(row.get("frame"), dict):
            raise ValueError(f"line {line_number}: expected at_ms and frame")
        if row.get("direction") not in ("send", "recv") or not isinstance(row.get("client"), str):
            raise ValueError(f"line {line_number}: expected client and send/recv direction")
        rows.append(row)
    if not rows:
        raise ValueError("empty capture")
    return sorted(rows, key=lambda row: row["at_ms"])


def check(rows, scenario="turn"):
    failures = []
    notes = []
    commands = [row for row in rows if row["direction"] == "send" and row["frame"].get("type") == "cmd"]
    if not commands:
        failures.append("no commands captured")
    latencies = []
    for command in commands:
        seq = command["frame"].get("seq")
        replies = [row for row in rows if row["client"] == command["client"] and row["direction"] == "recv"
                   and row["frame"].get("type") == "ack" and row["frame"].get("seq") == seq
                   and row["at_ms"] >= command["at_ms"]]
        if len(replies) != 1:
            failures.append(f"{command['client']} command seq {seq}: expected one ack, got {len(replies)}")
            continue
        delay = replies[0]["at_ms"] - command["at_ms"]
        latencies.append(delay)
        if delay > 1000:
            failures.append(f"command seq {seq}: ack took {delay:.0f} ms")
        if replies[0]["frame"].get("ok") is not True:
            notes.append(f"command seq {seq} refused: {replies[0]['frame'].get('reason', 'no reason')}")
    if latencies:
        notes.append(f"ack p50 {statistics.median(latencies):.0f} ms, max {max(latencies):.0f} ms")

    received = [row for row in rows if row["direction"] == "recv"]
    mode_events = [row for row in received if row["frame"].get("type") == "event"
                   and row["frame"].get("event") == "mode"]
    for event in mode_events:
        later = next((row for row in received if row["client"] == event["client"]
                      and row["frame"].get("type") == "tlm" and row["at_ms"] > event["at_ms"]), None)
        if later is None:
            failures.append(f"mode event {event['frame'].get('mode')}: no later telemetry")
        elif later["frame"].get("mode") != event["frame"].get("mode"):
            failures.append(f"stale telemetry after {event['frame'].get('cause', 'mode event')}: "
                            f"{later['frame'].get('mode')} != {event['frame'].get('mode')}")

    slew_commands = [row for row in commands if row["frame"].get("cmd") == "mode"
                     and row["frame"].get("mode") == "SLEW"]
    for command in slew_commands:
        completions = [event for event in mode_events if event["client"] == command["client"]
                       and event["frame"].get("cause") == "slew_complete"
                       and command["at_ms"] < event["at_ms"] <= command["at_ms"] + 6000]
        if not completions:
            interrupted = scenario in ("stop", "estop") and any(
                row["client"] == command["client"] and row["at_ms"] > command["at_ms"]
                and row["frame"].get("cmd") in ("estop", "mode")
                and row["frame"].get("mode", "READY") == "READY" for row in commands)
            if not interrupted:
                failures.append(f"SLEW seq {command['frame'].get('seq')}: no slew_complete within 6 s")

    for client in {row["client"] for row in rows}:
        sends = [row for row in rows if row["client"] == client and row["direction"] == "send"]
        pings = [row for row in sends if row["frame"].get("type") == "ping"]
        for first, second in zip(pings, pings[1:]):
            gap = second["at_ms"] - first["at_ms"]
            if not 140 <= gap <= 260:
                failures.append(f"{client}: ping gap {gap:.0f} ms (expected 200 +/- 60)")
        for ping in pings:
            pong = [row for row in received if row["client"] == client and row["frame"].get("type") == "pong"
                    and row["frame"].get("ping_seq") == ping["frame"].get("seq")
                    and row["at_ms"] >= ping["at_ms"]]
            if len(pong) != 1:
                failures.append(f"{client}: ping seq {ping['frame'].get('seq')} got {len(pong)} pongs")
        first_send = sends[0]["at_ms"] if sends else float("inf")
        before = [row for row in received if row["client"] == client and row["at_ms"] < first_send]
        if before and any(row["frame"].get("type") != "tlm" for row in before):
            failures.append(f"{client}: watch-only page received a non-tlm frame before sending")

    phases = [row["frame"].get("t_ms") % 20 for row in received if row["frame"].get("type") == "tlm"
              and isinstance(row["frame"].get("t_ms"), (int, float))]
    if phases:
        notes.append(f"common telemetry phase t_ms mod 20: {statistics.mode(phases):.1f} ms")
    return failures, notes


def selftest():
    base = [
        {"at_ms": 0, "client": "operator", "direction": "send", "frame": {"type": "cmd", "cmd": "mode", "mode": "SLEW", "seq": 1}},
        {"at_ms": 20, "client": "operator", "direction": "recv", "frame": {"type": "ack", "seq": 1, "ok": True}},
        {"at_ms": 1000, "client": "operator", "direction": "recv", "frame": {"type": "event", "event": "mode", "mode": "READY", "cause": "slew_complete"}},
        {"at_ms": 1020, "client": "operator", "direction": "recv", "frame": {"type": "tlm", "mode": "READY", "t_ms": 1020}},
    ]
    if check(base)[0]:
        print("selftest: valid capture failed", file=sys.stderr)
        return 1
    stale = [dict(row) for row in base]
    stale[-1] = {**stale[-1], "frame": {"type": "tlm", "mode": "SLEW", "t_ms": 1020}}
    missing_ack = [row for row in base if row["frame"].get("type") != "ack"]
    for name, capture, needle in (("stale telemetry", stale, "stale telemetry"),
                                  ("missing ack", missing_ack, "expected one ack")):
        failures, _ = check(capture)
        if not any(needle in failure for failure in failures):
            print(f"selftest: {name} falsifier was not caught: {failures}", file=sys.stderr)
            return 1
        print(f"selftest: caught {name}")
    print("selftest: PASS")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", help="frames.jsonl from ui-turn-recorder.mjs")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--scenario", choices=("turn", "stop", "estop", "watch", "hidden"), default="turn")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.capture:
        parser.error("capture is required unless --selftest is used")
    try:
        failures, notes = check(load(args.capture), args.scenario)
    except (ValueError, OSError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}")
        return 1
    for note in notes:
        print(note)
    for failure in failures:
        print(f"FAIL: {failure}")
    print("PASS" if not failures else f"FAIL ({len(failures)} checks)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
