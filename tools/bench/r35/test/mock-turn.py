#!/usr/bin/env python3
"""Mock harness only: turn tools/mock-box.py at a commanded rate, standing in for a hand.

    test/mock-turn.py --url ws://127.0.0.1:<port>/ws --dps 30 --seconds 3

Arms the mock, waits for READY, enters MANUAL rate, sends rate_dps at 10 Hz for the
given time (wire frame: positive is clockwise), then returns to READY and disarms.
It ARMS whatever it connects to, so it refuses port 80, the real box's port.
"""
import argparse
import asyncio
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import r35common as rc  # noqa: E402


async def turn(args):
    client = rc.Client("turner", args.url)
    await client.open()
    client.start_pings(5)
    ack, _ = await client.cmd("arm")
    print(f"arm: {ack}", flush=True)
    ready = await client.wait_for(lambda m: m.get("type") == "tlm" and m.get("mode") == "READY", 12.0)
    print(f"READY: {bool(ready)}", flush=True)
    ack, _ = await client.cmd("mode", mode="MANUAL", input="rate")
    print(f"mode MANUAL rate: {ack}", flush=True)
    loop = asyncio.get_running_loop()
    end = loop.time() + args.seconds
    sent = 0
    while loop.time() < end:
        await client.cmd("manual", rate_dps=args.dps, timeout=0.5)
        sent += 1
        await asyncio.sleep(0.1)
    print(f"sent {sent} manual setpoints of {args.dps} deg/s", flush=True)
    for _ in range(5):
        await client.cmd("manual", rate_dps=0, timeout=0.5)
        await asyncio.sleep(0.1)
    print(f"mode READY: {(await client.cmd('mode', mode='READY'))[0]}", flush=True)
    print(f"disarm: {(await client.cmd('disarm'))[0]}", flush=True)
    await client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", required=True)
    parser.add_argument("--dps", type=float, default=30.0)
    parser.add_argument("--seconds", type=float, default=3.0)
    args = parser.parse_args()
    _host, port, _path = rc.parse_ws_url(args.url)
    if port == 80:
        sys.exit("mock-turn: refusing port 80 (the real box): this script arms what it connects to")
    asyncio.run(turn(args))


if __name__ == "__main__":
    main()
