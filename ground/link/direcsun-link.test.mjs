// Tests for direcsun-link.js. Run with `node --test ground/link` (Node 20+).
// A fake WebSocket and a fake clock make every timing rule deterministic.
// With DIRECSUN_LINK_CAPTURE=<file>, every message the link sends is appended to
// that file as JSON lines; tools/check-link.sh validates it against the schema.
import { appendFileSync } from "node:fs";
import assert from "node:assert/strict";
import { test } from "node:test";

import { DEFAULT_URL, DirecSunCommandError, DirecSunLink } from "./direcsun-link.js";

class FakeClock {
  constructor() {
    this.t = 1000;
    this.timers = new Map();
    this.nextId = 1;
  }
  now() {
    return this.t;
  }
  setTimeout(fn, ms) {
    const id = this.nextId++;
    this.timers.set(id, { fn, at: this.t + ms, every: null });
    return id;
  }
  setInterval(fn, ms) {
    const id = this.nextId++;
    this.timers.set(id, { fn, at: this.t + ms, every: ms });
    return id;
  }
  clearTimeout(id) {
    this.timers.delete(id);
  }
  clearInterval(id) {
    this.timers.delete(id);
  }
  // Runs every timer due within the next ms, in time order.
  advance(ms) {
    const end = this.t + ms;
    for (;;) {
      let nextId = null;
      let next = null;
      for (const [id, timer] of this.timers) {
        if (timer.at <= end && (next === null || timer.at < next.at)) {
          nextId = id;
          next = timer;
        }
      }
      if (next === null) break;
      this.t = next.at;
      if (next.every === null) this.timers.delete(nextId);
      else next.at += next.every;
      next.fn();
    }
    this.t = end;
  }
}

class FakeSocket {
  static instances = [];
  constructor(url) {
    this.url = url;
    this.readyState = 0;
    this.sent = [];
    FakeSocket.instances.push(this);
  }
  send(text) {
    if (this.readyState !== 1) throw new Error("send on a socket that is not open");
    this.sent.push(JSON.parse(text));
  }
  close() {
    this.readyState = 3;
  }
  // test helpers acting as the box
  open() {
    this.readyState = 1;
    this.onopen?.();
  }
  deliver(message) {
    this.onmessage?.({ data: typeof message === "string" ? message : JSON.stringify(message) });
  }
  dropFromPeer(reason = "peer went away") {
    this.readyState = 3;
    this.onclose?.({ code: 1006, reason });
  }
  sentOfType(type) {
    return this.sent.filter((m) => m.type === type);
  }
}

const capturePath = process.env.DIRECSUN_LINK_CAPTURE;

function makeLink(options = {}) {
  FakeSocket.instances.length = 0;
  const clock = new FakeClock();
  const link = new DirecSunLink("ws://box.test/ws", { WebSocketImpl: FakeSocket, clock, ...options });
  if (capturePath) link.on("sent", (message) => appendFileSync(capturePath, JSON.stringify(message) + "\n"));
  return { link, clock };
}

async function openLink(options) {
  const { link, clock } = makeLink(options);
  const opened = link.connect();
  const socket = FakeSocket.instances[0];
  socket.open();
  await opened;
  return { link, clock, socket };
}

const ack = (seq, ok = true, extra = {}) => ({ v: 1, type: "ack", seq, ok, ...extra });

// Lets every queued promise reaction run (a macrotask boundary drains the microtask queue).
const flushMicrotasks = () => new Promise((resolve) => setImmediate(resolve));

test("default endpoint is the main UI's endpoint: mDNS name, port 80, path /ws", () => {
  assert.equal(DEFAULT_URL, "ws://direcsun.local/ws");
  const url = new URL(DEFAULT_URL);
  assert.equal(url.protocol, "ws:");
  assert.equal(url.port, "", "no explicit port means port 80 for ws:");
  assert.equal(url.pathname, "/ws");
});

test("connect moves idle, connecting, open and reports each state", async () => {
  const { link } = makeLink();
  const states = [];
  link.on("state", (s) => states.push(s.state));
  assert.equal(link.state, "idle");
  const opened = link.connect();
  assert.equal(link.state, "connecting");
  FakeSocket.instances[0].open();
  await opened;
  assert.equal(link.state, "open");
  assert.deepEqual(states, ["connecting", "open"]);
});

