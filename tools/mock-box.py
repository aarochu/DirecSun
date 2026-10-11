#!/usr/bin/env python3
"""Protocol stub of the DirecSun box, for the bench console and the demo UI.

    tools/mock-box.py [--host 127.0.0.1] [--port 8081] [--inputs throttle,wheel,rate]
    tools/mock-box.py --selftest

Speaks wire schema v1 (protocol/schema-v1.json) the way the firmware does:
50 Hz tlm to every client, an ack for every cmd, an event on every mode change,
pong for every ping. Like the firmware (R3.5), events go only to clients that
have sent at least one message: a read-only viewer such as the main UI's device
link receives only tlm, which carries the mode in every frame. It follows the mode machine of docs/firmware-design.md
section 13.1 (SAFE, arm to SPINUP, READY 2 s later, mode commands, MANUAL with
its 250 ms staleness rule), the controlling-client rule and the 1 s heartbeat of
section 14.4, and the e-stop. It is not a plant model: headings and speeds are
plausible shapes, not physics, and it is no evidence about the hardware.

Every outbound message is validated against the schema; a violation stops the
mock, because it would mean the mock teaches the UI the wrong protocol. Inbound
messages that fail the schema get ack ok:false with the reason.

The firmware serves port 80 at path /ws, the main UI's endpoint (protocol/README.md).
Port 80 needs root on Linux, so the mock listens on 8081 by default, but it serves
only /ws, like the firmware: point the console or the main UI at
ws://127.0.0.1:8081/ws. Any other path gets HTTP 404, so a client aimed at the wrong
path fails here the same way it would against the box.

Wire frame: every quantity about the spin axis is clockwise positive seen from above,
as the main UI expects. The values here are shapes, not physics, so no conversion
is involved.

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
    from websockets.asyncio.client import connect
    from websockets.asyncio.server import broadcast, serve
    from websockets.exceptions import ConnectionClosed, InvalidStatus
except ImportError:
    _reexec_with_repo_python()

WS_PATH = "/ws"          # the firmware's and the main UI's endpoint path
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
THROTTLE_MIN, THROTTLE_MAX = 150, 400     # the firmware's defaults until T-5 (FD section 16)
BYPASS_THROTTLE_MAX = 400                 # kBypassThrottleMax in the firmware
SPINUP_THROTTLE = 300                     # spinup_throttle default; rung 1 holds it in READY
HALL_EXPECT_THROTTLE = 200                # hall_expect_throttle default; must stay below spinup_throttle
SPINUP_RAMP_PER_S = 200                   # open-loop ramp, DShot units per second (R3.7)
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
        self.last_throttle = 0
        self.params = {"bench_mode": False, "hall_installed": True,
                       "manual_throttle_min": THROTTLE_MIN, "manual_throttle_max": THROTTLE_MAX,
                       "spinup_throttle": SPINUP_THROTTLE, "hall_expect_throttle": HALL_EXPECT_THROTTLE,
                       "bench_underspeed_rpm": 0, "bench_cap_rpm": 0}
        self.wheel_hold = False           # bench wheel_step hold (R3.8)
        self.talkers = set()              # clients that have sent anything get events (R3.5)
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
        self.wheel_hold = False
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
        self.talkers.add(conn)
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
            # As the firmware (R4.2): SAFE and READY only, so a zero can never move
            # the reference of a HOLD or SLEW in progress.
            if self.mode not in ("SAFE", "READY"):
                return self._ack(conn, seq, False, "not in SAFE or READY")
            self.psi_offset -= self.psi
            self._ack(conn, seq, True)
        elif cmd == "set_params":
            if self.mode != "SAFE":
                return self._ack(conn, seq, False, "not in SAFE")
            if self.ramp:
                # SAFE but still ramping down: the firmware waits for the ramp to end (V-65).
                return self._ack(conn, seq, False, "ramp-down in progress")
            update = message["params"]
            # The Hall bench bypass needs bench mode already on: two commands, never one (R3.6).
            if update.get("hall_installed") is False and not self.params.get("bench_mode"):
                return self._ack(conn, seq, False, "bench_mode required: hall_installed")
            if update.get("bench_mode") is False and not update.get("hall_installed", self.params["hall_installed"]):
                return self._ack(conn, seq, False, "bench_mode required: hall_installed")
            # HALL_SILENT must arm in every SPINUP, as in the firmware (FD section 16, V-64).
            merged = {**self.params, **update}
            if merged["hall_expect_throttle"] >= merged["spinup_throttle"]:
                return self._ack(conn, seq, False, "invalid param: hall_expect_throttle")
            bench_after = update.get("bench_mode", self.params["bench_mode"])
            for key in ("bench_underspeed_rpm", "bench_cap_rpm"):
                if update.get(key, 0) != 0 and not bench_after:
                    return self._ack(conn, seq, False, f"bench_mode required: {key}")
            was_bypassed = not self.params["hall_installed"]
            if self.params["bench_mode"] and not bench_after:
                # Turning bench mode off clears the overrides it does not name (R3.8).
                for key in ("bench_underspeed_rpm", "bench_cap_rpm"):
                    if key not in update:
                        self.params[key] = 0
            self.params.update(update)
            self._ack(conn, seq, True, applied=update)
            if was_bypassed != (not self.params["hall_installed"]):
                self._emit(None, {"v": 1, "type": "event", "seq": self._next_seq(), "t_ms": self._t_ms(),
                                  "event": "notice", "name": "HALL_BYPASS",
                                  "detail": "off" if self.params["hall_installed"] else "on"})
        elif cmd == "wheel_step":
            # Same refusal order as the firmware supervisor (R3.8).
            if not self.params.get("bench_mode"):
                return self._ack(conn, seq, False, "bench_mode required")
            if self.mode != "READY":
                return self._ack(conn, seq, False, "not in READY")
            if not self._wheel_loop_available():
                return self._ack(conn, seq, False, "wheel loop not available")
            self.wheel_hold = True
            self.wheel_ref = min(self._cap(), max(OMEGA_LOW, self.wheel_ref + message["delta_rpm"]))
            self._ack(conn, seq, True)

    def _mode_command(self, conn, seq, message):
        target = message["mode"]
        if target == "READY":
            if self.mode in ACTIVE_MODES:
                self._ack(conn, seq, True)
                self._set_mode("READY", "mode")
            elif self.mode == "READY":
                self.wheel_hold = False   # `mode READY` in READY ends a wheel-step hold
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
            if self.manual_input == "throttle":
                # Start from the throttle being sent now, so entry is not a step (FD 13.5).
                lo, hi = self._throttle_band()
                self.manual_value = float(min(hi, max(lo, self.last_throttle)))
            self.manual_last = time.monotonic()
            return
        self._ack(conn, seq, True)
        self._set_mode(target, "mode")
        if target == "HOLD":
            self.psi_ref = wrap_deg(self.psi)
        elif target == "SLEW":
            self.psi_ref = wrap_deg(self.psi)
            self.slew = (self.psi, message["psi_ref_deg"], time.monotonic())

    def _wheel_loop_available(self):
        # The firmware's wheel_loop_available(): Hall fitted and the T-5 table set.
        return bool(self.params["hall_installed"]) and isinstance(self.params.get("ff_table"), list)

    def _cap(self):
        cap = self.params.get("bench_cap_rpm") or 0
        return cap if 0 < cap < OMEGA_CAP else OMEGA_CAP

    def _ff(self, rpm):
        # Linear interpolation on the table, held at the ends (ff_throttle() in the firmware).
        table = self.params["ff_table"]
        if rpm <= table[0]["rpm"]:
            return table[0]["throttle"]
        for lo, hi in zip(table, table[1:]):
            if rpm <= hi["rpm"]:
                f = (rpm - lo["rpm"]) / (hi["rpm"] - lo["rpm"])
                return lo["throttle"] + f * (hi["throttle"] - lo["throttle"])
        return table[-1]["throttle"]

    def _throttle_band(self):
        # Same rule as manual_throttle_band() in the firmware core.
        hi = self.params["manual_throttle_max"]
        if not self.params["hall_installed"]:
            hi = min(hi, BYPASS_THROTTLE_MAX)
        lo = min(self.params["manual_throttle_min"], hi)
        return lo, hi

    def _manual(self, conn, seq, message):
        if self.mode != "MANUAL":
            return self._ack(conn, seq, False, "not in MANUAL")
        key = SETPOINT_KEY[self.manual_input]
        if key not in message:
            return self._ack(conn, seq, False, "input mismatch")
        now = time.monotonic()
        if now - self.manual_last < MANUAL_MIN_SPACING_S and self.manual_value is not None:
            return self._ack(conn, seq, False, "too fast")
        value = float(message[key])
        if key == "throttle":
            lo, hi = self._throttle_band()
            value = min(hi, max(lo, value))
        elif key == "wheel_rpm":
            value = min(OMEGA_CAP, max(OMEGA_LOW, value))
        else:
            value = min(90.0, max(-90.0, value))
        self.manual_value = value
        self.manual_last = now
        self._ack(conn, seq, True)

    def disconnected(self, conn):
        # The controlling client keeps its role until the heartbeat expires (FD 14.4).
        self.talkers.discard(conn)
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
        if not self.params["hall_installed"]:
            flags.append("HALL_BYPASS")
        if self.wheel_hold:
            flags.append("WHEEL_STEP")
        flags.append("HEADING_GYRO")   # gyro-only heading, relative to zero_heading, until Set 5 (R4.2)
        flags.extend(self.latched)

        # Throttle as rung 1 (R3.7) sends it: an open-loop ramp in SPINUP, the
        # spin-up throttle held in READY, the setpoint in MANUAL throttle.
        throttle = 0
        if self.mode == "SPINUP":
            throttle = int(min(SPINUP_THROTTLE, (now - self.mode_since) * SPINUP_RAMP_PER_S))
        elif self.mode == "READY" and self._wheel_loop_available():
            throttle = int(round(self._ff(self.wheel_ref)))   # closed wheel loop at steady state (R3.8)
        elif self.mode != "SAFE":
            throttle = SPINUP_THROTTLE
        elif self.ramp:
            throttle = int(48 + self.wheel_ref * 0.17)
        if 0 < throttle < 48:
            throttle = 0
        if self.mode == "MANUAL" and self.manual_input == "throttle" and self.manual_value is not None:
            throttle = int(self.manual_value)
        self.last_throttle = throttle
        band_lo, band_hi = self._throttle_band()
        psi_wire = wrap_deg(self.psi + self.psi_offset)
        self._emit(None, {
            "v": 1, "type": "tlm", "seq": self._next_seq(), "t_ms": self._t_ms(), "mode": self.mode,
            "manual_input": self.manual_input if self.mode == "MANUAL" else None,
            "psi_deg": round(psi_wire, 2),
            "psi_ref_deg": None if self.psi_ref is None or self.mode in ("SAFE", "SPINUP", "READY", "DETUMBLE")
            else round(wrap_deg(self.psi_ref + self.psi_offset), 2),
            "rate_dps": round(self.rate, 2),
            "mag_deg": None,   # as the firmware: no magnetometer heading until Set 5
            "sun_deg": None, "sun_valid": False,
            "wheel_rpm": round(max(0.0, self.wheel), 1),
            # As the firmware: the wheel reference is reported only while the closed
            # wheel loop runs (SPINUP and READY with the T-5 table), else 0 (R3.8).
            "wheel_ref_rpm": round(max(0.0, self.wheel_ref), 1)
            if self.mode in ("SPINUP", "READY") and self._wheel_loop_available() else 0,
            "throttle": throttle, "throttle_band": [band_lo, band_hi], "tau_cmd_mNm": round(-reaction * 2.0, 2),
            "vbat": round(10.1 + random.uniform(-0.03, 0.03), 2),
            "loop_us": random.randint(690, 790), "flags": flags,
            "t_c": 23.5, "p_pa": 101300.0, "tlm_dropped": 0, "dropped_events": 0,
            **({"gyro_raw": [0, 0, int(max(-32768, min(32767, -self.rate * 16.4)))], "mag_raw": [812, -1234, 300],
                "sun_a_mv": 0, "sun_b_mv": 0} if self.params["bench_mode"] else {}),
        })


def only_ws_path(connection, request):
    """websockets process_request hook: refuse every path but /ws with HTTP 404."""
    path = request.path.split("?", 1)[0]
    if path != WS_PATH:
        log(f"refusing a client on path {request.path!r}: the box serves only {WS_PATH}")
        return connection.respond(404, f"DirecSun serves its WebSocket at {WS_PATH}\n")
    return None


async def start_box(host, port, inputs):
    """Start the mock box. Returns (server, ticker task); the server's port is in server.sockets."""
    box = Box(Schema(), set(inputs.split(",")))
    clients = set()

    def flush():
        while box.outbox:
            target, message = box.outbox.pop(0)
            text = json.dumps(message, separators=(",", ":"))
            if target is None and message["type"] == "tlm":
                broadcast(clients, text)
            elif target is None:
                broadcast([c for c in clients if c in box.talkers], text)
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

    server = await serve(handler, host, port, max_size=4096, process_request=only_ws_path)
    bound = server.sockets[0].getsockname()[1]
    log(f"listening on ws://{host}:{bound}{WS_PATH} (inputs: {inputs})")
    return server, asyncio.ensure_future(ticker())


