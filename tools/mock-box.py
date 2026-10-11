#!/usr/bin/env python3
"""Protocol stub of the DirecSun box, for the bench console and the demo UI.

    tools/mock-box.py [--host 127.0.0.1] [--port 8081] [--inputs throttle,wheel,rate]
                      [--uncharacterised] [--stuck]
    tools/mock-box.py --selftest

Speaks wire schema v1 (protocol/schema-v1.json) the way the firmware does:
50 Hz tlm to every client, an ack for every cmd, an event on every mode change,
pong for every ping. Like the firmware (R3.5), events go only to clients that
have sent at least one message: a read-only viewer such as the main UI's device
link receives only tlm, which carries the mode in every frame. It follows the mode machine of docs/firmware-design.md
section 13.1 (SAFE, arm to SPINUP, READY about 6 s later, once the wheel is at its bias, mode commands, MANUAL with
its 250 ms staleness rule), the controlling-client rule and the 1 s heartbeat of
section 14.4, and the e-stop. It is not a plant model: headings and speeds are
plausible shapes, not physics, and it is no evidence about the hardware.

Refusals use the firmware's reasons and guard order (modes.cpp): arm needs SAFE, no
ramp-down in progress, bench_mode when the Hall is bypassed, a box turning slower than
5 deg/s, and a pack above 9.6 V. DETUMBLE, SUN_POINT and respin answer not implemented.
With --uncharacterised the wheel loop is unavailable, so SLEW is refused and the box
may never leave SPINUP with the Hall fitted, as on the R3.5 firmware.

HOLD and SLEW follow the firmware's rules (R4.3, FD sections 12.4 and 13.1):
entered from READY only, and only once the wheel loop is available (the
T-5 feed-forward table is set; the mock starts with one unless
--uncharacterised). SLEW moves psi_ref along the firmware's trapezoid, 60 deg/s
and 120 deg/s^2 on the shortest wrapped path, and ends with slew_complete once
the box has sat within 3 deg and 5 deg/s of the target for 0.5 s after the
profile, or with SLEW_TIMEOUT after 6 s. The heading closes on psi_ref through
the firmware's attitude law (K_psi 3.1 1/s, 90 deg/s limit, 1 deg hold
deadband) and a 0.1 s lag standing in for the rate loop. --stuck makes a box
that cannot turn, so every SLEW times out.

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
SPINUP_RAMP_RPM_PER_S = 400.0   # kWheelRefRampRpmPerS: omega_ref slope in SPINUP (FD section 13.2)
SPINUP_BAND = 0.05              # kSpinupBand: READY needs the wheel within 5 % of the target
SPINUP_DWELL_S = 0.5            # kSpinupDwellMs: ... for this long
RAMP_S = 5.0             # e-stop and heartbeat ramp-down (FD section 14.3)
ARM_MAX_RATE_DPS = 5.0          # kArmMaxRateDps: the box must be still to arm
ARM_MIN_BATTERY_V = 9.6         # kArmMinBatteryV
MOMENTUM_WARN_RPM = 300.0       # kMomentumWarnRpm: the MOMENTUM_NEAR_LIMIT band
MIN_RAMP_THROTTLE = 48          # kDshotMinThrottle: a drop to SAFE ramps only from a real throttle
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
# HOLD and SLEW as the firmware (R4.3): controllers.hpp and FD section 12.
SLEW_RATE_DPS, SLEW_ACCEL_DPS2 = 60.0, 120.0
SLEW_SETTLE_DEG, SLEW_SETTLE_RATE_DPS, SLEW_SETTLE_S = 3.0, 5.0, 0.5
SLEW_TIMEOUT_S = 6.0
K_PSI, ATTITUDE_RATE_LIMIT_DPS, HOLD_DEADBAND_DEG = 3.1, 90.0, 1.0
RATE_LAG_S = 0.1                          # the rate loop, as a shape
BOX_INERTIA = 1.095e-3                    # I_b + I_w, kg m^2 (TD section 3.2)
WHEEL_RPM_PER_BOX_DPS = 1.03e-3 / 6.5e-5 / 6.0   # momentum exchange, I_b / I_w in rpm per deg/s
# A plausible T-5 table, on the mock's own throttle shape (48 + 0.17 rpm).
DEFAULT_FF_TABLE = [{"rpm": 500 * (i + 1), "throttle": round(48 + 0.17 * 500 * (i + 1))} for i in range(8)]
SETPOINT_KEY = {"throttle": "throttle", "wheel": "wheel_rpm", "rate": "rate_dps"}


def log(text):
    print(f"[mock-box {time.time():.3f}] {text}", flush=True)


def wrap_deg(angle):
    wrapped = math.remainder(angle, 360.0)
    return 180.0 if wrapped <= -180.0 else wrapped


def wire_angle(angle):
    """An angle as the firmware puts it on the wire: two decimals, inside (-180, 180]."""
    return wrap_deg(round(wrap_deg(angle), 2))


def trapezoid(distance, t):
    """Degrees covered t seconds into a move of `distance` (>= 0) at the slew
    limits, and whether the move has ended: SlewProfile in the firmware core."""
    a = SLEW_ACCEL_DPS2
    t_a = SLEW_RATE_DPS / a
    if distance >= a * t_a * t_a:
        peak, t_c = SLEW_RATE_DPS, (distance - a * t_a * t_a) / SLEW_RATE_DPS
    else:
        t_a = math.sqrt(distance / a)
        peak, t_c = a * t_a, 0.0
    total = 2 * t_a + t_c
    if t >= total:
        return distance, True
    if t <= 0:
        return 0.0, False
    if t < t_a:
        return 0.5 * a * t * t, False
    if t < t_a + t_c:
        return 0.5 * a * t_a * t_a + peak * (t - t_a), False
    left = total - t
    return distance - 0.5 * a * left * left, False


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

    def __init__(self, schema, inputs, characterised=True, stuck=False, clock=time.monotonic):
        self.schema = schema
        self.inputs = inputs
        self.stuck = stuck                # a box that cannot turn: every SLEW times out
        self.clock = clock
        self.t0 = self.clock()
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
        self.psi_ref = None               # continuous, the mock's own frame; wire = wrap(psi_ref + offset)
        self.slew = None                  # (start psi, signed move, start time)
        self.slew_settled_since = None
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
        if characterised:
            self.params["ff_table"] = DEFAULT_FF_TABLE
        self.wheel_hold = False           # bench wheel_step hold (R3.8)
        self.vbat = 10.1                  # pack voltage the arm guard reads; the tests can lower it
        self.spinup_band_since = None     # when the wheel entered the 5 % band around the target
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
        return int((self.clock() - self.t0) * 1000)

    def _set_mode(self, mode, cause):
        if mode == self.mode:
            return
        self.mode = mode
        self.mode_since = self.clock()
        self.wheel_hold = False
        self.spinup_band_since = None
        if mode != "MANUAL":
            self.manual_input = None
            self.manual_value = None
        if mode not in ("HOLD", "SLEW", "SUN_POINT"):
            self.psi_ref = None
            self.slew = None
            self.slew_settled_since = None
        log(f"mode {mode} (cause {cause})")
        self._emit(None, {"v": 1, "type": "event", "seq": self._next_seq(), "t_ms": self._t_ms(),
                          "event": "mode", "mode": mode, "cause": cause})

    def _to_safe(self, cause, latch=None):
        if latch and latch not in self.latched:
            self.latched.append(latch)
        if self.mode != "SAFE" and self.last_throttle >= MIN_RAMP_THROTTLE:
            self.ramp = (self.clock(), self.wheel)   # begin_ramp(): nothing to ramp from zero throttle
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
                self.last_heartbeat = self.clock()
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
            self.last_heartbeat = self.clock()

        if cmd == "arm":
            # The firmware's guards, in its order (modes.cpp, Arm).
            if self.mode != "SAFE":
                return self._ack(conn, seq, False, "not in SAFE")
            if self.ramp:
                return self._ack(conn, seq, False, "ramp-down in progress")
            if not self.params["hall_installed"] and not self.params["bench_mode"]:
                return self._ack(conn, seq, False, "bench_mode required")
            if not abs(self.rate) < ARM_MAX_RATE_DPS:
                return self._ack(conn, seq, False, "box not still")
            if not self.vbat > ARM_MIN_BATTERY_V:
                return self._ack(conn, seq, False, "battery low")
            # The firmware also refuses on a latched fault it cannot release. The mock
            # latches only ESTOP and HEARTBEAT_LOST, and arm releases both.
            self.controller = conn
            self.last_heartbeat = self.clock()
            self.latched.clear()
            self.spinup_target = OMEGA_BIAS
            self._ack(conn, seq, True)
            self._set_mode("SPINUP", "arm")
        elif cmd == "disarm":
            if self.mode == "SAFE":
                return self._ack(conn, seq, False, "not armed")
            self._ack(conn, seq, True)
            self._to_safe("disarm")
        elif cmd == "mode":
            self._mode_command(conn, seq, message)
        elif cmd == "manual":
            self._manual(conn, seq, message)
        elif cmd == "respin":
            self._ack(conn, seq, False, "not implemented")   # as the firmware until a later set builds it
        elif cmd == "zero_heading":
            # As the firmware (R4.2): SAFE and READY only, so a zero can never move
            # the reference of a HOLD or SLEW in progress.
            if self.mode not in ("SAFE", "READY"):
                return self._ack(conn, seq, False, "not in SAFE or READY")
            self.psi_offset = -self.psi   # the wire heading is psi + offset: 0 now, whatever came before
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
        if target in ("DETUMBLE", "SUN_POINT"):
            return self._ack(conn, seq, False, "not implemented")   # in every state, as the firmware
        if target == "READY":
            if self.mode == "READY":
                self.wheel_hold = False   # `mode READY` in READY ends a wheel-step hold
                self._ack(conn, seq, True)
            elif self.mode in ACTIVE_MODES:
                self._ack(conn, seq, True)
                self._set_mode("READY", "mode")
            elif self.mode == "SPINUP":
                self._ack(conn, seq, False, "wheel not at bias")
            else:
                self._ack(conn, seq, False, "not armed")
            return
        if target in ("HOLD", "SLEW"):
            # As the firmware supervisor (R4.3): from READY only, and only with the
            # closed wheel loop, with the same refusal reasons.
            if self.mode != "READY":
                return self._ack(conn, seq, False, "not in READY")
            if not self._wheel_loop_available():
                return self._ack(conn, seq, False, "wheel loop not available")
            self._ack(conn, seq, True)
            self._set_mode(target, "mode")
            self.psi_ref = self.psi
            if target == "SLEW":
                # The target is relative to the last zero_heading. Shortest wrapped
                # path; a half-turn goes counter-clockwise, as the firmware's does,
                # which is negative on the wire.
                goal = message["psi_ref_deg"] - self.psi_offset
                self.slew = (self.psi, -wrap_deg(self.psi - goal), self.clock())
                self.slew_settled_since = None
            return
        if self.mode != "READY":
            return self._ack(conn, seq, False, "not in READY")
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
            self.manual_last = self.clock()
            return
        self._ack(conn, seq, True)
        self._set_mode(target, "mode")

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
        now = self.clock()
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
        now = self.clock()
        dt = TICK_S
        if self.mode != "SAFE" and now - self.last_heartbeat > HEARTBEAT_S:
            self._to_safe("HEARTBEAT_LOST", latch="HEARTBEAT_LOST")
        if self.mode == "SPINUP":
            if self.params["hall_installed"]:
                # READY once the wheel has sat within 5 % of the target for 0.5 s (FD section 13.2).
                if abs(self.wheel - self.spinup_target) <= SPINUP_BAND * self.spinup_target:
                    if self.spinup_band_since is None:
                        self.spinup_band_since = now
                    if now - self.spinup_band_since >= SPINUP_DWELL_S - 1e-9:
                        self._set_mode("READY", "spinup_complete")
                else:
                    self.spinup_band_since = None
            elif now - self.mode_since >= self.params["spinup_throttle"] / SPINUP_RAMP_PER_S + SPINUP_DWELL_S - 1e-9:
                self._set_mode("READY", "spinup_complete")   # Hall bypassed: the open-loop ramp, then the dwell
        slew_ended = False
        if self.mode == "SLEW" and self.slew:
            # The firmware's rules (FD section 12.4): complete once the profile has
            # ended and the box has sat within 3 deg and 5 deg/s for 0.5 s; time
            # out after 6 s.
            start, move, t_start = self.slew
            covered, slew_ended = trapezoid(abs(move), now - t_start)
            self.psi_ref = start + math.copysign(covered, move)
            settled = slew_ended and abs(start + move - self.psi) < SLEW_SETTLE_DEG \
                and abs(self.rate) < SLEW_SETTLE_RATE_DPS
            if not settled:
                self.slew_settled_since = None
            elif self.slew_settled_since is None:
                self.slew_settled_since = now
            if settled and now - self.slew_settled_since >= SLEW_SETTLE_S - 1e-9:
                self._set_mode("READY", "slew_complete")
            elif now - t_start >= SLEW_TIMEOUT_S - 1e-9:
                self._set_mode("READY", "SLEW_TIMEOUT")

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
            self.wheel_ref = min(self.spinup_target, SPINUP_RAMP_RPM_PER_S * (now - self.mode_since))
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
        previous_rate = self.rate
        attitude = self.mode in ("HOLD", "SLEW") and self.psi_ref is not None
        if self.mode == "MANUAL" and self.manual_input == "rate":
            target = 0.0 if stale or self.manual_value is None else self.manual_value
            self.rate += (target - self.rate) * min(1.0, dt / 0.3)
        elif attitude:
            # The firmware's attitude law on the wrapped error, with the hold
            # deadband once the reference stands still (HOLD, or a SLEW whose
            # profile has ended); the rate loop as a lag.
            error = wrap_deg(self.psi_ref - self.psi)
            still = self.mode == "HOLD" or slew_ended
            command = 0.0 if still and abs(error) < HOLD_DEADBAND_DEG \
                else max(-ATTITUDE_RATE_LIMIT_DPS, min(ATTITUDE_RATE_LIMIT_DPS, K_PSI * error))
            self.rate += ((0.0 if self.stuck else command) - self.rate) * min(1.0, dt / RATE_LAG_S)
        elif self.mode == "READY" and self._wheel_loop_available():
            self.rate += (0.0 - self.rate) * min(1.0, dt / RATE_LAG_S)   # the rate loop holds 0 (R3.9)
        else:
            self.rate = 4.0 * math.cos(2 * math.pi * (now - self.t0) / 30.0) + reaction
        if self.stuck and self.mode != "SAFE":
            self.rate = 0.0
        self.psi += self.rate * dt
        tau_mnm = -reaction * 2.0
        if attitude:
            # The wheel takes up the box's change of momentum, and the torque is
            # what that change needed. Signs are wire-frame shapes.
            tau_mnm = BOX_INERTIA * math.radians(self.rate - previous_rate) / dt * 1000.0
            self.wheel_ref = min(self._cap(), max(OMEGA_LOW, self.wheel_ref
                                                  - WHEEL_RPM_PER_BOX_DPS * (self.rate - previous_rate)))

        flags = []
        if self.params["hall_installed"] and self.mode in ("READY", "MANUAL", "HOLD", "SLEW"):
            # The firmware sets a momentum flag only where the wheel speed is measured and held.
            near = self.wheel > self._cap() - MOMENTUM_WARN_RPM or self.wheel < OMEGA_LOW + MOMENTUM_WARN_RPM
            flags.append("MOMENTUM_NEAR_LIMIT" if near else "MOMENTUM_OK")
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
        elif self.mode in ("READY", "HOLD", "SLEW") and self._wheel_loop_available():
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
        self._emit(None, {
            "v": 1, "type": "tlm", "seq": self._next_seq(), "t_ms": self._t_ms(), "mode": self.mode,
            "manual_input": self.manual_input if self.mode == "MANUAL" else None,
            "psi_deg": wire_angle(self.psi + self.psi_offset),
            "psi_ref_deg": None if self.psi_ref is None or self.mode in ("SAFE", "SPINUP", "READY", "DETUMBLE")
            else wire_angle(self.psi_ref + self.psi_offset),
            "rate_dps": round(self.rate, 2),
            "mag_deg": None,   # as the firmware: no magnetometer heading until Set 5
            "sun_deg": None, "sun_valid": False,
            "wheel_rpm": round(max(0.0, self.wheel), 1),
            # As the firmware: the wheel reference is reported only while the closed
            # wheel loop runs (SPINUP, READY, HOLD, and SLEW with the T-5 table), else 0.
            "wheel_ref_rpm": round(max(0.0, self.wheel_ref), 1)
            if self.mode in ("SPINUP", "READY", "HOLD", "SLEW") and self._wheel_loop_available() else 0,
            "throttle": throttle, "throttle_band": [band_lo, band_hi], "tau_cmd_mNm": round(tau_mnm, 2),
            "vbat": round(self.vbat + random.uniform(-0.03, 0.03), 2),
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


async def start_box(host, port, inputs, characterised=True, stuck=False):
    """Start the mock box. Returns (server, ticker task); the server's port is in server.sockets."""
    box = Box(Schema(), set(inputs.split(",")), characterised=characterised, stuck=stuck)
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
    log(f"listening on ws://{host}:{bound}{WS_PATH} (inputs: {inputs}; "
        f"{'characterised' if characterised else 'uncharacterised: no ff_table'}{'; stuck' if stuck else ''})")
    return server, asyncio.ensure_future(ticker())


