#!/usr/bin/env python3
"""Run every kit script against the mock box: the positive cases and the falsifiers.

    . env.sh && test/mock-suite.py [--only t3] [--skip-long]

Each case gets its own server (the stock tools/mock-box.py, a deliberately broken
copy of it, test/junk-box.py, or none) on a free port, and the server is stopped
when the case ends. A case's full output goes to results/mock-<time>/<case>.txt;
the suite then reads that file for the RESULT line and for the check lines the case
must show (for a falsifier: the specific [FAIL] line it exists to provoke). The
table is written to SUMMARY.txt. Touches no hardware: every URL is local.
"""
import argparse
import datetime
import os
import pathlib
import re
import signal
import socket
import subprocess
import sys
import threading
import time

KIT = pathlib.Path(__file__).resolve().parent.parent
REPO = pathlib.Path(os.environ["R35_REPO"])
PY = os.environ["DIRECSUN_PYTHON"]
SELF = socket.gethostname().split(".")[0]
# Optional real R3.4 serial logs for the t7 cross-checks; those cases are skipped
# when unset (the flight log is testresults/2026-10-10-s3-control-task-fixed/ on main).
R34_FLIGHT_LOG = os.environ.get("R35_R34_FLIGHT_LOG", "")
R34_DIAG_LOG = os.environ.get("R35_R34_DIAG_LOG", "")

# Broken copies of the mock: (old text, new text) pairs, each of which must match exactly once.
VARIANTS = {
    # The read-only rule removed: events go to every client, as if the firmware broadcast them.
    "leaky": [("broadcast([c for c in clients if c in box.talkers], text)", "broadcast(clients, text)")],
    # Heartbeat 3 s instead of 1 s.
    "slowhb": [("HEARTBEAT_S = 1.0 ", "HEARTBEAT_S = 3.0 ")],
    # The controlling-client rule removed.
    "nocontroller": [("if armed and conn is not self.controller:", "if False:")],
    # Every arm refused the way the firmware refuses it with the battery divider unwired.
    "battery": [('        if cmd == "arm":\n', '        if cmd == "arm":\n            return self._ack(conn, seq, False, "battery low")\n')],
    # Firmware-like: an estop in SAFE still posts event mode SAFE cause ESTOP (modes.cpp step 4, HT-8).
    "fwestop": [('            self._ack(conn, seq, True)\n            self._to_safe("ESTOP", latch="ESTOP")\n',
                 '            self._ack(conn, seq, True)\n            was_safe = self.mode == "SAFE"\n'
                 '            self._to_safe("ESTOP", latch="ESTOP")\n            if was_safe:\n'
                 '                self._emit(None, {"v": 1, "type": "event", "seq": self._next_seq(), "t_ms": self._t_ms(),\n'
                 '                                  "event": "mode", "mode": "SAFE", "cause": "ESTOP"})\n')],
}


def make_variants(suite):
    source = (REPO / "tools" / "mock-box.py").read_text()
    anchor = "REPO = pathlib.Path(__file__).resolve().parent.parent"
    assert source.count(anchor) == 1
    for name, patches in VARIANTS.items():
        text = source.replace(anchor, f"REPO = pathlib.Path({str(REPO)!r})")
        for old, new in patches:
            if text.count(old) != 1:
                sys.exit(f"mock-suite: variant {name}: patch text found {text.count(old)} times, not once: {old!r}")
            text = text.replace(old, new)
        (suite / f"mock-{name}.py").write_text(text)


def free_port(host):
    with socket.socket() as s:
        s.bind((host, 0))
        return s.getsockname()[1]


def wait_port(host, port, timeout=10.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with socket.socket() as s:
            if s.connect_ex((host, port)) == 0:
                return True
        time.sleep(0.1)
    return False


class Proc:
    def __init__(self, cmd, log_path):
        self.log = open(log_path, "w")
        self.proc = subprocess.Popen([str(c) for c in cmd], stdout=self.log, stderr=subprocess.STDOUT,
                                     start_new_session=True)

    def stop(self):
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGTERM)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait()
        self.log.close()


