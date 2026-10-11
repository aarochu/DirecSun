"""Shared helpers for the R3.5 hardware-test kit (see RUNBOOK.md).

Every t*.py script imports this module first. It:

- re-runs the script under $DIRECSUN_PYTHON when this interpreter lacks websockets;
- finds the two checkouts the kit drives: $R35_REPO (feat/set3/R3.5-comms: tools/,
  protocol/, ground/) and $R35_UI (frontend/ground-station from origin/main, after npm ci);
- gives each run its own output directory and copies everything printed into
  <out>/output.txt, so the full record survives whatever the terminal keeps;
- provides a recording WebSocket client that time-stamps every frame on arrival.
"""
import asyncio
import contextlib
import datetime
import json
import os
import pathlib
import re
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request

KIT = pathlib.Path(__file__).resolve().parent


def _reexec_into_venv():
    if os.environ.get("R35_REEXEC"):
        sys.exit("r35kit: $DIRECSUN_PYTHON does not have websockets and jsonschema either.")
    candidate = os.environ.get("DIRECSUN_PYTHON")
    if candidate and os.path.exists(candidate) and os.path.abspath(candidate) != os.path.abspath(sys.executable):
        os.environ["R35_REEXEC"] = "1"
        os.execv(candidate, [candidate, *sys.argv])
    sys.exit("r35kit: needs websockets and jsonschema. Set DIRECSUN_PYTHON to the repo's .venv/bin/python "
             "(source env.sh).")


try:
    import jsonschema  # noqa: F401  (check-schema needs it; fail here, not halfway through a run)
    from websockets.asyncio.client import connect
    from websockets.exceptions import ConnectionClosed
except ImportError:
    _reexec_into_venv()


# ---- configuration ----------------------------------------------------------------

def repo_dir():
    path = pathlib.Path(os.environ.get("R35_REPO", "")).expanduser()
    if not (path / "tools" / "capture-box.py").is_file() or not (path / "protocol" / "schema-v1.json").is_file():
        sys.exit(f"r35kit: R35_REPO={str(path)!r} is not a checkout of feat/set3/R3.5-comms "
                 "(no tools/capture-box.py or protocol/schema-v1.json). Source env.sh.")
    return path


def ui_dir():
    path = pathlib.Path(os.environ.get("R35_UI", "")).expanduser()
    if not (path / "package.json").is_file() or not (path / "node_modules" / "@playwright" / "test").is_dir():
        sys.exit(f"r35kit: R35_UI={str(path)!r} is not frontend/ground-station with node_modules installed. "
                 "Source env.sh and run prep.sh.")
    if not (path / "dist" / "index.html").is_file():
        sys.exit(f"r35kit: {path}/dist is missing: run prep.sh (it builds the UI once).")
    return path


def python():
    return os.environ.get("DIRECSUN_PYTHON") or sys.executable


# ---- run bookkeeping ----------------------------------------------------------------

class _Tee:
    def __init__(self, stream, path):
        self.stream = stream
        self.file = open(path, "a", buffering=1)

    def write(self, text):
        self.stream.write(text)
        self.file.write(text)
        return len(text)

    def flush(self):
        self.stream.flush()
        self.file.flush()


class Run:
    """One script run: an output directory, a tee of stdout and stderr, and the verdict."""

    def __init__(self, item, out_root=None, parent=None):
        self.item = item
        self.checks = []
        self.started = time.time()
        if parent is not None:
            # A second verdict inside the same run directory (t7 judges T-11 on its own stream).
            self.dir = parent.dir
            self.result_name = f"result-{item}.json"
            return
        self.result_name = "result.json"
        root = pathlib.Path(out_root or os.environ.get("R35_OUT") or "r35-results").expanduser()
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.dir = (root / f"{item}-{stamp}").resolve()
        self.dir.mkdir(parents=True, exist_ok=True)
        log = self.dir / "output.txt"
        sys.stdout = _Tee(sys.stdout, log)
        sys.stderr = _Tee(sys.stderr, log)
        self.say(f"{item}: writing everything to {self.dir}")

    def say(self, text):
        print(f"[{time.strftime('%H:%M:%S')}] {text}", flush=True)

    def check(self, name, ok, detail):
        """Record one criterion. ok is True, False, or None (information only, not judged)."""
        mark = {True: "ok  ", False: "FAIL", None: "info"}[ok]
        self.checks.append({"name": name, "ok": ok, "detail": detail})
        self.say(f"  [{mark}] {name}: {detail}")
        return ok

    def finish(self, verdict=None, summary=""):
        """Print the RESULT line and write result.json. verdict defaults to PASS iff no check failed."""
        if verdict is None:
            verdict = "FAIL" if any(c["ok"] is False for c in self.checks) else "PASS"
        failed = [c["name"] for c in self.checks if c["ok"] is False]
        if failed and not summary:
            summary = "failed: " + "; ".join(failed)
        (self.dir / self.result_name).write_text(json.dumps(
            {"item": self.item, "verdict": verdict, "summary": summary, "checks": self.checks,
             "started": self.started, "finished": time.time()}, indent=2) + "\n")
        print(f"RESULT {self.item}: {verdict}{' (' + summary + ')' if summary else ''}", flush=True)
        print(f"RESULT {self.item}: details in {self.dir}", flush=True)
        return {"PASS": 0, "FAIL": 1}.get(verdict, 2)


