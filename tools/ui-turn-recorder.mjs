#!/usr/bin/env node
// Drives the ground station (frontend/ground-station) through its own controls and
// records every WebSocket frame in both directions for tools/check-ui-capture.py.
//
//   node tools/ui-turn-recorder.mjs --scenario turn|stop|estop|watch|hidden
//        --url ws://127.0.0.1:8765/ws --base http://127.0.0.1:4173 --out <dir>
//        [--turns 4] [--hidden-s 30] [--ui-dir frontend/ground-station]
//        [--chrome <path to Google Chrome>] [--confirm-arm julian-go]
//
// The UI must already be served at --base (npm run build, then npx vite preview, in
// frontend/ground-station). The box is whatever --url names.
//
// THIS ARMS THE MOTOR when --url is a real box. The recorder sends arm only to a loopback
// address unless --confirm-arm julian-go is given, and Julian's go is needed every time.
//
// Writes frames.jsonl, timeline.jsonl, summary.json and screenshots into --out.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const argv = process.argv.slice(2);
const arg = (name, fallback) => {
  const i = argv.indexOf(`--${name}`);
  return i >= 0 && i + 1 < argv.length ? argv[i + 1] : fallback;
};
const scenario = arg('scenario');
const boxUrl = arg('url');
const base = arg('base', 'http://127.0.0.1:4173');
const out = arg('out');
const turns = Number(arg('turns', 4));
const hiddenS = Number(arg('hidden-s', 30));
const uiDir = path.resolve(arg('ui-dir', 'frontend/ground-station'));
const confirm = arg('confirm-arm');
const chromePath = arg(
  'chrome',
  process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'google-chrome',
);

const SCENARIOS = ['turn', 'stop', 'estop', 'watch', 'hidden'];
if (!SCENARIOS.includes(scenario) || !boxUrl || !out) {
  console.error('usage: ui-turn-recorder.mjs --scenario turn|stop|estop|watch|hidden --url ws://host/ws --out dir [--base http://127.0.0.1:4173]');
  process.exit(2);
}

const host = new URL(boxUrl).hostname.replace(/^\[|\]$/g, '');
const loopback = host === 'localhost' || host === '::1' || /^127\./.test(host);
if (!loopback && confirm !== 'julian-go') {
  console.error(`refusing: ${boxUrl} is not a loopback address, and this scenario arms the motor.`);
  console.error('Run it against tools/mock-box.py, or get Julian\'s explicit go and pass --confirm-arm julian-go.');
  process.exit(3);
}

const PLAN = {
  turn: `arm, then ${turns} turns alternating right and left (each: zero_heading, SLEW +-90, 1 s hold, SLEW 0)`,
  stop: 'arm, start a right turn, press Stop turn mid-slew',
  estop: 'arm, press the space bar to e-stop, then try to re-arm 1 s later',
  watch: 'one page arms; a second page only watches for 3 s, then presses the space bar (e-stop)',
  hidden: `arm, hide the operator tab behind another tab for ${hiddenS} s in plain Chrome, bring it back`,
};
console.log(`scenario ${scenario}: ${PLAN[scenario]}`);
console.log(`box ${boxUrl} (${loopback ? 'loopback mock' : 'REAL BOX, arming confirmed with --confirm-arm'}), UI ${base}, output ${out}`);

fs.mkdirSync(out, { recursive: true });
const require = createRequire(path.join(uiDir, 'package.json'));
let chromium;
try {
  ({ chromium } = require('@playwright/test'));
} catch {
  console.error(`Playwright is not installed under ${uiDir}. Run npm ci there, or pass --ui-dir.`);
  process.exit(2);
}

const t0 = Date.now();
const now = () => Date.now() - t0;
const framesFile = fs.createWriteStream(path.join(out, 'frames.jsonl'));
const timelineFile = fs.createWriteStream(path.join(out, 'timeline.jsonl'));
const drains = new Map();
const record = (entry) => framesFile.write(JSON.stringify(entry) + '\n');
const summary = { scenario, url: boxUrl, startedAt: new Date(t0).toISOString(), screenshots: [], notes: [] };
const log = (text) => console.log(`${(now() / 1000).toFixed(1).padStart(6)} s  ${text}`);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function makeDrain(page, name) {
  return async () => {
    const frames = await page.evaluate(() => window.__frames.splice(0)).catch(() => []);
    for (const f of frames) {
      if (f.dir === 'meta') {
        record({ t: f.epoch - t0, page: name, dir: 'meta', what: f.what, state: f.state });
        log(`${name}: visibilityState ${f.state}`);
      } else {
        record({ t: f.epoch - t0, page: name, dir: f.dir, msg: f.msg });
      }
    }
  };
}