def bridge_address():
    out = subprocess.run(["getent", "ahostsv4", f"{SELF}.local"], capture_output=True, text=True, timeout=10).stdout
    return out.split()[0] if out.strip() else None


def cases(suite, br):
    t = lambda name: str(KIT / name)
    fx = lambda name: str(suite / name)
    turn = lambda dps: f"{PY} {KIT / 'test' / 'mock-turn.py'} --url {{url}} --dps {dps} --seconds 3"
    mdns_url = f"ws://{SELF}.local:{{port}}/ws"
    flight, diag = R34_FLIGHT_LOG, R34_DIAG_LOG
    F = lambda label: rf"\[FAIL\] {re.escape(label)}"
    OK = lambda label: rf"\[ok  \] {re.escape(label)}"
    return [
        # ---- t1: mDNS, boot log, console, UI ----
        dict(name="t1-pass", server=("mock", "stock", br), url=mdns_url, expect="PASS",
             cmd=[t("t1-mdns.py"), "--url", "{url}", "--serial-log", fx("boot-ok.log")]),
        dict(name="t1-f-wrong-ip", server=("mock", "stock", br), url=mdns_url, expect="FAIL",
             cmd=[t("t1-mdns.py"), "--url", "{url}", "--serial-log", fx("boot-wrong-ip.log")],
             must=[F("resolved address equals the boot log's")]),
        dict(name="t1-f-no-wifi", server=("mock", "stock", br), url=mdns_url, expect="FAIL",
             cmd=[t("t1-mdns.py"), "--url", "{url}", "--serial-log", fx("boot-no-wifi.log")],
             must=[F("Wi-Fi configured"), F("boot log: IP line"), F("boot log: URL line")]),
        dict(name="t1-f-unresolvable", server=("mock", "stock", br), url="ws://nosuchbox-r35kit.local:{port}/ws",
             expect="FAIL", cmd=[t("t1-mdns.py"), "--url", "{url}", "--serial-log", fx("boot-nosuch.log")],
             must=[F("ping resolves the URL host"), F("raw client receives tlm")]),
        dict(name="t1-f-resolver", server=("mock", "stock", br), url=mdns_url, expect="FAIL",
             cmd=[t("t1-mdns.py"), "--url", "{url}", "--serial-log", fx("boot-ok.log"), "--self-name",
                  "nosuchself-r35kit.local"], must=[F("resolver works")]),
        dict(name="t1-f-no-server", server=None, host=br, url=mdns_url, expect="FAIL",
             cmd=[t("t1-mdns.py"), "--url", "{url}", "--serial-log", fx("boot-ok.log")],
             must=[F("raw client receives tlm"), F("bench console connects"), F("main UI Device link connects")]),
        # ---- t2: schema conformance ----
        dict(name="t2-pass", server=("mock", "stock", "127.0.0.1"), expect="PASS", cmd=[t("t2-schema.py"), "--url", "{url}"]),
        dict(name="t2-f-short", server=("mock", "stock", "127.0.0.1"), expect="FAIL",
             cmd=[t("t2-schema.py"), "--url", "{url}", "--seconds", "20"], must=[F("at least 2900 tlm")]),
        dict(name="t2-f-bad-tlm", server=("junk", ["--junk-every", "0", "--bad-tlm"]), expect="FAIL",
             cmd=[t("t2-schema.py"), "--url", "{url}", "--seconds", "15"],
             must=[F("check-schema: 0 failures"), F("acks captured")]),
        dict(name="t2-f-no-server", server=None, expect="FAIL", cmd=[t("t2-schema.py"), "--url", "{url}", "--seconds", "5"],
             must=[F("capture-box ran")]),
        # ---- t3: main UI reads the box ----
        dict(name="t3-pass", server=("mock", "stock", "127.0.0.1"), expect="PASS",
             cmd=[t("t3-ui.py"), "--url", "{url}", "--turn-s", "14", "--turn-cmd", turn(30)]),
        dict(name="t3-f-ccw", server=("mock", "stock", "127.0.0.1"), expect="FAIL",
             cmd=[t("t3-ui.py"), "--url", "{url}", "--turn-s", "14", "--turn-cmd", turn(-30)],
             must=[F("heading readout increased (clockwise)"), F("dial needle turned clockwise"),
                   OK("ignored-frame counter 0")]),
        dict(name="t3-f-no-turn", server=("mock", "stock", "127.0.0.1"), expect="FAIL",
             cmd=[t("t3-ui.py"), "--url", "{url}", "--turn-s", "6"], must=[F("heading readout increased (clockwise)")]),
        dict(name="t3-f-junk", server=("junk", ["--junk-every", "1"]), expect="FAIL",
             cmd=[t("t3-ui.py"), "--url", "{url}", "--turn-s", "6"],
             must=[F("ignored-frame counter 0"), F("only tlm on the wire")]),
        dict(name="t3-f-no-server", server=None, expect="FAIL", cmd=[t("t3-ui.py"), "--url", "{url}", "--turn-s", "3"],
             must=[F("Device link connects")]),
        # ---- t4: read-only client ----
        dict(name="t4-pass-hall", server=("mock", "stock", "127.0.0.1"), expect="PASS",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "hall"]),
        dict(name="t4-pass-estop-fwlike", server=("mock", "fwestop", "127.0.0.1"), expect="PASS",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "estop", "--seconds", "20"]),
        dict(name="t4-pass-arm", server=("mock", "stock", "127.0.0.1"), expect="PASS",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "arm", "--confirm-arm", "esc-unplugged", "--seconds", "20"]),
        dict(name="t4-pass-raw", server=("mock", "stock", "127.0.0.1"), expect="PASS",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "hall", "--reader", "raw", "--seconds", "20"]),
        dict(name="t4-f-estop-stock-mock", server=("mock", "stock", "127.0.0.1"), expect="FAIL",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "estop", "--seconds", "20"],
             must=[F("events were generated"), F("positive control: the pinger received the events")]),
        dict(name="t4-f-leaky-ui", server=("mock", "leaky", "127.0.0.1"), expect="FAIL",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "hall", "--seconds", "20"],
             must=[F("reader received only tlm"), OK("UI ignored counter stayed 0")]),
        dict(name="t4-f-leaky-raw", server=("mock", "leaky", "127.0.0.1"), expect="FAIL",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "hall", "--reader", "raw", "--seconds", "20"],
             must=[F("reader received only tlm")]),
        dict(name="t4-f-arm-unconfirmed", server=("mock", "stock", "127.0.0.1"), expect="EXIT2",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--events", "arm"], server_must_not=["connected from"]),
        dict(name="t4-f-no-server", server=None, expect="FAIL",
             cmd=[t("t4-readonly.py"), "--url", "{url}", "--reader", "raw", "--seconds", "5"], must=[F("clients connect")]),
        # ---- t5: T-11 ----
        dict(name="t5-pass", long=True, server=("mock", "stock", "127.0.0.1"), expect="PASS",
             cmd=[t("t5-t11.py"), "--url", "{url}"], timeout=900),
        dict(name="t5-pass-bursty-sender",
             server=("junk", ["--junk-every", "0", "--stall-every", "3", "--stall-ms", "80", "--pong-delay-ms", "2"]),
             expect="PASS", cmd=[t("t5-t11.py"), "--url", "{url}", "--seconds", "30"],
             must=[OK("tlm loss under 1 %"), r"loss by capture-box's gap method \(information\): \d+ = [1-9]"]),
        dict(name="t5-f-box-closed-early", server=("mock", "stock", "127.0.0.1"), kill_after=20, expect="FAIL",
             cmd=[t("t5-t11.py"), "--url", "{url}", "--seconds", "60"], must=[F("capture lasted the whole window")]),
        dict(name="t5-f-rate-no-pongs", server=("junk", ["--junk-every", "0", "--tlm-hz", "40"]), expect="FAIL",
             cmd=[t("t5-t11.py"), "--url", "{url}", "--seconds", "30"],
             must=[F("tlm at 50 Hz"), F("round-trip p95 under 100 ms")]),
        dict(name="t5-f-loss", server=("junk", ["--junk-every", "0", "--drop-every", "50", "--pong-delay-ms", "5"]),
             expect="FAIL", cmd=[t("t5-t11.py"), "--url", "{url}", "--seconds", "30"],
             must=[F("tlm loss under 1 %"), OK("round-trip p95 under 100 ms")]),
        dict(name="t5-f-latency", server=("junk", ["--junk-every", "0", "--pong-delay-ms", "150"]), expect="FAIL",
             cmd=[t("t5-t11.py"), "--url", "{url}", "--seconds", "30"],
             must=[F("round-trip p95 under 100 ms"), OK("tlm at 50 Hz"), OK("tlm loss under 1 %")]),
        # ---- t6: controlling client and heartbeat ----
        dict(name="t6-pass", server=("mock", "stock", "127.0.0.1"), expect="PASS",
             cmd=[t("t6-control.py"), "--url", "{url}", "--confirm-arm", "esc-unplugged", "--throttle-guard", "2047"]),
        dict(name="t6-f-unconfirmed", server=("mock", "stock", "127.0.0.1"), expect="EXIT2",
             cmd=[t("t6-control.py"), "--url", "{url}"], server_must_not=["connected from"]),
        dict(name="t6-f-throttle-guard", server=("mock", "stock", "127.0.0.1"), expect="FAIL",
             cmd=[t("t6-control.py"), "--url", "{url}", "--confirm-arm", "esc-unplugged"],
             must=[r"THROTTLE GUARD", F("sequence completed")], server_must=["estop from client"]),
        dict(name="t6-f-slow-heartbeat", server=("mock", "slowhb", "127.0.0.1"), expect="FAIL",
             cmd=[t("t6-control.py"), "--url", "{url}", "--confirm-arm", "esc-unplugged", "--throttle-guard", "2047"],
             must=[F("ramp-down starts within 1.5 s of closing A")]),
        dict(name="t6-f-no-controller-rule", server=("mock", "nocontroller", "127.0.0.1"), expect="FAIL",
             cmd=[t("t6-control.py"), "--url", "{url}", "--confirm-arm", "esc-unplugged", "--throttle-guard", "2047"],
             must=[F("B's mode refused 'not controlling client'")]),
        dict(name="t6-f-battery-low", server=("mock", "battery", "127.0.0.1"), expect="BLOCKED",
             cmd=[t("t6-control.py"), "--url", "{url}", "--confirm-arm", "esc-unplugged", "--throttle-guard", "2047"],
             must=[r"BLOCKED: arm precondition not met \(battery low\)"]),
        dict(name="t6-f-no-server", server=None, expect="FAIL",
             cmd=[t("t6-control.py"), "--url", "{url}", "--confirm-arm", "esc-unplugged"], must=[F("clients connect")]),
        # ---- t7: R3.4 timing with Wi-Fi ----
        dict(name="t7-pass", long=True, server=("mock", "stock", "127.0.0.1"), serial=["--seconds", "640"], expect="PASS",
             cmd=[t("t7-timing.py"), "--url", "{url}", "--serial-log", "{serial}"], timeout=900,
             must=[r"RESULT t5-t11: PASS"]),
        dict(name="t7-f-spike", server=("mock", "stock", "127.0.0.1"), serial=["--seconds", "45", "--spike", "12:1200"],
             expect="FAIL", cmd=[t("t7-timing.py"), "--url", "{url}", "--serial-log", "{serial}", "--seconds", "30"],
             must=[F("worst loop_us_max under 1000 us")]),
        dict(name="t7-f-log-not-growing", server=("mock", "stock", "127.0.0.1"), serial_static=True, expect="FAIL",
             cmd=[t("t7-timing.py"), "--url", "{url}", "--serial-log", "{serial}", "--seconds", "30"],
             must=[F("serial log is growing")]),
        dict(name="t7-f-baro-silent", server=("mock", "stock", "127.0.0.1"), serial=["--seconds", "45", "--baro-off-from", "8"],
             expect="FAIL", cmd=[t("t7-timing.py"), "--url", "{url}", "--serial-log", "{serial}", "--seconds", "30"],
             must=[F("every sensor read: barometer answered every second")]),
        dict(name="t7-f-reboot", server=("mock", "stock", "127.0.0.1"), serial=["--seconds", "45", "--reboot-at", "15"],
             expect="FAIL", cmd=[t("t7-timing.py"), "--url", "{url}", "--serial-log", "{serial}", "--seconds", "30"],
             must=[F("no reboot in the window")]),
        dict(name="t7-f-wifi-drop", server=("mock", "stock", "127.0.0.1"), serial=["--seconds", "45", "--wifi-drop-at", "12"],
             expect="FAIL", cmd=[t("t7-timing.py"), "--url", "{url}", "--serial-log", "{serial}", "--seconds", "30"],
             must=[F("Wi-Fi stayed connected")]),
    ] + ([
        dict(name="t7-pass-log-only-real-r34", server=None, expect="PASS",
             cmd=[t("t7-timing.py"), "--serial-log", flight, "--log-only"], must=[r"worst 976 us at s 1"]),
    ] if flight and os.path.exists(flight) else []) + ([
        dict(name="t7-f-log-only-real-diag", server=None, expect="FAIL",
             cmd=[t("t7-timing.py"), "--serial-log", diag, "--log-only"],
             must=[F("worst loop_us_max under 1000 us"), F("flight build")]),
    ] if diag and os.path.exists(diag) else [])


