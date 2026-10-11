#!/usr/bin/env python3
"""R3.5 Tests item 2: Wi-Fi and mDNS on the bench network.

    t1-mdns.py --url ws://direcsun.local/ws --serial-log serial.log

The item: "the boot log shows the IP and URL; `ping direcsun.local` resolves from
the laptop; both the bench console and the main UI's Device link panel connect at
ws://direcsun.local/ws."

Checks, in order:
 1. The serial log (a capture of `idf.py monitor`, ANSI codes and all) has the
    firmware's lines from direcsun_comms/src/wifi.cpp and ws_server.cpp:
    "wifi: connected, address <ip>", "wifi: WebSocket: ws://<host>.local/ws  or
    ws://<ip>/ws", "wifi: mDNS hostname <host>.local", and "ws: WebSocket server on
    port 80 at /ws". The last connect in the log is the one judged.
 2. Instrument check first: the laptop resolves its own <hostname>.local through
    the same resolver (nss-mdns). If that fails, a failure to resolve the box says
    nothing about the box.
 3. `ping -c 3 <url host>` resolves, and the address equals the boot log's.
 4. A raw WebSocket client at the URL receives a tlm frame.
 5. The bench console (ground/console, served from $R35_REPO) connects and shows a tlm seq.
 6. The main UI's Device link panel (frontend/ground-station) connects: DEVICE CONNECTED,
    the device mode shown, the ignored-frame counter at 0.

--allow-ip lets the URL host be a bare address (to exercise the rest of the
chain without mDNS); the item itself needs the .local name, so the RESULT line
then says mDNS was not exercised.
"""
import argparse
import asyncio
import json
import re
import socket
import sys

import r35common as rc

LINE_CONNECTED = re.compile(r"wifi: connected, address (\d+\.\d+\.\d+\.\d+)")
LINE_URL = re.compile(r"wifi: WebSocket: ws://(\S+?)/ws\s+or\s+ws://(\S+?)/ws")
LINE_MDNS = re.compile(r"wifi: mDNS hostname (\S+)")
LINE_MDNS_FAILED = re.compile(r"wifi: mDNS failed \((\S+)\)")
LINE_SERVER = re.compile(r"ws: WebSocket server on port (\d+) at (\S+)")
LINE_NO_WIFI = re.compile(r"wifi: no Wi-Fi network configured")
LINE_DISCONNECTED = re.compile(r"wifi: disconnected \(reason (-?\d+)\)")


def resolve_v4(host, run, label, timeout=10):
    code, text = rc.run_logged(["getent", "ahostsv4", host], run.dir / f"getent-{label}.txt", timeout=timeout)
    addresses = []
    for line in text.splitlines()[1:]:
        parts = line.split()
        if parts and re.fullmatch(r"\d+\.\d+\.\d+\.\d+", parts[0]) and parts[0] not in addresses:
            addresses.append(parts[0])
    return code, addresses