test("command resolves on the ack with its seq", async () => {
  const { link, socket } = await openLink();
  const pending = link.command("arm");
  const sent = socket.sentOfType("cmd").at(-1);
  assert.deepEqual({ v: sent.v, type: sent.type, cmd: sent.cmd }, { v: 1, type: "cmd", cmd: "arm" });
  socket.deliver(ack(sent.seq));
  const result = await pending;
  assert.equal(result.seq, sent.seq);
  assert.equal(result.ok, true);
});

test("acks arriving out of order resolve the matching commands", async () => {
  const { link, socket } = await openLink();
  const first = link.command("mode", { mode: "READY" });
  const second = link.command("zero_heading");
  const [cmdA, cmdB] = socket.sentOfType("cmd");
  assert.notEqual(cmdA.seq, cmdB.seq);
  socket.deliver(ack(cmdB.seq, false, { reason: "not allowed in SAFE" }));
  socket.deliver(ack(cmdA.seq, true));
  assert.equal((await first).seq, cmdA.seq);
  await assert.rejects(second, (error) => {
    assert.ok(error instanceof DirecSunCommandError);
    assert.equal(error.code, "REFUSED");
    assert.equal(error.seq, cmdB.seq);
    assert.match(error.message, /not allowed in SAFE/);
    return true;
  });
});

test("a command without an ack rejects with TIMEOUT after 1 s, and a late ack is ignored", async () => {
  const { link, clock, socket } = await openLink();
  const pending = link.command("arm");
  const { seq } = socket.sentOfType("cmd").at(-1);
  let settled = false;
  pending.then(() => (settled = true), () => (settled = true));
  clock.advance(999);
  await flushMicrotasks();
  assert.equal(settled, false, "must still be pending at 999 ms");
  clock.advance(1);
  await assert.rejects(pending, (error) => error.code === "TIMEOUT" && error.seq === seq);
  socket.deliver(ack(seq));
});

test("pings go out at 5 Hz and pongs update the round-trip time", async () => {
  const { link, clock, socket } = await openLink();
  const before = socket.sentOfType("ping").length;
  clock.advance(10_000);
  const pings = socket.sentOfType("ping");
  const count = pings.length - before;
  assert.ok(count >= 49 && count <= 51, `expected about 50 pings in 10 s, got ${count}`);
  for (const ping of pings) assert.equal(typeof ping.t_client_ms, "number");
  const last = pings.at(-1);
  clock.advance(37);
  socket.deliver({ v: 1, type: "pong", seq: 900, ping_seq: last.seq, t_client_ms: last.t_client_ms, t_ms: 5000 });
  assert.equal(link.rtt.count, 1);
  assert.equal(link.rtt.last, 37);
});

test("estop is sent at once while open and throws when the link is not open", async () => {
  const { link: closedLink } = makeLink();
  assert.throws(() => closedLink.estop(), /not open/);

  const { link, socket } = await openLink();
  link.manual({ rate_dps: 10 });
  link.manual({ rate_dps: 20 }); // queued behind the rate limit
  const stop = link.estop();
  const last = socket.sent.at(-1);
  assert.equal(last.cmd, "estop", "estop must not wait behind queued manual setpoints");
  socket.deliver(ack(last.seq));
  assert.equal((await stop).ok, true);
});

test("the link never reconnects after the peer closes, and pending commands reject", async () => {
  const { link, clock, socket } = await openLink();
  const pending = link.command("arm");
  socket.dropFromPeer();
  await assert.rejects(pending, (error) => error.code === "CLOSED");
  assert.equal(link.state, "closed");
  const pingsAtClose = socket.sentOfType("ping").length;
  clock.advance(30_000);
  assert.equal(FakeSocket.instances.length, 1, "no new socket may be created without connect()");
  assert.equal(socket.sentOfType("ping").length, pingsAtClose, "pings must stop after close");
  assert.equal(clock.timers.size, 0, "a closed link must leave no live timers");
  assert.throws(() => link.estop(), /not open/);
});

test("an explicit connect after a close opens a new socket", async () => {
  const { link, socket } = await openLink();
  link.close();
  assert.equal(socket.readyState, 3);
  const again = link.connect();
  assert.equal(FakeSocket.instances.length, 2);
  FakeSocket.instances[1].open();
  await again;
  assert.equal(link.state, "open");
});

