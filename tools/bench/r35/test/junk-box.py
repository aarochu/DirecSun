#!/usr/bin/env python3
"""Mock harness only: a box that misbehaves on purpose, to prove the kit's checks can fail.

    test/junk-box.py --port <port> [--junk-every 1.0] [--tlm-hz 50] [--bad-tlm]
                     [--drop-every N] [--pong-delay-ms MS]

Serves /ws: schema-valid tlm at --tlm-hz to every client, t_ms from its own clock.
  --junk-every S     every S seconds, also a frame no correct box sends (type "debug"),
                     which the main UI cannot place; 0 turns it off.
  --bad-tlm          every tenth tlm lacks the required field "vbat" (schema failure).
  --drop-every N     skip every Nth tlm while time runs on (1/N telemetry loss).
  --pong-delay-ms    answer each ping with a valid pong after this delay; without it,
                     pings are never answered. It never acks anything.
  --stall-every S --stall-ms MS
                     every S seconds the sender stalls for MS, then sends the frames it
                     owes back to back, each stamped when it is sent: nothing is lost,
                     but t_ms shows one long gap followed by short ones.
"""
import argparse
import asyncio
import json
import time

from websockets.asyncio.server import broadcast, serve


def tlm(seq, t0, bad):
    message = {"v": 1, "type": "tlm", "seq": seq, "t_ms": int((time.monotonic() - t0) * 1000), "mode": "SAFE",
               "manual_input": None, "psi_deg": 0.0, "psi_ref_deg": None, "rate_dps": 0.0, "mag_deg": None,
               "sun_deg": None, "sun_valid": False, "wheel_rpm": 0.0, "wheel_ref_rpm": 0.0, "throttle": 0,
               "tau_cmd_mNm": 0.0, "vbat": 10.0, "loop_us": 700, "flags": [], "t_c": None, "p_pa": None,
               "tlm_dropped": 0, "dropped_events": 0}
    if bad and seq % 10 == 0:
        del message["vbat"]
    return message


async def main(args):
    clients = set()
    t0 = time.monotonic()
    seq = 0

    def next_seq():
        nonlocal seq
        seq += 1
        return seq

    async def pong_later(conn, ping):
        await asyncio.sleep(args.pong_delay_ms / 1000.0)
        try:
            await conn.send(json.dumps({"v": 1, "type": "pong", "seq": next_seq(), "ping_seq": ping["seq"],
                                        "t_client_ms": ping["t_client_ms"], "t_ms": int((time.monotonic() - t0) * 1000)}))
        except Exception:  # noqa: BLE001 - the client may have gone
            pass

    async def handler(conn):
        clients.add(conn)
        try:
            async for text in conn:
                if args.pong_delay_ms is None:
                    continue
                try:
                    message = json.loads(text)
                except json.JSONDecodeError:
                    continue
                if isinstance(message, dict) and message.get("type") == "ping":
                    asyncio.ensure_future(pong_later(conn, message))
        finally:
            clients.discard(conn)

    async def ticker():
        count = 0
        next_at = time.monotonic()
        next_junk = time.monotonic() + args.junk_every if args.junk_every > 0 else float("inf")
        next_stall = time.monotonic() + args.stall_every if args.stall_every > 0 else float("inf")
        while True:
            if time.monotonic() >= next_stall:
                next_stall += args.stall_every
                time.sleep(args.stall_ms / 1000.0)
            count += 1
            if not (args.drop_every and count % args.drop_every == 0):
                broadcast(clients, json.dumps(tlm(next_seq(), t0, args.bad_tlm)))
            if time.monotonic() >= next_junk:
                next_junk += args.junk_every
                broadcast(clients, json.dumps({"v": 1, "type": "debug", "seq": next_seq(), "text": "junk"}))
            next_at += 1.0 / args.tlm_hz
            await asyncio.sleep(max(0.0, next_at - time.monotonic()))

    async with serve(handler, args.host, args.port):
        print(f"junk-box on ws://{args.host}:{args.port}/ws", flush=True)
        await ticker()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--junk-every", type=float, default=1.0)
    parser.add_argument("--tlm-hz", type=float, default=50.0)
    parser.add_argument("--bad-tlm", action="store_true")
    parser.add_argument("--drop-every", type=int, default=0)
    parser.add_argument("--pong-delay-ms", type=float)
    parser.add_argument("--stall-every", type=float, default=0.0)
    parser.add_argument("--stall-ms", type=float, default=100.0)
    asyncio.run(main(parser.parse_args()))
