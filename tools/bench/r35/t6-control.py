#!/usr/bin/env python3
"""R3.5 Tests item 7: controlling client and heartbeat, with two clients.

    t6-control.py --url ws://direcsun.local/ws --confirm-arm esc-unplugged|julian-go

The item (needs R3.6):
- Tab A arms; tab B's `mode` command is refused with "not controlling client", but
  tab B's `estop` is accepted.
- Closing tab A starts the ramp-down within 1.5 s, with HEARTBEAT_LOST in the event stream.
- Reconnecting does not resume anything.

THIS TEST ARMS THE BOX (twice). See RUNBOOK.md, step 7, before running it. Without
--confirm-arm it prints what it would do and sends nothing.

Two Python clients stand in for the two tabs (the box cannot tell them apart from
browsers: both are WebSocket clients sending schema v1 frames). Sequence:
 1. B connects and pings at 5 Hz from the start, so it receives events, and so the
    test also shows that a non-controlling client's pings cannot keep the box armed.
 2. A connects, pings at 5 Hz (its heartbeat), and arms.
 3. B sends `mode READY`: must be refused "not controlling client".
 4. B sends `estop`: must be acked ok, with event mode SAFE cause ESTOP.
 5. A arms again (retrying while a ramp-down blocks it), then holds for 2 s while
    pinging: the box must stay armed. This is the positive control for step 6:
    while the heartbeat flows, nothing drops the box.
 6. A's socket is closed. B must see event mode SAFE cause HEARTBEAT_LOST within
    1.5 s of the close, and HEARTBEAT_LOST in the tlm flags.
 7. A reconnects as a new client (A2) and pings for 5 s: the box stays SAFE, no mode
    event, throttle and wheel reference never rise, and A2's `mode READY` is refused.
Throttle guard: if any tlm shows throttle above --throttle-guard (default 0: the
R3.5 build's controller always commands 0), B sends estop at once and the run fails.
On exit, if the box is not SAFE, B sends estop.

Note on "ramp-down": FD V-36 starts no ramp when the last throttle was below 48, so
on the R3.5 build (throttle 0) the box drops straight to SAFE with DShot 0. The
script times that SAFE transition; the ramp's shape needs a build with throttle.
"""
import argparse
import asyncio
import sys
import time

import r35common as rc

CONFIRM_VALUES = ("esc-unplugged", "julian-go")
NOT_CONTROLLING = "not controlling client"
PRECONDITION_REASONS = ("battery low", "box not still", "latched fault", "bench_mode required")

SAFETY_TEXT = """\
t6-control will, against {url}:
  - connect client B (pings at 5 Hz) and client A (pings at 5 Hz);
  - send `arm` from A: the box enters SPINUP. On the R3.5 build the throttle controller
    is ZeroThrottleController (firmware/main/control_task.cpp), so SPINUP commands DShot 0,
    but under changes/CONTEXT.md sending `arm` counts as a step that can turn the motor;
  - send `mode READY` from B (refused), then `estop` from B: SAFE, ESTOP latched;
  - send `arm` from A again, hold 2 s, then close A: the heartbeat expires after 1 s
    and the box drops to SAFE with HEARTBEAT_LOST;
  - reconnect as A2, ping 5 s, send `mode READY` (refused); estop if anything is armed at the end.
Run it only with the ESC physically disconnected from D8 and from power
(--confirm-arm esc-unplugged), or with Julian's explicit go for this run if the ESC is
connected (--confirm-arm julian-go). Nothing was sent."""


class Abort(Exception):
    pass