async def run(args):
    server, ticker = await start_box(args.host, args.port, args.inputs)
    async with server:
        await ticker


async def selftest_endpoint():
    """Positive control for the endpoint: the real server answers on /ws and refuses /."""
    server, ticker = await start_box("127.0.0.1", 0, "throttle,wheel,rate")
    port = server.sockets[0].getsockname()[1]
    problems = []
    try:
        async with connect(f"ws://127.0.0.1:{port}{WS_PATH}") as ws:
            first = json.loads(await asyncio.wait_for(ws.recv(), timeout=2.0))
            if first.get("type") != "tlm":
                problems.append(f"first frame on {WS_PATH} was {first.get('type')!r}, not tlm")
        # Read-only rule (R3.5): a client that never sends gets only tlm, while a
        # client that has spoken receives the events (the positive control).
        async with connect(f"ws://127.0.0.1:{port}{WS_PATH}") as silent, \
                connect(f"ws://127.0.0.1:{port}{WS_PATH}") as talker:
            await talker.send(json.dumps({"v": 1, "type": "ping", "seq": 1, "t_client_ms": 1}))
            await talker.send(json.dumps({"v": 1, "type": "cmd", "seq": 2, "cmd": "arm"}))
            deadline = time.monotonic() + 0.6
            silent_types, talker_types = set(), set()
            while time.monotonic() < deadline:
                for ws, seen in ((silent, silent_types), (talker, talker_types)):
                    try:
                        frame = json.loads(await asyncio.wait_for(ws.recv(), timeout=0.05))
                        seen.add(frame["type"])
                    except asyncio.TimeoutError:
                        pass
            if silent_types != {"tlm"}:
                problems.append(f"a read-only client received {sorted(silent_types)}, not only tlm")
            if "event" not in talker_types:
                problems.append(f"the client that armed got no event ({sorted(talker_types)})")
        for wrong in ("/", "/ws/extra"):
            try:
                async with connect(f"ws://127.0.0.1:{port}{wrong}"):
                    problems.append(f"a client on {wrong!r} was accepted")
            except InvalidStatus as exc:
                if exc.response.status_code != 404:
                    problems.append(f"{wrong!r} got HTTP {exc.response.status_code}, not 404")
    finally:
        ticker.cancel()
        server.close()
        await server.wait_closed()
    if not problems:
        print(f"mock-box selftest: endpoint ok, tlm on {WS_PATH}, HTTP 404 on / and /ws/extra; "
              "a read-only client gets only tlm, a client that spoke gets events")
    return problems


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
    # The box-rule checks number from 101, clear of the feature steps' seqs.
    send(a, {"v": 1, "type": "cmd", "seq": 103, "cmd": "set_params", "params": {"bench_mode": True}})  # mid-ramp
    box.ramp = (box.ramp[0] - RAMP_S, box.ramp[1])
    box.tick()                                                                        # ramp ends
    send(a, {"v": 1, "type": "cmd", "seq": 10, "cmd": "set_params", "params": {"hall_installed": False}})  # refused
    send(a, {"v": 1, "type": "cmd", "seq": 11, "cmd": "set_params", "params": {"bench_mode": True}})
    send(a, {"v": 1, "type": "cmd", "seq": 12, "cmd": "set_params", "params": {"hall_installed": False}})
    send(a, {"v": 1, "type": "cmd", "seq": 101, "cmd": "set_params",
             "params": {"spinup_throttle": 2047, "hall_expect_throttle": 2047}})              # refused
    send(a, {"v": 1, "type": "cmd", "seq": 102, "cmd": "set_params",
             "params": {"spinup_throttle": 2047, "hall_expect_throttle": 2046}})
    box.tick()                                                                        # HALL_BYPASS flag
    # R3.8: bench overrides and the wheel step, with the firmware's refusal reasons.
    send(a, {"v": 1, "type": "cmd", "seq": 13, "cmd": "set_params", "params": {"hall_installed": True}})
    send(a, {"v": 1, "type": "cmd", "seq": 14, "cmd": "set_params", "params": {"bench_mode": False}})
    send(a, {"v": 1, "type": "cmd", "seq": 15, "cmd": "set_params", "params": {"bench_cap_rpm": 2500}})  # refused
    send(a, {"v": 1, "type": "cmd", "seq": 16, "cmd": "wheel_step", "delta_rpm": 300})          # refused: bench
    send(a, {"v": 1, "type": "cmd", "seq": 17, "cmd": "set_params", "params": {
        "bench_mode": True, "bench_cap_rpm": 2500, "bench_underspeed_rpm": 300,
        "ff_table": [{"rpm": 500 * (i + 1), "throttle": 48 + 100 * (i + 1)} for i in range(8)]}})
    box.last_heartbeat = time.monotonic()
    send(a, {"v": 1, "type": "cmd", "seq": 18, "cmd": "wheel_step", "delta_rpm": 300})          # refused: SAFE
    send(a, {"v": 1, "type": "cmd", "seq": 19, "cmd": "arm"})
    box.mode_since -= SPINUP_S
    box.tick()                                                                        # READY
    send(a, {"v": 1, "type": "cmd", "seq": 20, "cmd": "wheel_step", "delta_rpm": 300})          # ok
    box.tick()                                                                        # WHEEL_STEP flag
    stepped_ref = box.wheel_ref
    send(a, {"v": 1, "type": "cmd", "seq": 21, "cmd": "mode", "mode": "READY"})                 # ends the hold
    box.tick()
    # R4.2: zero_heading in READY reads 0 at once; refused in MANUAL with the firmware's reason.
    box.psi = 33.0
    send(a, {"v": 1, "type": "cmd", "seq": 22, "cmd": "zero_heading"})
    box.tick()
    zeroed_psi = [m for _t, m in box.outbox if m["type"] == "tlm"][-1]["psi_deg"]
    send(a, {"v": 1, "type": "cmd", "seq": 23, "cmd": "mode", "mode": "MANUAL", "input": "throttle"})
    send(a, {"v": 1, "type": "cmd", "seq": 24, "cmd": "zero_heading"})                          # refused
    send(a, {"v": 1, "type": "cmd", "seq": 25, "cmd": "mode", "mode": "READY"})
    kinds = {}
    for _target, message in box.outbox:
        key = message["type"] + (":" + message["event"] if message["type"] == "event" else "") \
            + (":ok" if message.get("ok") is True else ":refused" if message.get("ok") is False else "")
        kinds[key] = kinds.get(key, 0) + 1
    needed = {"tlm", "pong", "ack:ok", "ack:refused", "event:mode", "event:notice"}
    missing = needed - set(kinds)
    causes = [m["cause"] for _t, m in box.outbox if m["type"] == "event" and m["event"] == "mode"]
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
    if not any("HALL_BYPASS" in m.get("flags", []) for _t, m in box.outbox if m["type"] == "tlm"):
        problems.append("HALL_BYPASS never set")
    if not any(m.get("name") == "HALL_BYPASS" for _t, m in box.outbox if m["type"] == "event"):
        problems.append("no HALL_BYPASS notice")
    reasons = {m["seq"]: m.get("reason") for _t, m in box.outbox if m["type"] == "ack" and m.get("ok") is False}
    expected = {15: "bench_mode required: bench_cap_rpm", 16: "bench_mode required", 18: "not in READY",
                24: "not in SAFE or READY"}
    for seq, reason in expected.items():
        if reasons.get(seq) != reason:
            problems.append(f"seq {seq} refused with {reasons.get(seq)!r}, expected {reason!r}")
    tlm_flags = [m["flags"] for _t, m in box.outbox if m["type"] == "tlm"]
    if not any("WHEEL_STEP" in f for f in tlm_flags):
        problems.append("WHEEL_STEP never set")
    if "WHEEL_STEP" in tlm_flags[-1]:
        problems.append("WHEEL_STEP still set after mode READY")
    if stepped_ref > 2500:
        problems.append(f"wheel step not clamped to the bench cap (ref {stepped_ref})")
    if not all("HEADING_GYRO" in f for f in tlm_flags):
        problems.append("HEADING_GYRO missing from a tlm frame")
    if abs(zeroed_psi) > 1.0:
        problems.append(f"zero_heading in READY left psi_deg at {zeroed_psi}")
    # Box rules the mock mirrors: each one refused, and the nearest legal update accepted.
    acks = {m["seq"]: m for _t, m in box.outbox if m["type"] == "ack" and m["seq"] in (101, 102, 103)}
    expected = {101: (False, "invalid param: hall_expect_throttle"), 102: (True, None),
                103: (False, "ramp-down in progress")}
    for seq, (ok, reason) in expected.items():
        got = acks.get(seq)
        if got is None or got["ok"] is not ok or got.get("reason") != reason:
            problems.append(f"set_params seq {seq}: expected ok {ok} reason {reason!r}, got {got}")
    problems.extend(asyncio.run(selftest_endpoint()))
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