async def run(args):
    server, ticker = await start_box(args.host, args.port, args.inputs, not args.uncharacterised, args.stuck)
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


SPINUP_RUN_S = 8.0   # simulated seconds that always cover a spin-up: the ramp takes 6 s, then the 0.5 s dwell


def finish_spinup(box):
    """Puts the wheel at the target for the dwell, so the next tick completes SPINUP."""
    box.wheel = box.spinup_target
    box.spinup_band_since = box.clock() - SPINUP_DWELL_S
    box.tick()
    box.tick()


def selftest_parity(schema, box_cls=Box):
    """The firmware's answers on every path the UI can reach (R4.1 Approach 5).

    Each case compares the exact ack reason with modes.cpp. It drives the box only through
    its public surface (receive, tick, a few attributes), so the same cases can be run
    against an older mock to show they fail there."""

    class Clock:
        def __init__(self):
            self.t = 1000.0

        def __call__(self):
            return self.t

    class Conn:
        pass

    problems = []

    def make(**kwargs):
        clock = Clock()
        return box_cls(schema, {"throttle", "wheel", "rate"}, clock=clock, **kwargs), Conn(), clock

    def send(box, conn, seq, **cmd):
        box.receive(conn, json.dumps({"v": 1, "type": "cmd", "seq": seq, **cmd}))
        return next(m for _t, m in reversed(box.outbox) if m["type"] == "ack" and m["seq"] == seq)

    def run(box, clock, seconds, conn=None):
        start = len(box.outbox)
        for _ in range(int(round(seconds / TICK_S))):
            clock.t += TICK_S
            if conn is not None:
                box.controller = conn
                box.last_heartbeat = clock.t
            box.tick()
        return [m for _t, m in box.outbox[start:]]

    def expect(label, ack, ok, reason=None):
        got = (ack["ok"], ack.get("reason"))
        if got != (ok, reason):
            problems.append(f"{label}: expected ok {ok} reason {reason!r}, got ok {got[0]} reason {got[1]!r}")

    def armed_to(state):
        """A box in SPINUP (running 1 s) or READY, with its controlling client."""
        box, conn, clock = make()
        expect(f"setup arm for {state}", send(box, conn, 1, cmd="arm"), True)
        run(box, clock, 1.0 if state == "SPINUP" else SPINUP_RUN_S, conn)
        if box.mode != state:
            problems.append(f"setup: expected {state}, the box is in {box.mode}")
        return box, conn, clock

    # The table of differences: command, state, the firmware's answer.
    box, conn, clock = make()
    expect("disarm in SAFE", send(box, conn, 2, cmd="disarm"), False, "not armed")
    expect("mode READY in SAFE", send(box, conn, 3, cmd="mode", mode="READY"), False, "not armed")
    expect("mode DETUMBLE in SAFE", send(box, conn, 4, cmd="mode", mode="DETUMBLE"), False, "not implemented")
    expect("mode SUN_POINT in SAFE", send(box, conn, 5, cmd="mode", mode="SUN_POINT"), False, "not implemented")
    expect("respin in SAFE", send(box, conn, 6, cmd="respin"), False, "not implemented")
    expect("mode MANUAL in SAFE", send(box, conn, 7, cmd="mode", mode="MANUAL", input="rate"), False, "not in READY")
    expect("mode HOLD in SAFE", send(box, conn, 8, cmd="mode", mode="HOLD"), False, "not in READY")

    box, conn, clock = armed_to("SPINUP")
    expect("arm in SPINUP", send(box, conn, 2, cmd="arm"), False, "not in SAFE")
    expect("mode READY in SPINUP", send(box, conn, 3, cmd="mode", mode="READY"), False, "wheel not at bias")
    expect("mode DETUMBLE in SPINUP", send(box, conn, 4, cmd="mode", mode="DETUMBLE"), False, "not implemented")

    box, conn, clock = armed_to("READY")
    expect("mode SUN_POINT in READY", send(box, conn, 2, cmd="mode", mode="SUN_POINT"), False, "not implemented")
    expect("mode DETUMBLE in READY", send(box, conn, 3, cmd="mode", mode="DETUMBLE"), False, "not implemented")
    expect("respin in READY", send(box, conn, 4, cmd="respin"), False, "not implemented")
    expect("respin with a speed in READY", send(box, conn, 5, cmd="respin", wheel_rpm=1500), False, "not implemented")
    expect("arm in READY", send(box, conn, 6, cmd="arm"), False, "not in SAFE")

    # Arm within 5 s of an e-stop, a disarm, or a lost heartbeat; the ramp ends after 5 s.
    for how in ("estop", "disarm", "heartbeat"):
        box, conn, clock = armed_to("READY")
        if how == "estop":
            expect("estop from READY", send(box, conn, 2, cmd="estop"), True)
        elif how == "disarm":
            expect("disarm from READY", send(box, conn, 2, cmd="disarm"), True)
        else:
            run(box, clock, 2.0)                       # no heartbeat: HEARTBEAT_LOST after 1 s
        if box.mode != "SAFE":
            problems.append(f"{how}: the box is in {box.mode}, not SAFE")
        run(box, clock, 1.0)
        expect(f"arm 1 s after {how}", send(box, Conn(), 3, cmd="arm"), False, "ramp-down in progress")
        run(box, clock, RAMP_S + 0.5)
        expect(f"arm 6 s after {how}", send(box, Conn(), 4, cmd="arm"), True)

    # An e-stop before any throttle has nothing to ramp, so a re-arm is allowed at once.
    box, conn, clock = make()
    send(box, conn, 1, cmd="arm")
    expect("estop in the first tick of SPINUP", send(box, conn, 2, cmd="estop"), True)
    run(box, clock, 0.1)
    expect("arm right after an e-stop with no throttle", send(box, Conn(), 3, cmd="arm"), True)

    # Arm guards, in the firmware's order.
    def guarded(label, reason, **setup):
        box, conn, clock = make()
        if "ramp" in setup:
            box.ramp = (clock(), 2400.0)
        if "hall" in setup:
            box.params["hall_installed"] = False
        if "rate" in setup:
            box.rate = setup["rate"]
            box.tick = lambda *a, **k: None            # hold the injected rate for the check
        if "vbat" in setup:
            box.vbat = setup["vbat"]
        expect(label, send(box, conn, 1, cmd="arm"), False, reason)

    guarded("arm while a ramp-down runs, with every other guard failing too", "ramp-down in progress",
            ramp=True, hall=True, rate=9.0, vbat=9.0)
    guarded("arm without the Hall and without bench mode", "bench_mode required", hall=True, rate=9.0, vbat=9.0)
    guarded("arm while the box turns at 9 deg/s", "box not still", rate=9.0, vbat=9.0)
    guarded("arm with a 9.0 V pack", "battery low", vbat=9.0)
    box, conn, clock = make()
    box.rate = 5.0
    box.tick = lambda *a, **k: None
    expect("arm at exactly 5 deg/s (the guard is strict)", send(box, conn, 1, cmd="arm"), False, "box not still")
    box, conn, clock = make()
    box.vbat = 9.6
    expect("arm at exactly 9.6 V (the guard is strict)", send(box, conn, 1, cmd="arm"), False, "battery low")

    # SPINUP takes about 6.2 s: omega_ref ramps at 400 rpm/s, then a 0.5 s dwell (FD section 13.2).
    box, conn, clock = make()
    send(box, conn, 1, cmd="arm")
    started = clock()
    ready_at = None
    for _ in range(int(12 / TICK_S)):
        clock.t += TICK_S
        box.controller, box.last_heartbeat = conn, clock.t
        box.tick()
        if box.mode == "READY":
            ready_at = clock() - started
            break
    if ready_at is None or not 5.8 <= ready_at <= 7.0:
        problems.append(f"spin-up reached READY after {ready_at} s, expected about 6.2 s (5.8 to 7.0)")

    # Momentum flags: only in READY, MANUAL, HOLD and SLEW, and only with the Hall fitted.
    def momentum(box):
        tlm = [m for _t, m in box.outbox if m["type"] == "tlm"][-1]
        return [flag for flag in tlm["flags"] if flag.startswith("MOMENTUM")]

    box, conn, clock = make()
    run(box, clock, 0.2)
    if momentum(box):
        problems.append(f"MOMENTUM flags in SAFE: {momentum(box)}")
    send(box, conn, 1, cmd="arm")
    run(box, clock, 1.0, conn)
    if momentum(box):
        problems.append(f"MOMENTUM flags in SPINUP: {momentum(box)}")
    run(box, clock, SPINUP_RUN_S, conn)
    if momentum(box) != ["MOMENTUM_OK"]:
        problems.append(f"MOMENTUM flags in READY: {momentum(box)}, expected MOMENTUM_OK")
    box.params["hall_installed"] = False
    run(box, clock, 0.2, conn)
    if momentum(box):
        problems.append(f"MOMENTUM flags in READY without the Hall: {momentum(box)}")
    return problems


