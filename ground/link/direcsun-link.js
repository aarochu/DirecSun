// DirecSun ground link: the client side of wire schema v1 (protocol/schema-v1.json,
// docs/firmware-design.md section 15). Dependency-free ES module for browsers and
// Node 20+. Aaron's UI and ground/console import it; neither touches raw frames.
//
// Safety rules this module owns (firmware-design sections 14.4 and 21):
// - it pings at 5 Hz while open, so the controlling client keeps the heartbeat alive;
// - every command resolves or rejects on the ack carrying its seq, or after a timeout;
// - estop() is sent at once, is never queued or rate-limited, and throws if it cannot leave;
// - it never reconnects by itself: after a close, only an explicit connect() opens it again.
//
// Wire values are passed through unchanged, counter-clockwise positive. A UI that
// shows clockwise-positive headings converts for display (finding V-17).

export const PROTOCOL_VERSION = 1;
export const DEFAULT_URL = "ws://direcsun.local:81/";

const WS_OPEN = 1;
const KINDS = ["tlm", "event", "pong", "ack", "state", "raw", "sent"];

const defaultClock = {
  now: () => globalThis.performance.now(),
  setTimeout: (fn, ms) => globalThis.setTimeout(fn, ms),
  clearTimeout: (id) => globalThis.clearTimeout(id),
  setInterval: (fn, ms) => globalThis.setInterval(fn, ms),
  clearInterval: (id) => globalThis.clearInterval(id),
};

// A command the box refused (ack ok:false), timed out, or that was cut off by a close.
export class DirecSunCommandError extends Error {
  constructor(message, { code, seq, cmd, ack = null } = {}) {
    super(message);
    this.name = "DirecSunCommandError";
    this.code = code; // "REFUSED", "TIMEOUT", or "CLOSED"
    this.seq = seq;
    this.cmd = cmd;
    this.ack = ack;
  }
}

export class DirecSunLink {
  // options:
  //   WebSocketImpl     WebSocket constructor (default: globalThis.WebSocket)
  //   clock             { now, setTimeout, clearTimeout, setInterval, clearInterval }
  //   ackTimeoutMs      time to wait for an ack before rejecting (default 1000)
  //   connectTimeoutMs  time to wait for the socket to open (default 6000)
  //   pingIntervalMs    heartbeat period (default 200, i.e. 5 Hz)
  //   manualIntervalMs  minimum spacing of manual setpoints (default 50, i.e. 20 Hz)
  constructor(url = DEFAULT_URL, options = {}) {
    this.url = url;
    this._WebSocket = options.WebSocketImpl ?? globalThis.WebSocket;
    this._clock = options.clock ?? defaultClock;
    this._ackTimeoutMs = options.ackTimeoutMs ?? 1000;
    this._connectTimeoutMs = options.connectTimeoutMs ?? 6000;
    this._pingIntervalMs = options.pingIntervalMs ?? 200;
    this._manualIntervalMs = options.manualIntervalMs ?? 50;

    this._listeners = new Map(KINDS.map((kind) => [kind, new Set()]));
    this._socket = null;
    this._state = "idle"; // idle | connecting | open | closed
    this._seq = 0;
    this._pending = new Map(); // seq -> { resolve, reject, timer, cmd }
    this._pingTimer = null;
    this._pings = new Map(); // ping seq -> client time sent
    this._rtt = { last: null, min: null, max: null, sum: 0, count: 0 };
    this._manualLastSent = -Infinity;
    this._manualQueued = null; // { setpoint, waiters: [{resolve, reject}] }
    this._manualTimer = null;
  }

  get state() {
    return this._state;
  }

  // Round-trip statistics from ping/pong, in ms.
  get rtt() {
    const { last, min, max, sum, count } = this._rtt;
    return { last, min, max, avg: count ? sum / count : null, count };
  }

  // Subscribe to "tlm", "event", "pong", "ack", "state", "raw" (every inbound frame:
  // { data, accepted, reason }), or "sent" (every outbound message object).
  // Returns an unsubscribe function.
  on(kind, fn) {
    const set = this._listeners.get(kind);
    if (!set) throw new Error(`unknown event kind "${kind}"`);
    set.add(fn);
    return () => set.delete(fn);
  }