async def sequence(args, run):
    guard = {"tripped": None}
    b = rc.Client("B", args.url)
    a = rc.Client("A", args.url)
    a2 = rc.Client("A2", args.url)
    clients = [b, a, a2]

    def watch(message):
        if message.get("type") == "tlm" and (message.get("throttle") or 0) > args.throttle_guard and not guard["tripped"]:
            guard["tripped"] = message
            run.say(f"THROTTLE GUARD: tlm throttle {message.get('throttle')} > {args.throttle_guard}: estop from B now")
            asyncio.ensure_future(b.cmd("estop"))

    def guard_ok():
        if guard["tripped"]:
            raise Abort(f"throttle guard tripped: {guard['tripped']}")

    b.on_message = watch
    try:
        await b.open()
        first = await b.wait_for(lambda m: m.get("type") == "tlm", 3.0)
        if not first:
            raise Abort("B received no tlm in 3 s")
        tlm0 = first[1]
        run.check("box SAFE at the start", tlm0.get("mode") == "SAFE" and tlm0.get("throttle") == 0,
                  f"mode {tlm0.get('mode')}, throttle {tlm0.get('throttle')}, flags {tlm0.get('flags')}")
        if tlm0.get("mode") != "SAFE":
            raise Abort("the box is armed by someone else: not starting")
        b.start_pings(5)
        await a.open()
        a.start_pings(5)
        await asyncio.sleep(0.5)

        # Step 2: A arms.
        mark = len(b.frames)
        ack, _ = await a.cmd("arm")
        guard_ok()
        if not ack or not ack.get("ok"):
            reason = (ack or {}).get("reason", "no ack within 2 s")
            run.check("A arms", False, f"refused: {reason}")
            if any(reason.startswith(p) for p in PRECONDITION_REASONS):
                raise Abort(f"BLOCKED: arm precondition not met ({reason}). See RUNBOOK step 7.")
            raise Abort(f"arm refused: {reason}")
        event = await b.wait_for(lambda m: m.get("type") == "event" and m.get("event") == "mode"
                                 and m.get("cause") == "arm", 1.5, mark)
        run.check("A arms", True, f"ack ok; B saw {'event mode ' + event[1]['mode'] + ' cause arm' if event else 'NO arm event'}")
        run.check("B (a pinging client) receives the arm event", event is not None, "within 1.5 s" if event else "missing")

        # Step 3: B's mode is refused.
        ack, _ = await b.cmd("mode", mode="READY")
        guard_ok()
        reason = (ack or {}).get("reason")
        run.check("B's mode refused 'not controlling client'", ack is not None and ack.get("ok") is False
                  and reason == NOT_CONTROLLING, f"ack {ack}")
        after = await b.wait_for(lambda m: m.get("type") == "tlm", 1.0, len(b.frames))
        run.check("box still armed after B's refused mode", after is not None and after[1].get("mode") != "SAFE",
                  f"next tlm mode {after[1].get('mode') if after else 'none in 1 s'}")

        # Step 4: B's estop is accepted.
        mark = len(b.frames)
        ack, _ = await b.cmd("estop")
        event = await b.wait_for(lambda m: m.get("type") == "event" and m.get("event") == "mode"
                                 and m.get("cause") == "ESTOP", 1.5, mark)
        safe = await b.wait_for(lambda m: m.get("type") == "tlm" and m.get("mode") == "SAFE", 1.5, mark)
        run.check("B's estop accepted", ack is not None and ack.get("ok") is True and event is not None and safe is not None,
                  f"ack {ack}; event {event[1] if event else None}; tlm SAFE {'yes' if safe else 'no'}")

        # Step 5: A arms again, and the heartbeat holds while A pings.
        deadline = time.monotonic() + args.rearm_wait
        while True:
            mark = len(b.frames)
            ack, _ = await a.cmd("arm")
            guard_ok()
            if ack and ack.get("ok"):
                break
            reason = (ack or {}).get("reason", "no ack")
            if reason != "ramp-down in progress" or time.monotonic() > deadline:
                run.check("A arms again", False, f"refused: {reason}")
                raise Abort(f"re-arm refused: {reason}")
            await asyncio.sleep(0.5)
        run.check("A arms again", True, f"ack ok, latched before: {(b.last_tlm() or {}).get('flags')}")
        hold_mark = len(b.frames)
        await asyncio.sleep(args.hold)
        guard_ok()
        dropped = [m for _t, _x, m in b.frames[hold_mark:] if m and m.get("type") == "event" and m.get("mode") == "SAFE"]
        modes = sorted({m.get("mode") for _t, _x, m in b.frames[hold_mark:] if m and m.get("type") == "tlm"})
        run.check(f"positive control: armed for {args.hold:.0f} s while A pings", not dropped and "SAFE" not in modes,
                  f"tlm modes {modes}; SAFE events {dropped}")
        if dropped or "SAFE" in modes:
            raise Abort("the box left the armed state before A closed: the heartbeat step would prove nothing")

        # Step 6: close A.
        a.stop_pings()
        last_ping = max(t for t, text in a.sent if '"type":"ping"' in text)
        close_mark = len(b.frames)
        t_close = time.monotonic()
        await a.close()
        event = await b.wait_for(lambda m: m.get("type") == "event" and m.get("event") == "mode"
                                 and m.get("mode") == "SAFE", 4.0, close_mark)
        if event is None:
            run.check("SAFE after A closes", False, "no mode SAFE event within 4 s of the close")
        else:
            t_event, message = event
            dt = t_event - t_close
            run.check("cause is HEARTBEAT_LOST", message.get("cause") == "HEARTBEAT_LOST", f"event {message}")
            run.check("ramp-down starts within 1.5 s of closing A", dt <= 1.5,
                      f"{dt * 1000:.0f} ms from the close (A's last ping was {(t_event - last_ping) * 1000:.0f} ms before "
                      f"the event; heartbeat timeout 1000 ms)")
            run.check("not an instant disarm on close (FD 14.4)", None,
                      f"{dt * 1000:.0f} ms" + ("" if dt >= 0.5 else
                                               ": UNDER 500 ms, the box dropped on the close itself, not the heartbeat"))
            flagged = await b.wait_for(lambda m: m.get("type") == "tlm" and "HEARTBEAT_LOST" in (m.get("flags") or []),
                                       1.0, close_mark)
            run.check("HEARTBEAT_LOST in tlm flags", flagged is not None,
                      f"flags {flagged[1].get('flags')}" if flagged else f"last flags {(b.last_tlm() or {}).get('flags')}")
        b_pings = sum(1 for _t, text in b.sent if '"type":"ping"' in text)
        run.check("B pinged throughout, and its pings did not hold the box", None, f"{b_pings} pings from B")

        # Step 7: reconnect resumes nothing.
        await asyncio.sleep(0.3)
        await a2.open()
        a2.start_pings(5)
        watch_mark_b = len(b.frames)
        t_watch = time.monotonic()
        await asyncio.sleep(1.0)
        ack, _ = await a2.cmd("mode", mode="READY")
        await asyncio.sleep(max(0.0, args.resume_watch - (time.monotonic() - t_watch)))
        guard_ok()
        window = [m for _t, _x, m in b.frames[watch_mark_b:] if m] + [m for _t, _x, m in a2.frames if m]
        tlms = [m for _t, _x, m in b.frames[watch_mark_b:] if m and m.get("type") == "tlm"]
        modes = sorted({m.get("mode") for m in window if m.get("type") == "tlm"})
        events = [m for m in window if m.get("type") == "event" and m.get("event") == "mode"]
        rises = [(p.get("throttle"), q.get("throttle"), p.get("wheel_ref_rpm"), q.get("wheel_ref_rpm"))
                 for p, q in zip(tlms, tlms[1:])
                 if (q.get("throttle") or 0) > (p.get("throttle") or 0) or (q.get("wheel_ref_rpm") or 0) > (p.get("wheel_ref_rpm") or 0) + 1]
        run.check("after reconnecting: SAFE throughout", modes == ["SAFE"] and len(tlms) > 40 * args.resume_watch,
                  f"tlm modes {modes} over {len(tlms)} tlm in {args.resume_watch:.0f} s")
        run.check("after reconnecting: no mode events", not events, f"{events}")
        run.check("after reconnecting: throttle and wheel reference never rise", not rises,
                  f"{len(rises)} rise(s)" + (f": {rises[:3]}" if rises else
                                             f"; throttle {tlms[0].get('throttle')}->{tlms[-1].get('throttle')}, "
                                             f"wheel_ref {tlms[0].get('wheel_ref_rpm')}->{tlms[-1].get('wheel_ref_rpm')}"))
        run.check("after reconnecting: A2's mode READY refused", ack is not None and ack.get("ok") is False,
                  f"ack {ack}")
        return clients, None
    except Abort as exc:
        return clients, str(exc)
    finally:
        # Judge the end state from a tlm that arrives now, not a stale one; estop if armed or unknown.
        for client in (b, a2, a):
            if client.ws is None or client.closed_at is not None:
                continue
            fresh = await client.wait_for(lambda m: m.get("type") == "tlm", 0.5, len(client.frames))
            if fresh is None or fresh[1].get("mode") != "SAFE":
                state = fresh[1].get("mode") if fresh else "unknown (no tlm in 0.5 s)"
                run.say(f"cleanup: box {state}: estop from {client.name}")
                await client.cmd("estop")
            break
        for client in clients:
            await client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--confirm-arm", choices=CONFIRM_VALUES)
    parser.add_argument("--throttle-guard", type=int, default=0, help="estop and fail if tlm throttle exceeds this")
    parser.add_argument("--hold", type=float, default=2.0, help="armed hold while A pings (positive control)")
    parser.add_argument("--resume-watch", type=float, default=5.0)
    parser.add_argument("--rearm-wait", type=float, default=7.0, help="retry arm this long while a ramp-down blocks it")
    parser.add_argument("--out")
    args = parser.parse_args()
    if not args.confirm_arm:
        print(SAFETY_TEXT.format(url=args.url))
        return 2
    run = rc.Run("t6-control", args.out)
    run.say(f"--confirm-arm {args.confirm_arm}; throttle guard {args.throttle_guard}")
    try:
        clients, problem = asyncio.run(sequence(args, run))
    except Exception as exc:  # noqa: BLE001 - a client that cannot connect is the finding
        run.check("clients connect", False, f"{type(exc).__name__}: {exc}")
        return run.finish()
    files = []
    for client in clients:
        if client.frames:
            files.append(client.write_jsonl(run.dir / f"{client.name}.jsonl"))
            run.check(f"client {client.name} frames", None, f"{client.types()}")
    ok, summaries = rc.check_schema(run, files, "t6")
    run.check("check-schema on every capture", ok, "; ".join(f"{k.split('/')[-1]}: {v}" for k, v in summaries.items()))
    if problem:
        blocked = problem.startswith("BLOCKED")
        if not blocked:
            run.check("sequence completed", False, problem)
        return run.finish("BLOCKED" if blocked else None, problem if blocked else "")
    return run.finish()


if __name__ == "__main__":
    sys.exit(main())