async function instrument(page, name) {
  page.on('websocket', (socket) => {
    if (socket.url() === boxUrl) log(`${name}: WebSocket opened`);
  });
  await page.addInitScript((url) => {
    const Native = window.WebSocket;
    window.__frames = [];
    const stamp = () => performance.timeOrigin + performance.now();
    const push = (dir, data) => {
      let msg;
      try {
        msg = JSON.parse(String(data));
      } catch {
        msg = { type: '(not JSON)' };
      }
      window.__frames.push({ epoch: stamp(), dir, msg });
    };
    window.WebSocket = class extends Native {
      constructor(target, protocols) {
        super(target, protocols);
        if (String(target) === url) this.addEventListener('message', (event) => push('rx', event.data));
        this.__recorded = String(target) === url;
      }
      send(data) {
        if (this.__recorded) push('tx', data);
        super.send(data);
      }
    };
    const report = () => window.__frames.push({ epoch: stamp(), dir: 'meta', what: 'visibility', state: document.visibilityState });
    document.addEventListener('visibilitychange', report);
    window.addEventListener('load', report);
  }, boxUrl);
  drains.set(name, makeDrain(page, name));
}

function sampleTimeline(page, name) {
  let last = '';
  const timer = setInterval(async () => {
    await drains.get(name)();
    try {
      const sample = await page.evaluate(() => ({
        status: document.querySelector('.controls__status')?.textContent ?? '',
        result: document.querySelector('.controls__result')?.textContent ?? '',
        link: document.querySelector('.link__status')?.textContent ?? '',
      }));
      const key = JSON.stringify(sample);
      if (key !== last) {
        last = key;
        timelineFile.write(JSON.stringify({ t: now(), page: name, ...sample }) + '\n');
      }
    } catch {}
  }, 250);
  return async () => {
    clearInterval(timer);
    await drains.get(name)();
  };
}

async function shot(page, name) {
  const file = path.join(out, `${String(summary.screenshots.length + 1).padStart(2, '0')}-${name}.png`);
  await page.screenshot({ path: file }).catch(() => {});
  summary.screenshots.push(file);
}

const status = (page) => page.locator('.controls__status');
const result = (page) => page.locator('.controls__result');

async function waitUntil(page, locator, predicate, timeout, what) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const text = (await locator.textContent().catch(() => '')) ?? '';
    if (predicate(text)) return text;
    await sleep(100);
  }
  throw new Error(`timed out after ${timeout} ms waiting for ${what}`);
}

async function connect(page) {
  await page.goto(base);
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill(boxUrl);
  await page.getByRole('button', { name: 'Connect' }).click();
  await page.getByText('DEVICE CONNECTED — RECEIVING TELEMETRY').waitFor({ timeout: 10000 });
  log('connected');
}

async function arm(page) {
  await page.getByRole('button', { name: 'Arm the box' }).click();
  await page.getByRole('checkbox').check();
  await page.getByRole('button', { name: 'Arm now' }).click();
}

async function armToReady(page) {
  await arm(page);
  const deadline = Date.now() + 30000;
  while ((await status(page).textContent()) !== 'READY / DEVICE') {
    const text = (await result(page).textContent().catch(() => '')) ?? '';
    if (/ARM REFUSED/i.test(text)) throw new Error(`the box refused to arm: ${text}`);
    if (Date.now() > deadline) throw new Error('timed out after 30000 ms waiting for READY after arm');
    await sleep(100);
  }
  log('armed, READY');
}

async function turn(page, side, index) {
  const name = `Rotate ${side} ninety degrees and return`;
  await page.getByRole('button', { name }).click();
  const text = await waitUntil(page, result(page), (t) => /TURN COMPLETE|ABORT|FAIL|REFUSED|TIMED OUT/i.test(t), 30000, `turn ${index + 1} result`);
  log(`turn ${index + 1} (${side}): ${text}`);
  summary.notes.push(`turn ${index + 1} ${side}: ${text}`);
  await sleep(500);
}

async function pressSpace(page) {
  await page.getByRole('button', { name: 'Disconnect' }).focus();
  await page.keyboard.press('Space');
}

async function runOperator(page) {
  await connect(page);
  await shot(page, 'connected');
  await armToReady(page);
  await shot(page, 'ready');

  if (scenario === 'turn') {
    for (let i = 0; i < turns; i += 1) await turn(page, i % 2 === 0 ? 'right' : 'left', i);
    await shot(page, 'after-turns');
  } else if (scenario === 'stop') {
    await page.getByRole('button', { name: 'Rotate right ninety degrees and return' }).click();
    await waitUntil(page, status(page), (t) => t.includes('TURNING'), 10000, 'TURNING');
    await sleep(700);
    await page.getByRole('button', { name: 'Stop the turn' }).click();
    await waitUntil(page, status(page), (t) => t === 'READY / DEVICE', 10000, 'READY after Stop turn');
    summary.notes.push(`stop: ${await result(page).textContent()}`);
    log(`stop: ${summary.notes.at(-1)}`);
    await shot(page, 'after-stop');
  } else if (scenario === 'estop') {
    await pressSpace(page);
    await waitUntil(page, status(page), (t) => t === 'SAFE / DEVICE', 10000, 'SAFE after e-stop');
    await shot(page, 'after-estop');
    await sleep(1000);
    await arm(page);
    await sleep(1000);
    const text = (await result(page).textContent()) ?? '';
    summary.notes.push(`re-arm 1 s after the e-stop: ${text}`);
    log(`re-arm 1 s after the e-stop: ${text}`);
    summary.rearmRefusedRampDown = /ramp-down in progress/i.test(text);
    await shot(page, 'rearm');
  }
}