test("tlm and event are dispatched; wrong v, bad JSON and unknown types surface as raw", async () => {
  const { link, socket } = await openLink();
  const tlm = [];
  const events = [];
  const raw = [];
  link.on("tlm", (m) => tlm.push(m));
  link.on("event", (m) => events.push(m));
  link.on("raw", (r) => raw.push(r));
  socket.deliver({ v: 1, type: "tlm", seq: 1, mode: "SAFE", psi_deg: 10 });
  socket.deliver({ v: 1, type: "event", seq: 2, t_ms: 5, event: "mode", mode: "SAFE", cause: "ESTOP" });
  socket.deliver({ v: 2, type: "tlm", seq: 3 });
  socket.deliver("ACK from DirecSun");
  socket.deliver({ v: 1, type: "mystery", seq: 4 });
  assert.equal(tlm.length, 1);
  assert.equal(tlm[0].psi_deg, 10, "wire values pass through unchanged (clockwise positive on the wire)");
  assert.equal(events.length, 1);
  const rejected = raw.filter((r) => !r.accepted).map((r) => r.reason);
  assert.equal(rejected.length, 3);
  assert.match(rejected[0], /unsupported v/);
  assert.match(rejected[1], /not JSON/);
  assert.match(rejected[2], /unknown type/);
});

test("manual setpoints are spaced at least 50 ms apart and the latest value wins", async () => {
  const { link, clock, socket } = await openLink();
  const results = [];
  for (let i = 0; i < 10; i += 1) {
    results.push(link.manual({ throttle: 300 + i }));
    clock.advance(10);
  }
  clock.advance(100);
  const manual = socket.sentOfType("cmd").filter((m) => m.cmd === "manual");
  assert.ok(manual.length >= 2 && manual.length <= 3, `expected 2 or 3 frames for 100 ms of input, got ${manual.length}`);
  assert.equal(manual.at(-1).throttle, 309, "the last value must be the one that goes out");
  const sentTimes = [];
  // Re-run with timestamps to check spacing directly.
  const timed = await openLink();
  timed.link.on("sent", (m) => m.cmd === "manual" && sentTimes.push(timed.clock.now()));
  for (let i = 0; i < 20; i += 1) {
    timed.link.manual({ wheel_rpm: 2400 + i }).catch(() => {});
    timed.clock.advance(7);
  }
  timed.clock.advance(200);
  for (let i = 1; i < sentTimes.length; i += 1) {
    assert.ok(sentTimes[i] - sentTimes[i - 1] >= 50, `frames ${i - 1} and ${i} only ${sentTimes[i] - sentTimes[i - 1]} ms apart`);
  }
  for (const m of manual) socket.deliver(ack(m.seq));
  const acks = await Promise.all(results);
  assert.ok(acks.every((a) => a.ok));
});

test("manual rejects malformed setpoints without sending", async () => {
  const { link, socket } = await openLink();
  const before = socket.sent.length;
  await assert.rejects(link.manual({ throttle: 400, wheel_rpm: 2000 }), /exactly one/);
  await assert.rejects(link.manual({}), /exactly one/);
  await assert.rejects(link.manual({ speed: 3 }), /exactly one/);
  assert.equal(socket.sent.length, before);
});

test("commands cover every v1 cmd kind with the documented fields", async () => {
  const { link, socket } = await openLink();
  const calls = [
    ["arm", {}],
    ["disarm", {}],
    ["zero_heading", {}],
    ["mode", { mode: "READY" }],
    ["mode", { mode: "SLEW", psi_ref_deg: 137 }],
    ["mode", { mode: "MANUAL", input: "rate" }],
    ["respin", { wheel_rpm: 1000 }],
    ["respin", {}],
    ["set_params", { params: { bench_mode: true, manual_throttle_min: 200, manual_throttle_max: 600 } }],
    ["wheel_step", { delta_rpm: -300 }],
  ];
  const pending = calls.map(([name, fields]) => link.command(name, fields).catch(() => {}));
  link.manual({ rate_dps: -12.5 }).catch(() => {});
  link.estop().catch(() => {});
  const cmds = socket.sentOfType("cmd");
  assert.equal(cmds.length, calls.length + 2);
  assert.deepEqual(new Set(cmds.map((m) => m.cmd)), new Set(["arm", "disarm", "zero_heading", "mode", "respin", "set_params", "wheel_step", "manual", "estop"]));
  for (const m of cmds) socket.deliver(ack(m.seq));
  await Promise.all(pending);
});