  // Opens the socket. Resolves when it is open; rejects if it closes first or does
  // not open within connectTimeoutMs.
  connect() {
    if (this._state === "connecting" || this._state === "open") {
      return Promise.reject(new Error(`already ${this._state}`));
    }
    if (typeof this._WebSocket !== "function") {
      return Promise.reject(new Error("no WebSocket implementation available"));
    }
    let socket;
    try {
      socket = new this._WebSocket(this.url);
    } catch (error) {
      this._setState("closed", { reason: `invalid endpoint: ${error.message}` });
      return Promise.reject(error);
    }
    this._socket = socket;
    this._setState("connecting");

    return new Promise((resolve, reject) => {
      let settled = false;
      const timer = this._clock.setTimeout(() => {
        if (settled || this._socket !== socket) return;
        settled = true;
        this._teardown(socket, "connection timeout");
        reject(new Error("connection timeout"));
      }, this._connectTimeoutMs);

      socket.onopen = () => {
        if (this._socket !== socket) return;
        settled = true;
        this._clock.clearTimeout(timer);
        this._setState("open");
        this._pingTimer = this._clock.setInterval(() => this._ping(), this._pingIntervalMs);
        this._ping();
        resolve();
      };
      socket.onmessage = (event) => {
        if (this._socket === socket) this._receive(event.data);
      };
      socket.onerror = () => {};
      socket.onclose = (event) => {
        if (this._socket !== socket) return;
        this._clock.clearTimeout(timer);
        const reason = settled ? "closed by peer" : "connection failed";
        this._teardown(socket, event?.reason || reason, { alreadyClosed: true });
        if (!settled) {
          settled = true;
          reject(new Error(reason));
        }
      };
    });
  }

  // Closes the socket. Pending commands reject with code CLOSED. Nothing reconnects.
  close() {
    if (this._socket) this._teardown(this._socket, "closed by client");
  }

  // Sends {cmd: name, ...fields}. Resolves with the ack when ok is true; rejects with a
  // DirecSunCommandError (code REFUSED, TIMEOUT, or CLOSED) otherwise.
  command(name, fields = {}) {
    if (name === "estop") return this.estop();
    if (name === "manual") return this.manual(fields);
    return this._sendCommand(name, fields);
  }

  // Sends estop now. Never queued, never rate-limited. Throws synchronously when the
  // socket is not open, so the UI can show that the stop did not leave. Returns a
  // promise for the ack.
  estop() {
    if (!this._isOpen()) {
      throw new Error("e-stop not sent: link is not open");
    }
    return this._sendCommand("estop", {});
  }

  // MANUAL setpoint: exactly one of { throttle }, { wheel_rpm }, { rate_dps }.
  // Frames are spaced at least manualIntervalMs apart (the box refuses closer ones as
  // "too fast"). Within one interval the latest setpoint wins; the promise of a
  // superseded setpoint resolves with the ack of the frame that replaced it.
  manual(setpoint) {
    if (!this._isOpen()) {
      return Promise.reject(new Error("manual setpoint not sent: link is not open"));
    }
    const keys = Object.keys(setpoint ?? {}).filter((k) => ["throttle", "wheel_rpm", "rate_dps"].includes(k));
    if (keys.length !== 1 || Object.keys(setpoint).length !== 1) {
      return Promise.reject(new Error("manual setpoint needs exactly one of throttle, wheel_rpm, rate_dps"));
    }
    return new Promise((resolve, reject) => {
      if (this._manualQueued) {
        this._manualQueued.setpoint = { ...setpoint };
        this._manualQueued.waiters.push({ resolve, reject });
        return;
      }
      this._manualQueued = { setpoint: { ...setpoint }, waiters: [{ resolve, reject }] };
      const wait = this._manualLastSent + this._manualIntervalMs - this._clock.now();
      if (wait <= 0) {
        this._flushManual();
      } else {
        this._manualTimer = this._clock.setTimeout(() => this._flushManual(), wait);
      }
    });
  }

  // ---- internals ----

  _isOpen() {
    return this._state === "open" && this._socket?.readyState === WS_OPEN;
  }

  _emit(kind, payload) {
    for (const fn of this._listeners.get(kind)) {
      try {
        fn(payload);
      } catch (error) {
        console.error(`DirecSunLink ${kind} listener failed`, error);
      }
    }
  }

  _setState(state, detail = {}) {
    this._state = state;
    this._emit("state", { state, ...detail });
  }

  _nextSeq() {
    this._seq = (this._seq + 1) % 4294967296;
    return this._seq;
  }

  _send(message) {
    this._socket.send(JSON.stringify(message));
    this._emit("sent", message);
  }

