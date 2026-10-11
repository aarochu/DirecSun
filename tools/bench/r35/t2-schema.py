#!/usr/bin/env python3
"""R3.5 Tests item 3: firmware traffic matches the schema.

    t2-schema.py --url ws://direcsun.local/ws [--seconds 60]

The item: "tools/capture-box.py for 60 s, then tools/check-schema.py on the capture,
gives 0 failures across at least 2900 tlm frames plus the acks and pongs."

capture-box only pings, so its capture holds tlm and pong but never an ack. To get
acks without arming anything, a second client (the "commander") runs alongside it
and, once a second, sends a frame the box must refuse with an ack and never act on:
`disarm` while SAFE, malformed JSON, an unknown cmd, `v` 2, and an unknown type.
Both captures go through check-schema. Counts are taken from the capture files
themselves, not from the tools' summaries, and cross-checked against them.

Nothing here can arm the box or change its parameters. If the box is somehow armed
when this starts, the script stops before sending anything.
"""
import argparse
import asyncio
import json
import re
import sys

import r35common as rc

REFUSALS = [
    ("disarm while SAFE", lambda seq: {"v": 1, "type": "cmd", "seq": seq, "cmd": "disarm"}),
    ("malformed JSON", lambda seq: '{"v":1,"type":"cmd","seq":' + str(seq) + ',"cmd":'),
    ("unknown cmd", lambda seq: {"v": 1, "type": "cmd", "seq": seq, "cmd": "r35kit_no_such_cmd"}),
    ("v 2", lambda seq: {"v": 2, "type": "cmd", "seq": seq, "cmd": "disarm"}),
    ("unknown type", lambda seq: {"v": 1, "type": "r35kit_no_such_type", "seq": seq}),
]


async def commander(url, seconds, run):
    client = rc.Client("commander", url)
    await client.open()
    first = await client.wait_for(lambda m: m.get("type") == "tlm", 3.0)
    if first is None or first[1].get("mode") != "SAFE":
        await client.close()
        return client, [], f"box not in SAFE before the run (first tlm: {first[1] if first else None})"
    sent = []
    loop = asyncio.get_running_loop()
    end = loop.time() + seconds - 1.5
    i = 0
    while loop.time() < end:
        label, build = REFUSALS[i % len(REFUSALS)]
        seq = client.next_seq()
        await client.send(build(seq))
        sent.append((label, seq))
        i += 1
        await asyncio.sleep(1.0)
    await asyncio.sleep(1.0)
    await client.close()
    return client, sent, None


async def both(args, run):
    cap_cmd = [rc.python(), rc.repo_dir() / "tools" / "capture-box.py", "--url", args.url,
               "--seconds", str(args.seconds), "--out", run.dir / "capture.jsonl"]
    log = open(run.dir / "capture-box.txt", "w")
    log.write("$ " + " ".join(map(str, cap_cmd)) + "\n")
    log.flush()
    proc = await asyncio.create_subprocess_exec(*map(str, cap_cmd), stdout=log, stderr=asyncio.subprocess.STDOUT)
    await asyncio.sleep(0.5)
    try:
        client, sent, problem = await commander(args.url, args.seconds, run)
    except Exception as exc:  # noqa: BLE001
        client, sent, problem = None, [], f"commander could not run: {type(exc).__name__}: {exc}"
    code = await proc.wait()
    log.close()
    return code, client, sent, problem


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--min-tlm", type=int, default=2900)
    parser.add_argument("--out")
    args = parser.parse_args()
    run = rc.Run("t2-schema", args.out)

    run.say(f"capturing {args.seconds:.0f} s from {args.url} (capture-box with pings, plus the commander)")
    code, client, sent, problem = asyncio.run(both(args, run))
    cap_text = (run.dir / "capture-box.txt").read_text(errors="replace")
    for line in cap_text.splitlines()[1:]:
        run.say(f"    {line}")
    if problem:
        run.check("commander", False, problem)
    run.check("capture-box ran", code == 0, f"exit {code}")

    capture = run.dir / "capture.jsonl"
    kinds = {}
    if capture.exists():
        for line in capture.read_text().splitlines():
            try:
                kind = json.loads(line).get("type", "?")
            except json.JSONDecodeError:
                kind = "(not JSON)"
            kinds[kind] = kinds.get(kind, 0) + 1
    tlm, pongs = kinds.get("tlm", 0), kinds.get("pong", 0)
    tool_tlm = re.search(r"capture-box: tlm (\d+) frames", cap_text)
    run.check(f"at least {args.min_tlm} tlm", tlm >= args.min_tlm,
              f"{tlm} tlm in capture.jsonl (capture-box says {tool_tlm.group(1) if tool_tlm else '?'})")
    run.check("pongs captured", pongs > 0, f"{pongs} pong")
    if tool_tlm:
        run.check("file count matches capture-box's own count", int(tool_tlm.group(1)) == tlm,
                  f"file {tlm}, capture-box {tool_tlm.group(1)}")

    files = [capture]
    if client is not None:
        commander_file = client.write_jsonl(run.dir / "commander.jsonl")
        files.append(commander_file)
        acks = [m for m in client.messages() if m.get("type") == "ack"]
        by_seq = {}
        for ack in acks:
            by_seq.setdefault(ack["seq"], []).append(ack)
        answered = 0
        for label, seq in sent:
            # A frame the box cannot read has no readable seq, so its ack carries seq 0 (schema).
            expect = 0 if label == "malformed JSON" else seq
            if by_seq.get(expect):
                by_seq[expect].pop(0)
                answered += 1
        refused_ok = [a for a in acks if a.get("ok") is True]
        reasons = sorted({a["reason"][:40] for a in acks if a.get("reason")})
        run.check("acks captured", len(acks) > 0, f"{len(acks)} ack for {len(sent)} refused frames sent; "
                  f"reasons {reasons}")
        run.check("every refused frame got one ack", answered == len(sent) and len(acks) == len(sent),
                  f"{answered}/{len(sent)} answered, {len(acks)} acks")
        run.check("nothing the commander sent was accepted", not refused_ok,
                  f"{len(refused_ok)} ack ok:true" + (f": {refused_ok[:3]}" if refused_ok else ""))
        run.check("commander frames", None, f"{client.types()}")

    ok, summaries = rc.check_schema(run, files, "t2")
    run.check("check-schema: 0 failures", ok, "; ".join(f"{k.split('/')[-1]}: {v}" for k, v in summaries.items()))
    return run.finish(summary=f"tlm {tlm}, pong {pongs}, ack {len([m for m in client.messages() if m.get('type') == 'ack']) if client else 0}, schema {'0 failures' if ok else 'FAILURES'}"
                      if not any(c['ok'] is False for c in run.checks) else "")


if __name__ == "__main__":
    sys.exit(main())