# ---- URLs, ports, child processes ----------------------------------------------------------

def parse_ws_url(url):
    parts = urllib.parse.urlparse(url)
    if parts.scheme not in ("ws", "wss") or not parts.hostname:
        sys.exit(f"r35kit: {url!r} is not a ws:// URL")
    return parts.hostname, parts.port or (443 if parts.scheme == "wss" else 80), parts.path or "/"


def free_port(host="127.0.0.1"):
    with socket.socket() as s:
        s.bind((host, 0))
        return s.getsockname()[1]


def run_logged(cmd, log_path, timeout=None, cwd=None):
    """Run a command with its full output in log_path. Returns (exit code, output text)."""
    with open(log_path, "w") as log:
        log.write("$ " + " ".join(str(c) for c in cmd) + "\n")
        log.flush()
        try:
            code = subprocess.run([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT,
                                  timeout=timeout, cwd=cwd).returncode
        except subprocess.TimeoutExpired:
            code = 124
            log.write(f"\n(timed out after {timeout} s)\n")
    return code, pathlib.Path(log_path).read_text(errors="replace")


@contextlib.contextmanager
def background(cmd, log_path, ready_url=None, cwd=None, timeout=40):
    """Start a server process, wait until ready_url answers HTTP 200, and always stop it."""
    log = open(log_path, "w")
    proc = subprocess.Popen([str(c) for c in cmd], stdout=log, stderr=subprocess.STDOUT, cwd=cwd,
                            start_new_session=True)
    try:
        if ready_url:
            deadline = time.monotonic() + timeout
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(f"{cmd[0]} exited with {proc.returncode}; see {log_path}")
                try:
                    with urllib.request.urlopen(ready_url, timeout=2) as response:
                        if response.status == 200:
                            break
                except OSError:
                    pass
                if time.monotonic() > deadline:
                    raise RuntimeError(f"{ready_url} did not answer within {timeout} s; see {log_path}")
                time.sleep(0.3)
        yield proc
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
        log.close()


@contextlib.contextmanager
def ui_server(run):
    """Serve the built main UI (frontend/ground-station/dist) with vite preview on a free port."""
    ui = ui_dir()
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with background(["npx", "vite", "preview", "--host", "127.0.0.1", "--port", port, "--strictPort"],
                    run.dir / "ui-server.txt", ready_url=base + "/", cwd=ui):
        yield base


@contextlib.contextmanager
def console_server(run):
    """Serve the repo root over HTTP so ground/console can import ../link as an ES module."""
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    with background([sys.executable, "-m", "http.server", port, "--bind", "127.0.0.1", "--directory", repo_dir()],
                    run.dir / "console-server.txt", ready_url=base + "/ground/console/index.html"):
        yield base


def probe(run, mode, url, base, extra=(), on_line=None, timeout=300):
    """Run ui-probe.mjs (Playwright in the installed Chrome). Returns its JSON result (or a failure dict)."""
    out = run.dir / f"probe-{mode}"
    out.mkdir(exist_ok=True)
    cmd = ["node", KIT / "ui-probe.mjs", "--mode", mode, "--url", url, "--base", base, "--ui-dir", ui_dir(),
           "--out", out, *extra]
    log_path = run.dir / f"probe-{mode}.txt"
    with open(log_path, "w") as log:
        proc = subprocess.Popen([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        watchdog = threading.Timer(timeout, proc.kill)
        watchdog.start()
        try:
            for line in proc.stdout:
                log.write(line)
                log.flush()
                run.say(f"    probe: {line.rstrip()}")
                if on_line:
                    on_line(line)
        finally:
            watchdog.cancel()
        proc.wait()
    result_path = out / "probe.json"
    if not result_path.exists():
        return {"ok": False, "error": f"the probe wrote no result (exit {proc.returncode}); see {log_path}"}
    result = json.loads(result_path.read_text())
    result["exit"] = proc.returncode
    return result


# ---- the recording client -----------------------------------------------------------------

class Client:
    """A WebSocket client that records every frame with its host arrival time (time.monotonic()).

    Like the main UI's browser socket it sends no protocol-level ping frames
    (ping_interval=None): every byte it sends is a DirecSun message the script chose.
    """

    def __init__(self, name, url, run=None):
        self.name = name
        self.url = url
        self.run = run
        self.ws = None
        self.frames = []          # (arrival monotonic, text, parsed message or None)
        self.sent = []            # (send monotonic, text)
        self.closed_at = None
        self.close_reason = None
        self._seq = 0
        self._arrived = asyncio.Event()
        self._reader = None
        self._pinger = None
        self.on_message = None    # optional callback(message)

    async def open(self, timeout=10):
        self.ws = await connect(self.url, open_timeout=timeout, ping_interval=None, close_timeout=2,
                                max_size=1 << 16)
        self._reader = asyncio.ensure_future(self._read())
        return self

    async def _read(self):
        try:
            async for text in self.ws:
                if isinstance(text, bytes):
                    text = text.decode(errors="replace")
                try:
                    message = json.loads(text)
                except json.JSONDecodeError:
                    message = None
                self.frames.append((time.monotonic(), text, message))
                if self.on_message and message is not None:
                    self.on_message(message)
                self._arrived.set()
        except ConnectionClosed as exc:
            self.close_reason = f"{exc}"
        finally:
            self.closed_at = time.monotonic()
            self._arrived.set()

    def next_seq(self):
        self._seq += 1
        return self._seq

    async def send(self, message):
        text = message if isinstance(message, str) else json.dumps(message, separators=(",", ":"))
        self.sent.append((time.monotonic(), text))
        await self.ws.send(text)

    async def ping(self):
        seq = self.next_seq()
        await self.send({"v": 1, "type": "ping", "seq": seq, "t_client_ms": round(time.monotonic() * 1000.0, 3)})
        return seq

    def start_pings(self, hz=5.0):
        async def loop():
            while True:
                try:
                    await self.ping()
                except ConnectionClosed:
                    return
                await asyncio.sleep(1.0 / hz)
        self._pinger = asyncio.ensure_future(loop())

    def stop_pings(self):
        if self._pinger:
            self._pinger.cancel()
            self._pinger = None

    async def cmd(self, name, timeout=2.0, **fields):
        """Send a cmd and wait for the ack with its seq. Returns (ack message or None, send time)."""
        seq = self.next_seq()
        start_index = len(self.frames)
        sent_at = time.monotonic()
        await self.send({"v": 1, "type": "cmd", "seq": seq, "cmd": name, **fields})
        hit = await self.wait_for(lambda m: m.get("type") == "ack" and m.get("seq") == seq, timeout, start_index)
        return (hit[1] if hit else None), sent_at

    async def wait_for(self, predicate, timeout, since=0):
        """First (arrival, message) at index >= since matching predicate, or None after timeout."""
        deadline = time.monotonic() + timeout
        index = since
        while True:
            while index < len(self.frames):
                arrival, _text, message = self.frames[index]
                index += 1
                if message is not None and predicate(message):
                    return arrival, message
            remaining = deadline - time.monotonic()
            if remaining <= 0 or self.closed_at is not None:
                return None
            self._arrived.clear()
            try:
                await asyncio.wait_for(self._arrived.wait(), remaining)
            except asyncio.TimeoutError:
                return None

    def messages(self, since_time=None, until_time=None):
        return [m for t, _x, m in self.frames if m is not None
                and (since_time is None or t >= since_time) and (until_time is None or t <= until_time)]

    def types(self):
        counts = {}
        for _t, _x, m in self.frames:
            key = "(not JSON)" if m is None else m.get("type", "?") + (
                f":{m.get('event')}" if m.get("type") == "event" else "")
            counts[key] = counts.get(key, 0) + 1
        return dict(sorted(counts.items()))

    def last_tlm(self):
        for _t, _x, m in reversed(self.frames):
            if m is not None and m.get("type") == "tlm":
                return m
        return None

    def write_jsonl(self, path):
        pathlib.Path(path).write_text("".join(text + "\n" for _t, text, _m in self.frames))
        return path

    async def close(self):
        self.stop_pings()
        if self.ws is not None:
            with contextlib.suppress(Exception):
                await self.ws.close()
        if self._reader:
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self._reader, 3)


def check_schema(run, paths, label):
    """Run tools/check-schema.py on JSON-lines files. Returns (ok, {path: summary line})."""
    paths = [p for p in paths if pathlib.Path(p).exists() and pathlib.Path(p).stat().st_size > 0]
    if not paths:
        return False, {"(none)": "no frames recorded"}
    code, text = run_logged([python(), repo_dir() / "tools" / "check-schema.py", *paths],
                            run.dir / f"check-schema-{label}.txt", timeout=300)
    summaries = {}
    for line in text.splitlines():
        match = re.match(r"check-schema: (.+?): (\d+)/(\d+) messages valid \((.*)\)", line)
        if match:
            summaries[match.group(1)] = f"{match.group(2)}/{match.group(3)} valid ({match.group(4)})"
    failures = [line for line in text.splitlines() if "check-schema: FAIL" in line]
    for line in failures[:5]:
        run.say(f"    {line[:300]}")
    return code == 0 and not failures and len(summaries) == len(paths), summaries


def strip_ansi(text):
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text).replace("\r", "")


# ---- T-11: capture-box plus an independent recount from the capture file ------------------

T11_RATE_HZ = (49.0, 51.0)
T11_MAX_LOSS = 0.01
T11_MAX_P95_MS = 100.0


def recount_tlm(path, period_ms=20.0):
    """tlm numbers from a capture file, computed here, three ways.

    - count: frames received (distinct box seq) against the frames the box-clock span
      implies at 50 Hz. A frame is lost only if it never arrived.
    - gaps: capture-box's method, round(gap / 20 ms) - 1 per gap. It counts a late
      frame's gap as loss and never credits the catch-up frames after it, so a sender
      whose t_ms jitters shows loss that did not happen (the mock under CPU load does).
    - box seq: tlm, pong, and event share the box's seq counter, so with a single
      client every missing seq is a frame the box numbered and this client never got.
    """
    times, tlm_seqs, box_seqs = [], set(), set()
    duplicates = 0
    for line in pathlib.Path(path).read_text().splitlines():
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        seq = message.get("seq")
        if message.get("type") in ("tlm", "pong", "event") and isinstance(seq, int):
            box_seqs.add(seq)
        if message.get("type") == "tlm" and isinstance(message.get("t_ms"), int):
            if seq in tlm_seqs:
                duplicates += 1
                continue
            tlm_seqs.add(seq)
            times.append(message["t_ms"])
    gaps = [b - a for a, b in zip(times, times[1:])]
    gap_lost = sum(max(0, round(g / period_ms) - 1) for g in gaps if g >= 0)
    span_ms = times[-1] - times[0] if len(times) > 1 else 0
    expected = round(span_ms / period_ms) + 1 if times else 0
    lost = max(0, expected - len(times))
    seq_missing = (max(box_seqs) - min(box_seqs) + 1 - len(box_seqs)) if box_seqs else 0
    return {"tlm": len(times), "span_s": span_ms / 1000.0,
            "rate_hz": (len(times) - 1) / (span_ms / 1000.0) if span_ms > 0 else 0.0,
            "expected": expected, "lost": lost, "loss": lost / expected if expected else 1.0,
            "gap_lost": gap_lost, "gap_loss": gap_lost / (len(times) + gap_lost) if times else 1.0,
            "seq_missing": seq_missing, "duplicates": duplicates,
            "max_gap_ms": max(gaps) if gaps else None, "backwards": sum(1 for g in gaps if g < 0)}


def parse_capture_box(text):
    """The numbers capture-box prints (see tools/capture-box.py)."""
    out = {}
    m = re.search(r"capture-box: tlm (\d+) frames, ([\d.]+) Hz over the box's t_ms, (\d+) lost \(([\d.]+) %\), "
                  r"largest gap (\d+) ms", text)
    if m:
        out.update(tlm=int(m.group(1)), rate_hz=float(m.group(2)), lost=int(m.group(3)),
                   loss_pct=float(m.group(4)), max_gap_ms=int(m.group(5)))
    m = re.search(r"round trip from (\d+) pongs: p50 ([\d.]+) ms, p95 ([\d.]+) ms, max ([\d.]+) ms", text)
    if m:
        out.update(pongs=int(m.group(1)), p50_ms=float(m.group(2)), p95_ms=float(m.group(3)), rtt_max_ms=float(m.group(4)))
    out["closed_early"] = "the box closed the connection" in text
    out["failed_lines"] = [l for l in text.splitlines() if "FAILED" in l]
    return out


def t11_capture_cmd(url, seconds, out_path):
    return [python(), repo_dir() / "tools" / "capture-box.py", "--url", url, "--seconds", str(seconds),
            "--t11", "--out", out_path]


def judge_t11(run, code, text, capture_path, seconds):
    """Record the T-11 checks for one capture-box --t11 run.

    Rate and loss are judged on this module's recount of the capture file, which
    counts distinct seq numbers over the box clock's span; round trip on capture-box's
    numbers (the file has no arrival times). capture-box counts distinct t_ms values
    instead (3799644), right for the box, which stamps each 20 ms snapshot, but a
    sender that stamps t_ms when it sends (test/junk-box.py after a stall) makes
    frames share a t_ms. So the cross-check compares what both define the same way,
    the frame count and the rate, and capture-box's loss is reported, not judged.
    """
    tool = parse_capture_box(text)
    mine = recount_tlm(capture_path) if pathlib.Path(capture_path).exists() else recount_tlm_empty()
    run.check("capture-box --t11 exit status (information)", None,
              f"exit {code}" + (f": {'; '.join(tool['failed_lines'])}" if tool["failed_lines"] else ""))
    run.check("capture lasted the whole window", mine["span_s"] >= 0.98 * seconds and not tool["closed_early"],
              f"box clock span {mine['span_s']:.1f} s of {seconds:.0f} s"
              + ("; the box closed the connection" if tool["closed_early"] else ""))
    lo, hi = T11_RATE_HZ
    run.check("tlm at 50 Hz", lo <= mine["rate_hz"] <= hi,
              f"{mine['rate_hz']:.2f} Hz from {mine['tlm']} tlm (capture-box: {tool.get('rate_hz')} Hz)")
    run.check("tlm loss under 1 %", mine["loss"] < T11_MAX_LOSS,
              f"{mine['lost']} of {mine['expected']} frames never arrived = {mine['loss'] * 100:.3f} % "
              f"(count over the span; {mine['duplicates']} duplicate(s) ignored)")
    run.check("loss by the gap method (information)", None,
              f"{mine['gap_lost']} = {mine['gap_loss'] * 100:.3f} %, largest gap {mine['max_gap_ms']} ms"
              + (": the gap method counts late frames as lost; the count above says they arrived"
                 if mine["gap_lost"] > mine["lost"] + 0.002 * mine["expected"] else ""))
    run.check("box seq numbers missing (information; exact only with one client)", None,
              f"{mine['seq_missing']} missing across tlm, pong, and event")
    p95 = tool.get("p95_ms")
    run.check("round-trip p95 under 100 ms", p95 is not None and p95 < T11_MAX_P95_MS,
              f"p95 {p95} ms, p50 {tool.get('p50_ms')} ms, max {tool.get('rtt_max_ms')} ms from {tool.get('pongs')} pongs"
              " (capture-box's measurement; the capture file has no arrival times to recompute it)")
    agree = tool.get("tlm") == mine["tlm"] and abs((tool.get("rate_hz") or 0) - mine["rate_hz"]) < 0.01
    run.check("capture file agrees with capture-box's summary", agree,
              f"file: tlm {mine['tlm']}, rate {mine['rate_hz']:.2f}; capture-box: tlm {tool.get('tlm')}, "
              f"rate {tool.get('rate_hz')}")
    run.check("capture-box's loss, distinct t_ms over the span (information)", None,
              f"{tool.get('lost')} lost; the recount above, distinct seq, says {mine['lost']}")
    run.check("box clock never went backwards", mine["backwards"] == 0, f"{mine['backwards']} backward step(s)")
    return tool, mine


def recount_tlm_empty():
    return {"tlm": 0, "span_s": 0.0, "rate_hz": 0.0, "expected": 0, "lost": 0, "loss": 1.0, "gap_lost": 0,
            "gap_loss": 1.0, "seq_missing": 0, "duplicates": 0, "max_gap_ms": None, "backwards": 0}