def selftest_attitude(schema):
    """HOLD and SLEW on a simulated clock, against the firmware's numbers (R4.3)."""

    class Clock:
        def __init__(self):
            self.t = 1000.0

        def __call__(self):
            return self.t

    class Conn:
        pass

    problems = []

    def send(box, conn, seq, **cmd):
        box.receive(conn, json.dumps({"v": 1, "type": "cmd", "seq": seq, **cmd}))
        return next(m for _t, m in reversed(box.outbox) if m["type"] == "ack" and m["seq"] == seq)

    def run(box, clock, seconds):
        """Ticks with the operator's heartbeat; returns the tlm frames and mode events."""
        start = len(box.outbox)
        for _ in range(int(round(seconds / TICK_S))):
            clock.t += TICK_S
            box.last_heartbeat = clock.t
            box.tick()
        new = [m for _t, m in box.outbox[start:]]
        return [m for m in new if m["type"] == "tlm"], [m for m in new if m["type"] == "event"]

    def armed(**kwargs):
        clock = Clock()
        box = Box(schema, {"throttle"}, clock=clock, **kwargs)
        conn = Conn()
        send(box, conn, 1, cmd="arm")
        run(box, clock, SPINUP_RUN_S)
        if box.mode != "READY":
            problems.append(f"the attitude selftest box is in {box.mode}, not READY, after arming")
        return box, conn, clock

    # Refusals with the firmware's reasons.
    box, conn, clock = armed(characterised=False)
    for seq, cmd in ((2, {"mode": "HOLD"}), (3, {"mode": "SLEW", "psi_ref_deg": 90})):
        reason = send(box, conn, seq, cmd="mode", **cmd).get("reason")
        if reason != "wheel loop not available":
            problems.append(f"uncharacterised {cmd['mode']} refused with {reason!r}")
    box, conn, clock = armed()
    send(box, conn, 2, cmd="mode", mode="MANUAL", input="throttle")
    reason = send(box, conn, 3, cmd="mode", mode="HOLD").get("reason")
    if reason != "not in READY":
        problems.append(f"HOLD from MANUAL refused with {reason!r}, expected 'not in READY'")

    # The judge's turn: zero, SLEW to 90 (clockwise on the wire), then back to 0.
    # Zeroed twice, the second time somewhere else: each zero reads 0.
    box, conn, clock = armed()
    for seq, where in ((90, 25.0), (2, 40.0)):
        box.psi = where
        send(box, conn, seq, cmd="zero_heading")
        tlm, _events = run(box, clock, TICK_S)
        if abs(tlm[-1]["psi_deg"]) > 0.5:
            problems.append(f"zero_heading at {where} left psi_deg at {tlm[-1]['psi_deg']}")
    t_start = clock.t
    send(box, conn, 3, cmd="mode", mode="SLEW", psi_ref_deg=90)
    tlm, events = run(box, clock, 4.0)
    refs = [(m["t_ms"], m["psi_ref_deg"]) for m in tlm if m["mode"] == "SLEW"]
    peak = max(abs(b[1] - a[1]) / ((b[0] - a[0]) / 1000.0) for a, b in zip(refs, refs[1:]) if b[0] > a[0])
    at_target = next((t for t, ref in refs if ref >= 90.0), None)
    done = [e for e in events if e["event"] == "mode"]
    done_s = (done[0]["t_ms"] - int((t_start - box.t0) * 1000)) / 1000.0 if done else None
    end_psi = tlm[-1]["psi_deg"]
    print(f"mock-box selftest: SLEW 90: psi_ref peak {peak:.1f} deg/s, at 90 after "
          f"{(at_target - int((t_start - box.t0) * 1000)) / 1000.0 if at_target else None} s, "
          f"{done[0]['cause'] if done else 'no event'} at {done_s} s, psi {end_psi}")
    if peak > SLEW_RATE_DPS + 0.5:
        problems.append(f"SLEW psi_ref moved at {peak:.1f} deg/s, over the 60 deg/s limit")
    if at_target is None or abs((at_target - int((t_start - box.t0) * 1000)) / 1000.0 - 2.0) > TICK_S + 1e-6:
        problems.append("SLEW 90 psi_ref did not reach 90 at 2.0 s")
    if not done or done[0]["cause"] != "slew_complete" or not 2.5 <= done_s <= 3.5:
        problems.append(f"SLEW 90 did not end with slew_complete between 2.5 and 3.5 s ({done})")
    if abs(end_psi - 90.0) > SLEW_SETTLE_DEG or tlm[-1]["psi_ref_deg"] is not None:
        problems.append(f"after SLEW 90: psi {end_psi}, psi_ref {tlm[-1]['psi_ref_deg']}")
    send(box, conn, 4, cmd="mode", mode="SLEW", psi_ref_deg=0)
    tlm, events = run(box, clock, 4.0)
    if not any(e.get("cause") == "slew_complete" for e in events) or abs(tlm[-1]["psi_deg"]) > SLEW_SETTLE_DEG:
        problems.append(f"the return SLEW to 0 ended at psi {tlm[-1]['psi_deg']} with {events}")

    # A half-turn always goes the same way: counter-clockwise, negative on the wire.
    box.psi = -box.psi_offset   # exactly 0 on the wire, so 180 is exactly a half-turn
    send(box, conn, 5, cmd="mode", mode="SLEW", psi_ref_deg=180)
    tlm, _events = run(box, clock, 0.5)
    if not tlm[-1]["psi_ref_deg"] < -1.0:
        problems.append(f"a half-turn SLEW moved psi_ref to {tlm[-1]['psi_ref_deg']}, not counter-clockwise")
    # mode READY interrupts a SLEW: ack, event cause mode, no reference, and the box stops there.
    ack = send(box, conn, 6, cmd="mode", mode="READY")
    tlm, events = run(box, clock, 1.0)
    if not ack["ok"] or [e.get("cause") for e in events] != [] or tlm[-1]["mode"] != "READY" \
            or tlm[-1]["psi_ref_deg"] is not None or abs(tlm[-1]["rate_dps"]) > 1.0:
        problems.append(f"mode READY did not end the SLEW cleanly: {ack}, {events}, {tlm[-1]}")
    if not any(m.get("cause") == "mode" and m.get("mode") == "READY" for _t, m in box.outbox[-80:]
               if m["type"] == "event"):
        problems.append("mode READY during a SLEW posted no mode event with cause mode")

    # The main UI's judge turn (frontend PR #10): capture psi_deg, SLEW to start + 90
    # wrapped onto (-180, 180] with two decimals, wait for slew_complete, hold 1.0 s
    # in READY, SLEW back to the captured start. Started near the wrap, unzeroed.
    box.psi = 150.0 - box.psi_offset
    tlm, _events = run(box, clock, 0.2)
    origin = tlm[-1]["psi_deg"]
    seq = 30
    for target in (round(wrap_deg(origin + 90.0), 2), origin):
        seq += 1
        send(box, conn, seq, cmd="mode", mode="SLEW", psi_ref_deg=target)
        tlm, events = run(box, clock, 4.0)
        causes = [e.get("cause") for e in events if e["event"] == "mode"]
        if causes != ["slew_complete"] or abs(wrap_deg(tlm[-1]["psi_deg"] - target)) > SLEW_SETTLE_DEG:
            problems.append(f"UI turn leg to {target}: causes {causes}, psi {tlm[-1]['psi_deg']}")
        tlm, events = run(box, clock, 1.0)
        if events or tlm[-1]["mode"] != "READY":
            problems.append(f"UI turn hold after the leg to {target}: {events}, mode {tlm[-1]['mode']}")
    print(f"mock-box selftest: UI turn from {origin}: out to {round(wrap_deg(origin + 90.0), 2)} and back, "
          f"psi {tlm[-1]['psi_deg']}")

    # HOLD: poked 10 deg off, back within 2 deg in under 2 s; the reference stays.
    run(box, clock, 1.0)
    send(box, conn, 7, cmd="mode", mode="HOLD")
    tlm, _events = run(box, clock, 0.1)
    held = tlm[-1]["psi_ref_deg"]
    box.psi += 10.0
    tlm, _events = run(box, clock, 2.0)
    if abs(wrap_deg(tlm[-1]["psi_deg"] - held)) > 2.0 or tlm[-1]["psi_ref_deg"] != held or tlm[-1]["mode"] != "HOLD":
        problems.append(f"HOLD at {held} poked 10 deg: psi {tlm[-1]['psi_deg']} after 2 s, mode {tlm[-1]['mode']}")

    # A box that cannot turn: SLEW_TIMEOUT at 6.0 s, then READY.
    box, conn, clock = armed(stuck=True)
    t_start = clock.t
    send(box, conn, 2, cmd="mode", mode="SLEW", psi_ref_deg=-90)
    tlm, events = run(box, clock, 6.5)
    ends = [e for e in events if e["event"] == "mode"]
    end_s = (ends[0]["t_ms"] - int((t_start - box.t0) * 1000)) / 1000.0 if ends else None
    print(f"mock-box selftest: stuck SLEW: {ends[0]['cause'] if ends else 'no event'} at {end_s} s, "
          f"then {tlm[-1]['mode']}")
    if not ends or ends[0]["cause"] != "SLEW_TIMEOUT" or abs(end_s - SLEW_TIMEOUT_S) > TICK_S + 1e-6 \
            or tlm[-1]["mode"] != "READY":
        problems.append(f"stuck SLEW did not time out at 6.0 s into READY ({ends})")
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
    finish_spinup(box)                                                                # SPINUP to READY
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
    if box.ramp is not None:
        box.ramp = (box.ramp[0] - RAMP_S, box.ramp[1])
        box.tick()
    box.rate = 0.0
    box.tick()
    send(a, {"v": 1, "type": "cmd", "seq": 9, "cmd": "arm"})
    box.mode_since -= 1.0
    box.tick()                                                                        # throttle is up
    box.last_heartbeat -= 5.0
    box.tick()                                                                        # HEARTBEAT_LOST
    # The box-rule checks number from 101, clear of the feature steps' seqs.
    send(a, {"v": 1, "type": "cmd", "seq": 103, "cmd": "set_params", "params": {"bench_mode": True}})  # mid-ramp
    if box.ramp is not None:
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
    finish_spinup(box)                                                                # READY
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
    problems.extend(selftest_attitude(schema))
    problems.extend(selftest_parity(schema))
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
    parser.add_argument("--uncharacterised", action="store_true",
                        help="start without the T-5 ff_table, as a fresh box: HOLD, SLEW, and wheel_step are "
                             "refused with 'wheel loop not available' until set_params sends one")
    parser.add_argument("--stuck", action="store_true",
                        help="a box that cannot turn, so every SLEW ends in SLEW_TIMEOUT")
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