async function runWatch(browser) {
  const operator = await (await browser.newContext()).newPage();
  const watcher = await (await browser.newContext()).newPage();
  await instrument(operator, 'operator');
  await instrument(watcher, 'watcher');
  const stops = [sampleTimeline(operator, 'operator'), sampleTimeline(watcher, 'watcher')];
  await connect(operator);
  await armToReady(operator);
  await connect(watcher);
  log('watcher connected, only watching');
  await sleep(3000);
  await shot(watcher, 'watching');
  await pressSpace(watcher);
  await waitUntil(watcher, status(watcher), (t) => t === 'SAFE / DEVICE', 10000, 'SAFE on the watcher after its e-stop');
  await waitUntil(operator, status(operator), (t) => t === 'SAFE / DEVICE', 10000, 'SAFE on the operator after the watcher\'s e-stop');
  await shot(watcher, 'after-estop');
  for (const stop of stops) await stop();
}

async function launchPlainChrome() {
  const port = 9222 + Math.floor(Math.random() * 500);
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'ui-turn-recorder-'));
  const child = spawn(chromePath, [`--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check', 'about:blank'], { stdio: 'ignore' });
  const endpoint = `http://127.0.0.1:${port}`;
  for (let i = 0; i < 100; i += 1) {
    try {
      await fetch(`${endpoint}/json/version`);
      return { child, endpoint, profile };
    } catch {
      await sleep(100);
    }
  }
  child.kill();
  throw new Error('plain Chrome did not open its debugging port');
}

async function runHidden() {
  const { child, endpoint, profile } = await launchPlainChrome();
  try {
    let browser = await chromium.connectOverCDP(endpoint);
    const context = browser.contexts()[0];
    const page = context.pages()[0] ?? (await context.newPage());
    await instrument(page, 'operator');
    let stop = sampleTimeline(page, 'operator');
    await connect(page);
    await armToReady(page);
    await stop();
    await browser.close().catch(() => {});

    // Playwright's CDP session keeps its pages "focused", so a tab hidden while it is attached
    // still reports visible. Detach, hide the tab, and reattach after it is back in front.
    const targets = await (await fetch(`${endpoint}/json/list`)).json();
    const target = targets.find((t) => t.type === 'page' && t.url.startsWith(base));
    if (!target) throw new Error('could not find the operator tab in /json/list');
    await fetch(`${endpoint}/json/new?about:blank`, { method: 'PUT' });
    log(`Playwright detached; operator tab ${target.id} hidden behind a new tab for ${hiddenS} s`);
    await sleep(hiddenS * 1000);
    await fetch(`${endpoint}/json/activate/${target.id}`);
    await sleep(2500);

    browser = await chromium.connectOverCDP(endpoint);
    const back = browser.contexts()[0].pages().find((p) => p.url().startsWith(base));
    if (!back) throw new Error('the operator tab is gone after the hidden period');
    drains.set('operator', makeDrain(back, 'operator'));
    await drains.get('operator')();
    const mode = await status(back).textContent();
    summary.statusAfterReturn = mode;
    log(`operator tab back in front: ${mode}`);
    await shot(back, 'after-return');
    stop = sampleTimeline(back, 'operator');
    await sleep(500);
    await stop();
    await browser.close().catch(() => {});
  } finally {
    const exited = new Promise((resolve) => child.once('exit', resolve));
    child.kill();
    await Promise.race([exited, sleep(5000)]);
    try {
      fs.rmSync(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    } catch {}
  }
}

try {
  if (scenario === 'hidden') {
    await runHidden();
  } else {
    const browser = await chromium.launch({ channel: 'chrome' });
    if (scenario === 'watch') {
      await runWatch(browser);
    } else {
      const page = await browser.newPage();
      await instrument(page, 'operator');
      const stop = sampleTimeline(page, 'operator');
      await runOperator(page);
      await stop();
    }
    await browser.close();
  }
  summary.ok = true;
} catch (error) {
  summary.ok = false;
  summary.error = String(error?.message ?? error);
  log(`FAILED: ${summary.error}`);
}

summary.endedAt = new Date().toISOString();
fs.writeFileSync(path.join(out, 'summary.json'), JSON.stringify(summary, null, 2) + '\n');
framesFile.end();
timelineFile.end();
log(summary.ok ? `done, frames in ${path.join(out, 'frames.jsonl')}` : 'stopped early, the capture so far is saved');
process.exit(summary.ok ? 0 : 1);