  _sendCommand(name, fields) {
    if (!this._isOpen()) {
      return Promise.reject(new DirecSunCommandError(`${name} not sent: link is not open`, { code: "CLOSED", cmd: name }));
    }
    const seq = this._nextSeq();
    const message = { v: PROTOCOL_VERSION, type: "cmd", seq, cmd: name, ...fields };
    const promise = new Promise((resolve, reject) => {
      const timer = this._clock.setTimeout(() => {
        if (!this._pending.has(seq)) return;
        this._pending.delete(seq);
        reject(new DirecSunCommandError(`${name}: no ack within ${this._ackTimeoutMs} ms`, { code: "TIMEOUT", seq, cmd: name }));
      }, this._ackTimeoutMs);
      this._pending.set(seq, { resolve, reject, timer, cmd: name });
    });
    this._send(message);
    return promise;
  }

  _flushManual() {
    this._manualTimer = null;
    const queued = this._manualQueued;
    this._manualQueued = null;
    if (!queued) return;
    this._manualLastSent = this._clock.now();
    const sent = this._sendCommand("manual", queued.setpoint);
    for (const waiter of queued.waiters) sent.then(waiter.resolve, waiter.reject);
  }

  _ping() {
    if (!this._isOpen()) return;
    const seq = this._nextSeq();
    const now = this._clock.now();
    this._pings.set(seq, now);
    if (this._pings.size > 50) this._pings.delete(this._pings.keys().next().value);
    this._send({ v: PROTOCOL_VERSION, type: "ping", seq, t_client_ms: Math.max(0, Math.round(now * 1000) / 1000) });
  }

  _receive(data) {
    let message;
    try {
      message = JSON.parse(String(data));
    } catch {
      this._emit("raw", { data, accepted: false, reason: "not JSON" });
      return;
    }
    if (message === null || typeof message !== "object" || Array.isArray(message)) {
      this._emit("raw", { data, accepted: false, reason: "not a JSON object" });
      return;
    }
    if (message.v !== PROTOCOL_VERSION) {
      this._emit("raw", { data, accepted: false, reason: `unsupported v ${JSON.stringify(message.v)}` });
      return;
    }
    switch (message.type) {
      case "tlm":
      case "event":
        this._emit("raw", { data, accepted: true });
        this._emit(message.type, message);
        return;
      case "ack":
        this._emit("raw", { data, accepted: true });
        this._settle(message);
        this._emit("ack", message);
        return;
      case "pong":
        this._emit("raw", { data, accepted: true });
        this._recordPong(message);
        this._emit("pong", message);
        return;
      default:
        this._emit("raw", { data, accepted: false, reason: `unknown type ${JSON.stringify(message.type)}` });
    }
  }

  _settle(ack) {
    const pending = this._pending.get(ack.seq);
    if (!pending) return;
    this._pending.delete(ack.seq);
    this._clock.clearTimeout(pending.timer);
    if (ack.ok === true) {
      pending.resolve(ack);
    } else {
      pending.reject(new DirecSunCommandError(`${pending.cmd} refused: ${ack.reason ?? "no reason given"}`, {
        code: "REFUSED", seq: ack.seq, cmd: pending.cmd, ack,
      }));
    }
  }

  _recordPong(pong) {
    const sent = this._pings.get(pong.ping_seq);
    if (sent === undefined) return;
    this._pings.delete(pong.ping_seq);
    const rtt = this._clock.now() - sent;
    const r = this._rtt;
    r.last = rtt;
    r.min = r.min === null ? rtt : Math.min(r.min, rtt);
    r.max = r.max === null ? rtt : Math.max(r.max, rtt);
    r.sum += rtt;
    r.count += 1;
  }

  _teardown(socket, reason, { alreadyClosed = false } = {}) {
    if (this._socket !== socket) return;
    this._socket = null;
    if (this._pingTimer !== null) this._clock.clearInterval(this._pingTimer);
    this._pingTimer = null;
    if (this._manualTimer !== null) this._clock.clearTimeout(this._manualTimer);
    this._manualTimer = null;
    const queued = this._manualQueued;
    this._manualQueued = null;
    this._pings.clear();
    socket.onopen = socket.onmessage = socket.onclose = socket.onerror = null;
    if (!alreadyClosed) {
      try {
        socket.close();
      } catch {
        // The socket may already be closing; nothing else to release.
      }
    }
    for (const [seq, pending] of this._pending) {
      this._clock.clearTimeout(pending.timer);
      pending.reject(new DirecSunCommandError(`${pending.cmd}: link closed before the ack`, { code: "CLOSED", seq, cmd: pending.cmd }));
    }
    this._pending.clear();
    if (queued) {
      const error = new DirecSunCommandError("manual: link closed before sending", { code: "CLOSED", cmd: "manual" });
      for (const waiter of queued.waiters) waiter.reject(error);
    }
    this._setState("closed", { reason });
  }
}
