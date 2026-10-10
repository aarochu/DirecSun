#!/usr/bin/env python3
"""Protocol stub of the DirecSun box, for the bench console and the demo UI.

    tools/mock-box.py [--host 127.0.0.1] [--port 8081] [--inputs throttle,wheel,rate]
    tools/mock-box.py --selftest

Speaks wire schema v1 (protocol/schema-v1.json) the way the firmware will:
50 Hz tlm to every client, an ack for every cmd, an event on every mode change,
pong for every ping. It follows the mode machine of docs/firmware-design.md
section 13.1 (SAFE, arm to SPINUP, READY 2 s later, mode commands, MANUAL with
its 250 ms staleness rule), the controlling-client rule and the 1 s heartbeat of
section 14.4, and the e-stop. It is not a plant model: headings and speeds are
plausible shapes, not physics, and it is no evidence about the hardware.

Every outbound message is validated against the schema; a violation stops the
mock, because it would mean the mock teaches the UI the wrong protocol. Inbound
messages that fail the schema get ack ok:false with the reason.

Port 81 needs root on Linux, so the mock listens on 8081 by default; point the
console at ws://127.0.0.1:8081/.

Needs websockets and jsonschema. If this interpreter lacks them, the script
re-runs itself with $DIRECSUN_PYTHON or the repo's .venv (tools/bootstrap-venv.sh).
"""
import argparse
import asyncio
import json
import math
import os
import pathlib
import random
import sys
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
SCHEMA = REPO / "protocol" / "schema-v1.json"


def _reexec_with_repo_python():
    # A venv's python is usually a symlink to the system binary, so compare paths as
    # given, and use an environment marker so a broken venv cannot loop.
    if not os.environ.get("DIRECSUN_REEXEC"):
        for candidate in (os.environ.get("DIRECSUN_PYTHON"), str(REPO / ".venv" / "bin" / "python")):
            if candidate and os.path.exists(candidate) and os.path.abspath(candidate) != os.path.abspath(sys.executable):
                os.environ["DIRECSUN_REEXEC"] = "1"
                os.execv(candidate, [candidate, __file__, *sys.argv[1:]])
    sys.exit("mock-box: needs the websockets and jsonschema packages. Run tools/bootstrap-venv.sh "
             "or set DIRECSUN_PYTHON to a Python that has them.")


try:
    from jsonschema import Draft202012Validator
    from jsonschema.exceptions import best_match
    from websockets.asyncio.server import broadcast, serve
    from websockets.exceptions import ConnectionClosed
except ImportError:
    _reexec_with_repo_python()

TICK_S = 0.02            # 50 Hz telemetry
HEARTBEAT_S = 1.0        # FD section 14.4
SPINUP_S = 2.0           # mock shortcut: SPINUP becomes READY after this
SLEW_S = 2.0             # mock shortcut: a slew completes after this
RAMP_S = 5.0             # e-stop and heartbeat ramp-down (FD section 14.3)
MANUAL_STALE_S = 0.25    # FD section 13.5
MANUAL_MIN_SPACING_S = 0.05
MAX_CLIENTS = 3
MAX_INBOUND_BYTES = 512

OMEGA_LOW, OMEGA_BIAS, OMEGA_CAP = 800.0, 2400.0, 4000.0
THROTTLE_MIN, THROTTLE_MAX = 200, 600     # stand-in for the T-5 band
ACTIVE_MODES = {"DETUMBLE", "HOLD", "SLEW", "SUN_POINT", "MANUAL"}
SETPOINT_KEY = {"throttle": "throttle", "wheel": "wheel_rpm", "rate": "rate_dps"}


def log(text):
    print(f"[mock-box {time.time():.3f}] {text}", flush=True)


def wrap_deg(angle):
    wrapped = math.remainder(angle, 360.0)
    return 180.0 if wrapped <= -180.0 else wrapped


class Schema:
    def __init__(self):
        schema = json.loads(SCHEMA.read_text())
        Draft202012Validator.check_schema(schema)
        self.validator = Draft202012Validator(schema)

    def error(self, message):
        best = best_match(self.validator.iter_errors(message))
        if best is None:
            return None
        where = "/".join(str(p) for p in best.absolute_path) or "(root)"
        return f"{where}: {best.message}"[:110]