def run_case(case, suite):
    name = case["name"]
    host = case["server"][2] if case.get("server") and case["server"][0] == "mock" else case.get("host", "127.0.0.1")
    port = free_port(host)
    url = case.get("url", "ws://{host}:{port}/ws").replace("{host}", host).replace("{port}", str(port))
    server = serial = None
    serial_path = suite / f"{name}-serial.log"
    note = ""
    try:
        if case.get("server"):
            kind = case["server"][0]
            if kind == "mock":
                variant = case["server"][1]
                script = REPO / "tools" / "mock-box.py" if variant == "stock" else suite / f"mock-{variant}.py"
                server = Proc([PY, script, "--host", host, "--port", port], suite / f"{name}-server.txt")
            else:
                server = Proc([PY, KIT / "test" / "junk-box.py", "--host", host, "--port", port, *case["server"][1]],
                              suite / f"{name}-server.txt")
            if not wait_port(host, port):
                note = "server did not start"
        if case.get("serial") or case.get("serial_static"):
            subprocess.run([PY, KIT / "test" / "fake-serial.py", "--out", serial_path, "--boot"], check=True)
            if case.get("serial"):
                serial = Proc([PY, KIT / "test" / "fake-serial.py", "--out", serial_path, *case["serial"]],
                              suite / f"{name}-serial-writer.txt")
                time.sleep(1.5)
        if case.get("kill_after"):
            threading.Timer(case["kill_after"], server.stop).start()
        cmd = [str(c).replace("{url}", url).replace("{serial}", str(serial_path)) for c in case["cmd"]]
        out_path = suite / f"{name}.txt"
        started = time.monotonic()
        with open(out_path, "w") as out:
            out.write("$ " + " ".join(cmd) + "\n")
            out.flush()
            try:
                code = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, timeout=case.get("timeout", 400),
                                      env={**os.environ, "R35_OUT": str(suite / "runs")}).returncode
            except subprocess.TimeoutExpired:
                code = 124
        elapsed = time.monotonic() - started
    finally:
        for proc in (serial, server):
            if proc:
                proc.stop()
    text = out_path.read_text(errors="replace")
    verdict = re.search(rf"^RESULT {re.escape(name.split('-')[0])}-\S+: (PASS|FAIL|BLOCKED)", text, re.M)
    got = verdict.group(1) if verdict else f"no RESULT (exit {code})"
    if case["expect"] == "EXIT2":
        got = "EXIT2" if code == 2 and not verdict else got
    problems = []
    if got != case["expect"]:
        problems.append(f"expected {case['expect']}, got {got}")
    shown = []
    for pattern in case.get("must", []):
        hit = re.search(pattern + r".*", text)
        if hit:
            shown.append(hit.group(0).strip()[:230])
        else:
            problems.append(f"missing /{pattern}/")
    server_log = (suite / f"{name}-server.txt").read_text(errors="replace") if case.get("server") else ""
    for pattern in case.get("server_must", []):
        if not re.search(pattern, server_log):
            problems.append(f"server log lacks /{pattern}/")
        else:
            shown.append(f"server log: {re.search(pattern + r'.*', server_log).group(0)[:150]}")
    for pattern in case.get("server_must_not", []):
        hits = re.findall(pattern, server_log)
        if hits:
            problems.append(f"server log has /{pattern}/ x{len(hits)}")
        else:
            shown.append(f"server log: 0 x /{pattern}/ (nothing connected)")
    if note:
        problems.append(note)
    return {"name": name, "expect": case["expect"], "got": got, "ok": not problems, "problems": problems,
            "shown": shown, "elapsed": elapsed, "exit": code}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--only", help="run cases whose name starts with this")
    parser.add_argument("--skip-long", action="store_true", help="skip the two 10-minute positive runs")
    args = parser.parse_args()
    suite = KIT / "results" / f"mock-{datetime.datetime.now().strftime('%Y%m%d-%H%M%S')}"
    suite.mkdir(parents=True)
    make_variants(suite)
    br = bridge_address()
    if not br:
        sys.exit(f"mock-suite: {SELF}.local does not resolve; the t1 cases need it")
    fs = [PY, KIT / "test" / "fake-serial.py"]
    subprocess.run([*fs, "--out", suite / "boot-ok.log", "--boot", "--host", SELF, "--ip", br], check=True)
    subprocess.run([*fs, "--out", suite / "boot-wrong-ip.log", "--boot", "--host", SELF, "--ip", "192.168.1.99"], check=True)
    subprocess.run([*fs, "--out", suite / "boot-nosuch.log", "--boot", "--host", "nosuchbox-r35kit", "--ip", "192.168.1.98"],
                   check=True)
    # What the box prints with no credentials (direcsun_comms/src/wifi.cpp), and nothing after it.
    (suite / "boot-no-wifi.log").write_text(
        "ESP-ROM:esp32s3-20210327\r\nrst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\r\n"
        "\x1b[0;31mE (1612) wifi: no Wi-Fi network configured: set CONFIG_DIRECSUN_WIFI_SSID and "
        "CONFIG_DIRECSUN_WIFI_PASSWORD in firmware/sdkconfig.defaults.local, delete firmware/sdkconfig, and rebuild. "
        "The box stays offline and safe.\x1b[0m\r\n"
        "\x1b[0;33mW (1613) comms: network not started: no ground link this boot (the box stays in SAFE)\x1b[0m\r\n")
    selected = [c for c in cases(suite, br) if (not args.only or c["name"].startswith(args.only))
                and not (args.skip_long and c.get("long"))]
    print(f"mock-suite: {len(selected)} cases, output in {suite}", flush=True)
    results = {}
    threads = []
    for case in [c for c in selected if c.get("long")]:
        def work(c=case):
            results[c["name"]] = run_case(c, suite)
            print(f"  done (long) {c['name']}: {results[c['name']]['got']}", flush=True)
        thread = threading.Thread(target=work)
        thread.start()
        threads.append(thread)
    for case in [c for c in selected if not c.get("long")]:
        results[case["name"]] = run_case(case, suite)
        r = results[case["name"]]
        print(f"  {'ok ' if r['ok'] else 'BAD'} {r['name']}: expected {r['expect']}, got {r['got']} ({r['elapsed']:.0f} s)",
              flush=True)
    for thread in threads:
        thread.join()
    lines = [f"mock suite {suite.name}: {sum(r['ok'] for r in results.values())}/{len(results)} cases behaved as expected", ""]
    for case in selected:
        r = results[case["name"]]
        lines.append(f"[{'ok ' if r['ok'] else 'BAD'}] {r['name']}: expected {r['expect']}, got {r['got']}, "
                     f"{r['elapsed']:.0f} s, exit {r['exit']}")
        lines.extend(f"      {s}" for s in r["shown"])
        lines.extend(f"      PROBLEM: {p}" for p in r["problems"])
    (suite / "SUMMARY.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines), flush=True)
    return 0 if all(r["ok"] for r in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