async def first_tlm(url):
    client = rc.Client("raw", url)
    await client.open()
    hit = await client.wait_for(lambda m: m.get("type") == "tlm", 3.0)
    await client.close()
    return hit[1] if hit else None, client.types()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--serial-log", required=True, help="capture of idf.py monitor that includes the boot")
    parser.add_argument("--allow-ip", action="store_true", help="accept a bare IP in --url (mDNS then not exercised)")
    parser.add_argument("--self-name", help="name for the resolver check (default: this laptop's <hostname>.local)")
    parser.add_argument("--out", help="results root (default $R35_OUT or ./r35-results)")
    args = parser.parse_args()
    run = rc.Run("t1-mdns", args.out)
    host, port, path = rc.parse_ws_url(args.url)
    url_is_ip = re.fullmatch(r"\d+\.\d+\.\d+\.\d+", host) is not None

    # 1. Boot log.
    try:
        log = rc.strip_ansi(open(args.serial_log, errors="replace").read())
    except OSError as exc:
        run.check("serial log readable", False, str(exc))
        return run.finish()
    (run.dir / "serial-log-copy.txt").write_text(log)
    lines = log.splitlines()
    run.check("serial log readable", True, f"{args.serial_log}: {len(lines)} lines")
    if any(LINE_NO_WIFI.search(l) for l in lines):
        run.check("Wi-Fi configured", False, "the box logged 'no Wi-Fi network configured': put CONFIG_DIRECSUN_WIFI_SSID "
                  "and _PASSWORD in firmware/sdkconfig.defaults.local, delete firmware/sdkconfig, rebuild, reflash")
    connected = [m.group(1) for l in lines if (m := LINE_CONNECTED.search(l))]
    urls = [(m.group(1), m.group(2)) for l in lines if (m := LINE_URL.search(l))]
    mdns = [m.group(1) for l in lines if (m := LINE_MDNS.search(l))]
    mdns_failed = [m.group(1) for l in lines if (m := LINE_MDNS_FAILED.search(l))]
    servers = [(m.group(1), m.group(2)) for l in lines if (m := LINE_SERVER.search(l))]
    drops = [m.group(1) for l in lines if (m := LINE_DISCONNECTED.search(l))]
    boot_ip = connected[-1] if connected else None
    run.check("boot log: IP line", bool(connected),
              f"'connected, address {boot_ip}' ({len(connected)} connect(s) in the log)" if connected
              else "no 'wifi: connected, address' line: the box never got an address, or the log missed the boot")
    url_line_ok = bool(urls) and boot_ip is not None and urls[-1][1] == boot_ip
    run.check("boot log: URL line", url_line_ok,
              f"'WebSocket: ws://{urls[-1][0]}/ws  or  ws://{urls[-1][1]}/ws'" if urls else "no 'wifi: WebSocket:' line")
    mdns_name = mdns[-1] if mdns else None
    run.check("boot log: mDNS hostname", bool(mdns) and not mdns_failed,
              f"'mDNS hostname {mdns_name}'" if mdns and not mdns_failed else
              f"mDNS failed ({', '.join(mdns_failed)})" if mdns_failed else "no 'wifi: mDNS hostname' line")
    run.check("boot log: server on port 80 at /ws", ("80", "/ws") in servers,
              f"{servers[-1]}" if servers else "no 'ws: WebSocket server on port' line")
    run.check("boot log: Wi-Fi drops", None, f"{len(drops)} 'wifi: disconnected' line(s)"
              + (f", reasons {sorted(set(drops))}" if drops else ""))

    # 2. Resolver instrument check, then the box's name.
    if url_is_ip:
        run.check("URL uses the mDNS name", None if args.allow_ip else False,
                  f"URL host {host} is an address; the item needs ws://<host>.local/ws"
                  + (" (--allow-ip: mDNS not exercised)" if args.allow_ip else ""))
    else:
        own = args.self_name or socket.gethostname().split(".")[0] + ".local"
        code, own_addrs = resolve_v4(own, run, "self")
        run.check("resolver works (laptop's own .local name)", bool(own_addrs),
                  f"{own} -> {', '.join(own_addrs)}" if own_addrs
                  else f"{own} did not resolve (getent exit {code}): fix nss-mdns/avahi on the laptop before judging the box")
        if mdns_name:
            run.check("URL host is the box's mDNS name", host == mdns_name,
                      f"URL host {host}, boot log {mdns_name}")
    code, text = rc.run_logged(["ping", "-c", "3", "-W", "2", host], run.dir / "ping.txt", timeout=20)
    match = re.search(r"^PING (\S+) \((\d+\.\d+\.\d+\.\d+)\)", text, re.M)
    received = re.search(r"(\d+) received", text)
    ping_ip = match.group(2) if match else None
    run.check("ping resolves the URL host", ping_ip is not None,
              f"ping {host} -> {ping_ip}, {received.group(1) if received else 0}/3 replies (exit {code})"
              if ping_ip else f"ping could not resolve {host}: {text.strip().splitlines()[-1] if text.strip() else 'no output'}")
    if ping_ip and boot_ip:
        run.check("resolved address equals the boot log's", ping_ip == boot_ip, f"ping {ping_ip}, boot log {boot_ip}")
    run.check("URL port and path", None, f"port {port}, path {path}" + (
        "" if (port, path) == (80, "/ws") else " (the box serves port 80 at /ws: this is not the box)"))

    # 3. Raw client.
    try:
        tlm, types = asyncio.run(first_tlm(args.url))
        run.check("raw client receives tlm", tlm is not None,
                  f"first tlm seq {tlm['seq']} mode {tlm['mode']}; frames {types}" if tlm else f"no tlm in 3 s; frames {types}")
    except Exception as exc:  # noqa: BLE001 - any failure to connect is the finding
        run.check("raw client receives tlm", False, f"{type(exc).__name__}: {exc}")

    # 4. Bench console, 5. main UI.
    try:
        with rc.console_server(run) as base:
            result = rc.probe(run, "console", args.url, base, ["--watch-s", "3"], timeout=90)
        detail = result.get("console") or {}
        run.check("bench console connects", result.get("ok") is True and "CONNECTED" in detail.get("state", ""),
                  f"state {detail.get('state', '').strip()!r}, {detail.get('seq')!r}, mode {detail.get('mode')!r}"
                  if result.get("ok") else result.get("error"))
    except RuntimeError as exc:
        run.check("bench console connects", False, f"could not serve the console: {exc}")
    try:
        with rc.ui_server(run) as base:
            result = rc.probe(run, "connect", args.url, base, ["--watch-s", "5"], timeout=120)
        if result.get("ok"):
            frames = result.get("wsFrames", {})
            run.check("main UI Device link connects", result.get("headerAlwaysLive") is True,
                      f"header live in every sample; mode {result.get('modesSeen')}; packets {result.get('packetsLast')}")
            run.check("main UI ignored counter 0", result.get("ignoredMax") == 0,
                      f"max ignored {result.get('ignoredMax')}; page received {frames.get('received')}, sent {frames.get('sent')}")
        else:
            run.check("main UI Device link connects", False, result.get("error"))
    except RuntimeError as exc:
        run.check("main UI Device link connects", False, f"could not serve the UI: {exc}")

    summary = f"boot IP {boot_ip}, URL ws://{urls[-1][0]}/ws" if urls else f"boot IP {boot_ip}"
    if url_is_ip and args.allow_ip:
        summary += "; mDNS NOT exercised (--allow-ip)"
    return run.finish(summary=summary if not any(c["ok"] is False for c in run.checks) else "")


if __name__ == "__main__":
    sys.exit(main())