class Box:
    """Mode machine and telemetry of the stub. Connection objects are opaque keys."""

    def __init__(self, schema, inputs):
        self.schema = schema
        self.inputs = inputs
        self.t0 = time.monotonic()
        self.seq = 0
        self.mode = "SAFE"
        self.mode_since = self.t0
        self.manual_input = None
        self.controller = None
        self.last_heartbeat = self.t0
        self.latched = []
        self.psi_offset = 0.0
        self.psi = 0.0
        self.rate = 0.0
        self.psi_ref = None
        self.slew = None                  # (start psi, target psi, start time)
        self.wheel = 0.0
        self.wheel_ref = 0.0
        self.spinup_target = OMEGA_BIAS
        self.ramp = None                  # (start time, start wheel) while ramping down
        self.manual_value = None
        self.manual_last = -1.0
        self.params = {"bench_mode": False, "manual_throttle_min": THROTTLE_MIN, "manual_throttle_max": THROTTLE_MAX}
        self.outbox = []                  # (connection or None for broadcast, message)

    # ---- outbound ----

    def _next_seq(self):
        self.seq = (self.seq + 1) % 4294967296
        return self.seq

    def _emit(self, target, message):
        error = self.schema.error(message)
        if error:
            raise RuntimeError(f"mock-box produced an invalid {message.get('type')}: {error}: {message}")
        self.outbox.append((target, message))

    def _ack(self, conn, seq, ok, reason=None, **extra):
        message = {"v": 1, "type": "ack", "seq": seq, "ok": ok, **extra}
        if not ok:
            message["reason"] = reason
            log(f"refused seq {seq}: {reason}")
        self._emit(conn, message)

    def _t_ms(self):
        return int((time.monotonic() - self.t0) * 1000)

    def _set_mode(self, mode, cause):
        if mode == self.mode:
            return
        self.mode = mode
        self.mode_since = time.monotonic()
        if mode != "MANUAL":
            self.manual_input = None
            self.manual_value = None
        if mode not in ("HOLD", "SLEW", "SUN_POINT"):
            self.psi_ref = None
            self.slew = None
        log(f"mode {mode} (cause {cause})")
        self._emit(None, {"v": 1, "type": "event", "seq": self._next_seq(), "t_ms": self._t_ms(),
                          "event": "mode", "mode": mode, "cause": cause})

    def _to_safe(self, cause, latch=None):
        if latch and latch not in self.latched:
            self.latched.append(latch)
        if self.mode != "SAFE":
            self.ramp = (time.monotonic(), self.wheel)
        self.wheel_ref = 0.0
        self._set_mode("SAFE", cause)

    # ---- inbound ----

    def receive(self, conn, text):
        if len(text.encode()) > MAX_INBOUND_BYTES:
            self._ack(conn, 0, False, "too large")
            return
        try:
            message = json.loads(text)
        except json.JSONDecodeError:
            self._ack(conn, 0, False, "malformed JSON")
            return
        seq = message.get("seq") if isinstance(message, dict) and isinstance(message.get("seq"), int) \
            and 0 <= message.get("seq") <= 4294967295 else 0
        error = self.schema.error(message)
        if error:
            self._ack(conn, seq, False, f"invalid: {error}")
            return
        if message["type"] == "ping":
            if conn is self.controller:
                self.last_heartbeat = time.monotonic()
            self._emit(conn, {"v": 1, "type": "pong", "seq": self._next_seq(), "ping_seq": message["seq"],
                              "t_client_ms": message["t_client_ms"], "t_ms": self._t_ms()})
            return
        if message["type"] != "cmd":
            self._ack(conn, seq, False, f"unexpected type {message['type']}")
            return
        self._command(conn, message)

    def _command(self, conn, message):
        seq, cmd = message["seq"], message["cmd"]
        armed = self.mode != "SAFE"
        if cmd == "estop":
            log(f"estop from client {id(conn) % 10000}")
            self._ack(conn, seq, True)
            self._to_safe("ESTOP", latch="ESTOP")
            return
        if armed and conn is not self.controller:
            self._ack(conn, seq, False, "not controlling client")
            return
        if conn is self.controller:
            self.last_heartbeat = time.monotonic()

        if cmd == "arm":
            if self.mode != "SAFE":
                return self._ack(conn, seq, False, f"not allowed in {self.mode}")
            self.controller = conn
            self.last_heartbeat = time.monotonic()
            self.latched.clear()
            self.ramp = None
            self.spinup_target = OMEGA_BIAS
            self._ack(conn, seq, True)
            self._set_mode("SPINUP", "arm")
        elif cmd == "disarm":
            if self.mode == "SAFE":
                return self._ack(conn, seq, False, "already SAFE")
            self._ack(conn, seq, True)
            self._to_safe("disarm")
        elif cmd == "mode":
            self._mode_command(conn, seq, message)
        elif cmd == "manual":
            self._manual(conn, seq, message)
        elif cmd == "respin":
            if self.mode != "READY":
                return self._ack(conn, seq, False, f"not allowed in {self.mode}")
            target = message.get("wheel_rpm", OMEGA_BIAS)
            if not OMEGA_LOW + 200 <= target <= OMEGA_CAP - 200:
                return self._ack(conn, seq, False, f"wheel_rpm outside {OMEGA_LOW + 200:.0f}..{OMEGA_CAP - 200:.0f}")
            self.spinup_target = float(target)
            self._ack(conn, seq, True)
            self._set_mode("SPINUP", "respin")
        elif cmd == "zero_heading":
            if self.mode not in ("SAFE", "READY"):
                return self._ack(conn, seq, False, f"not allowed in {self.mode}")
            self.psi_offset -= self.psi
            self._ack(conn, seq, True)
        elif cmd == "set_params":
            if self.mode != "SAFE":
                return self._ack(conn, seq, False, "set_params only in SAFE")
            self.params.update(message["params"])
            self._ack(conn, seq, True, applied=message["params"])
        elif cmd == "wheel_step":
            if self.mode != "READY":
                return self._ack(conn, seq, False, f"not allowed in {self.mode}")
            if not self.params.get("bench_mode"):
                return self._ack(conn, seq, False, "bench_mode is off")
            self.wheel_ref = min(OMEGA_CAP, max(OMEGA_LOW, self.wheel_ref + message["delta_rpm"]))
            self._ack(conn, seq, True)

    def _mode_command(self, conn, seq, message):
        target = message["mode"]
        if target == "READY":
            if self.mode in ACTIVE_MODES:
                self._ack(conn, seq, True)
                self._set_mode("READY", "mode")
            elif self.mode == "READY":
                self._ack(conn, seq, True)
            else:
                self._ack(conn, seq, False, f"not allowed in {self.mode}")
            return
        if self.mode != "READY":
            return self._ack(conn, seq, False, f"not allowed in {self.mode}")
        if target == "SUN_POINT":
            return self._ack(conn, seq, False, "sun not valid")
        if target == "MANUAL":
            if message["input"] not in self.inputs:
                return self._ack(conn, seq, False, "input not available")
            self._ack(conn, seq, True)
            self._set_mode("MANUAL", "mode")
            self.manual_input = message["input"]
            self.manual_value = None
            self.manual_last = time.monotonic()
            return
        self._ack(conn, seq, True)
        self._set_mode(target, "mode")
        if target == "HOLD":
            self.psi_ref = wrap_deg(self.psi)
        elif target == "SLEW":
            self.psi_ref = wrap_deg(self.psi)
            self.slew = (self.psi, message["psi_ref_deg"], time.monotonic())

    def _manual(self, conn, seq, message):
        if self.mode != "MANUAL":
            return self._ack(conn, seq, False, f"not allowed in {self.mode}")
        key = SETPOINT_KEY[self.manual_input]
        if key not in message:
            return self._ack(conn, seq, False, f"input is {self.manual_input}")
        now = time.monotonic()
        if now - self.manual_last < MANUAL_MIN_SPACING_S and self.manual_value is not None:
            return self._ack(conn, seq, False, "too fast")
        value = float(message[key])
        if key == "throttle":
            value = min(self.params["manual_throttle_max"], max(self.params["manual_throttle_min"], value))
        elif key == "wheel_rpm":
            value = min(OMEGA_CAP, max(OMEGA_LOW, value))
        else:
            value = min(90.0, max(-90.0, value))
        self.manual_value = value
        self.manual_last = now
        self._ack(conn, seq, True)

    def disconnected(self, conn):
        # The controlling client keeps its role until the heartbeat expires (FD 14.4).
        log(f"client {id(conn) % 10000} disconnected")

    # ---- time ----

    def tick(self):
        now = time.monotonic()
        dt = TICK_S
        if self.mode != "SAFE" and now - self.last_heartbeat > HEARTBEAT_S:
            self._to_safe("HEARTBEAT_LOST", latch="HEARTBEAT_LOST")
        if self.mode == "SPINUP" and now - self.mode_since >= SPINUP_S:
            self._set_mode("READY", "spinup_complete")
        if self.mode == "SLEW" and self.slew and now - self.slew[2] >= SLEW_S:
            self._set_mode("READY", "slew_complete")

        stale = self.mode == "MANUAL" and now - self.manual_last > MANUAL_STALE_S
        # Wheel reference.
        if self.mode == "SAFE":
            if self.ramp:
                start, wheel0 = self.ramp
                fraction = min(1.0, (now - start) / RAMP_S)
                self.wheel_ref = wheel0 * (1.0 - fraction)
                if fraction >= 1.0:
                    self.ramp = None
            else:
                self.wheel_ref = 0.0
        elif self.mode == "SPINUP":
            fraction = min(1.0, (now - self.mode_since) / SPINUP_S)
            self.wheel_ref = self.spinup_target * fraction
        elif self.mode == "MANUAL" and self.manual_input == "throttle" and self.manual_value is not None:
            self.wheel_ref = min(OMEGA_CAP, (self.manual_value - 48) / 0.17)
        elif self.mode == "MANUAL" and self.manual_input == "wheel" and self.manual_value is not None:
            self.wheel_ref = self.manual_value
        elif self.wheel_ref == 0.0:
            self.wheel_ref = OMEGA_BIAS
        previous_wheel = self.wheel
        self.wheel += (self.wheel_ref - self.wheel) * min(1.0, dt / 0.15)

        # Box heading: a slow sine, the commanded rate in MANUAL rate, the slew profile,
        # plus the reaction to any wheel speed change. Shapes, not physics.
        reaction = -(self.wheel - previous_wheel) / dt * 0.357 / 1000.0 * 0.2
        if self.mode == "MANUAL" and self.manual_input == "rate":
            target = 0.0 if stale or self.manual_value is None else self.manual_value
            self.rate += (target - self.rate) * min(1.0, dt / 0.3)
        elif self.mode == "SLEW" and self.slew:
            start, goal, t_start = self.slew
            delta = wrap_deg(goal - start)
            fraction = min(1.0, (now - t_start) / SLEW_S)
            target_psi = start + delta * (0.5 - 0.5 * math.cos(math.pi * fraction))
            self.rate = (target_psi - self.psi) / dt
            self.psi_ref = wrap_deg(target_psi)
        elif self.mode == "HOLD":
            self.rate = (self.psi_ref - self.psi) * 0.5
        else:
            self.rate = 4.0 * math.cos(2 * math.pi * (now - self.t0) / 30.0) + reaction
        self.psi += self.rate * dt

        flags = []
        if self.mode == "SAFE" or OMEGA_LOW + 300 < self.wheel < OMEGA_CAP - 300:
            flags.append("MOMENTUM_OK")
        else:
            flags.append("MOMENTUM_NEAR_LIMIT")
        if stale:
            flags.append("MANUAL_STALE")
        flags.extend(self.latched)

        throttle = 0
        if self.wheel_ref > 1.0 or self.wheel > 50.0:
            throttle = int(min(2047, max(48, 48 + self.wheel_ref * 0.17)))
        if self.mode == "MANUAL" and self.manual_input == "throttle" and self.manual_value is not None:
            throttle = int(self.manual_value)
        psi_wire = wrap_deg(self.psi + self.psi_offset)
        self._emit(None, {
            "v": 1, "type": "tlm", "seq": self._next_seq(), "t_ms": self._t_ms(), "mode": self.mode,
            "manual_input": self.manual_input if self.mode == "MANUAL" else None,
            "psi_deg": round(psi_wire, 2),
            "psi_ref_deg": None if self.psi_ref is None or self.mode in ("SAFE", "SPINUP", "READY", "DETUMBLE")
            else round(wrap_deg(self.psi_ref + self.psi_offset), 2),
            "rate_dps": round(self.rate, 2),
            "mag_deg": round(wrap_deg(psi_wire + random.uniform(-0.4, 0.4)), 2),
            "sun_deg": None, "sun_valid": False,
            "wheel_rpm": round(max(0.0, self.wheel), 1), "wheel_ref_rpm": round(max(0.0, self.wheel_ref), 1),
            "throttle": throttle, "tau_cmd_mNm": round(-reaction * 2.0, 2),
            "vbat": round(10.1 + random.uniform(-0.03, 0.03), 2),
            "loop_us": random.randint(690, 790), "flags": flags,
            "t_c": 23.5, "p_pa": 101300.0, "tlm_dropped": 0, "dropped_events": 0,
        })


async def run(args):
    box = Box(Schema(), set(args.inputs.split(",")))
    clients = set()

    def flush():
        while box.outbox:
            target, message = box.outbox.pop(0)
            text = json.dumps(message, separators=(",", ":"))
            if target is None:
                broadcast(clients, text)
            elif target in clients:
                broadcast([target], text)

    async def handler(conn):
        if len(clients) >= MAX_CLIENTS:
            log("refusing a client: 3 already connected")
            await conn.close(1013, "too many clients")
            return
        clients.add(conn)
        log(f"client {id(conn) % 10000} connected from {conn.remote_address}")
        try:
            async for text in conn:
                box.receive(conn, text if isinstance(text, str) else text.decode(errors="replace"))
                flush()
        except ConnectionClosed:
            pass
        finally:
            clients.discard(conn)
            box.disconnected(conn)

    async def ticker():
        next_at = time.monotonic()
        while True:
            box.tick()
            flush()
            next_at += TICK_S
            await asyncio.sleep(max(0.0, next_at - time.monotonic()))

    async with serve(handler, args.host, args.port, max_size=4096):
        log(f"listening on ws://{args.host}:{args.port}/ (inputs: {args.inputs})")
        await ticker()


def selftest():
    schema = Schema()
    if schema.error({"v": 1, "type": "tlm", "seq": 1}) is None:
        print("mock-box selftest: FAILED, the validator accepted an incomplete tlm", file=sys.stderr)
        return 1

    class Conn:
        pass

    box = Box(schema, {"throttle", "wheel", "rate"})
    a, b = Conn(), Conn()
    send = lambda conn, msg: box.receive(conn, json.dumps(msg))
    box.tick()
    send(a, {"v": 1, "type": "ping", "seq": 1, "t_client_ms": 10.5})
    send(a, {"v": 1, "type": "cmd", "seq": 2, "cmd": "arm"})
    send(b, {"v": 1, "type": "cmd", "seq": 3, "cmd": "mode", "mode": "READY"})      # not controlling client
    box.mode_since -= SPINUP_S
    box.tick()                                                                        # SPINUP to READY
    send(a, {"v": 1, "type": "cmd", "seq": 4, "cmd": "mode", "mode": "MANUAL", "input": "rate"})
    box.manual_last -= 1.0
    send(a, {"v": 1, "type": "cmd", "seq": 5, "cmd": "manual", "rate_dps": 30})
    send(a, {"v": 1, "type": "cmd", "seq": 6, "cmd": "manual", "rate_dps": 40})      # too fast
    box.manual_last -= 1.0
    box.tick()                                                                        # MANUAL_STALE
    send(a, "not json at all")
    send(a, {"v": 2, "type": "ping", "seq": 7, "t_client_ms": 1})
    send(b, {"v": 1, "type": "cmd", "seq": 8, "cmd": "estop"})
    box.last_heartbeat -= 5.0
    box.tick()
    send(a, {"v": 1, "type": "cmd", "seq": 9, "cmd": "arm"})
    box.last_heartbeat -= 5.0
    box.tick()                                                                        # HEARTBEAT_LOST
    kinds = {}
    for _target, message in box.outbox:
        key = message["type"] + (":" + message["event"] if message["type"] == "event" else "") \
            + (":ok" if message.get("ok") is True else ":refused" if message.get("ok") is False else "")
        kinds[key] = kinds.get(key, 0) + 1
    needed = {"tlm", "pong", "ack:ok", "ack:refused", "event:mode"}
    missing = needed - set(kinds)
    causes = [m["cause"] for _t, m in box.outbox if m["type"] == "event"]
    tlm_modes = {m["mode"] for _t, m in box.outbox if m["type"] == "tlm"}
    print(f"mock-box selftest: {len(box.outbox)} outbound messages, all valid against the schema "
          f"({', '.join(f'{k} {v}' for k, v in sorted(kinds.items()))})")
    print(f"mock-box selftest: mode causes {causes}; tlm modes {sorted(tlm_modes)}")
    problems = []
    if missing:
        problems.append(f"no {sorted(missing)} produced")
    for cause in ("arm", "spinup_complete", "mode", "ESTOP", "HEARTBEAT_LOST"):
        if cause not in causes:
            problems.append(f"no mode event with cause {cause}")
    if not any("MANUAL_STALE" in m.get("flags", []) for _t, m in box.outbox if m["type"] == "tlm"):
        problems.append("MANUAL_STALE never set")
    for problem in problems:
        print(f"mock-box selftest: FAILED, {problem}", file=sys.stderr)
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", default="127.0.0.1", help="bind address (0.0.0.0 to serve other machines)")
    parser.add_argument("--port", type=int, default=8081)
    parser.add_argument("--inputs", default="throttle,wheel,rate",
                        help="MANUAL inputs the stub accepts, comma-separated (others get 'input not available')")
    parser.add_argument("--selftest", action="store_true", help="validate one of each outbound kind and exit")
    args = parser.parse_args()
    if args.selftest:
        sys.exit(selftest())
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
