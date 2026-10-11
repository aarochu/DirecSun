#!/usr/bin/env node
// Drive the actual ground-station controls and capture both directions of its
// WebSocket traffic. No protocol command is constructed by this tool.
import { createRequire } from 'node:module';
import { createInterface } from 'node:readline/promises';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { createServer } from 'node:net';

function argumentsFor(argv) {
  const options = { scenario: 'turn', turns: '4', url: 'ws://127.0.0.1:8081/ws', base: 'http://127.0.0.1:4173' };
  for (let i = 0; i < argv.length; i += 2) {
    if (!argv[i]?.startsWith('--') || !argv[i + 1]) throw Error(`expected --name value, got ${argv[i] ?? '(end)'}`);
    options[argv[i].slice(2)] = argv[i + 1];
  }
  if (!['turn', 'stop', 'estop', 'watch', 'hidden'].includes(options.scenario)) throw Error('unknown scenario');
  if (!Number.isInteger(Number(options.turns)) || Number(options.turns) < 1) throw Error('--turns must be a positive integer');
  if (!options['ui-dir'] || !options.out) throw Error('--ui-dir and --out are required');
  return options;
}

const opts = argumentsFor(process.argv.slice(2));
const output = resolve(opts.out);
mkdirSync(output, { recursive: true });
const require = createRequire(join(resolve(opts['ui-dir']), 'package.json'));
const { chromium } = require('@playwright/test');
const realBox = !['localhost', '127.0.0.1', '::1'].includes(new URL(opts.url).hostname);
if (realBox && opts['confirm-arm'] !== 'julian-go') {
  console.error('BLOCKED: real-box runs require --confirm-arm julian-go and a fresh spoken go before each motor-turning action.');
  process.exit(2);
}

const frames = [];
const timeline = [];
const visibility = [];
const started = Date.now();
let browser;
let chrome;
let reader;
let operatorPage;
let watchPageTab;

const elapsed = () => Date.now() - started;
const write = (name, rows) => writeFileSync(join(output, name), rows.map((row) => JSON.stringify(row)).join('\n') + '\n');
function watchSocket(page, client) {
  page.on('websocket', (socket) => {
    socket.on('framesent', ({ payload }) => record('send', payload));
    socket.on('framereceived', ({ payload }) => record('recv', payload));
    function record(direction, payload) {
      let frame;
      try { frame = JSON.parse(String(payload)); } catch { frame = { malformed: String(payload) }; }
      frames.push({ at_ms: elapsed(), client, direction, frame });
    }
  });
}
async function watchPage(page, client) {
  await page.exposeFunction('recordVisibility', (state) => visibility.push({ at_ms: elapsed(), client, state }));
  await page.addInitScript(() => {
    document.addEventListener('visibilitychange', () => window.recordVisibility(document.visibilityState));
  });
  watchSocket(page, client);
  const timer = setInterval(async () => {
    try {
      const value = await page.evaluate(() => ({
        status: document.querySelector('.controls__status')?.textContent?.trim() ?? '',
        result: document.querySelector('.controls__result')?.textContent?.trim() ?? '',
      }));
      if (!timeline.length || JSON.stringify(timeline.at(-1).value) !== JSON.stringify(value)) {
        timeline.push({ at_ms: elapsed(), client, value });
      }
    } catch { /* page can close between samples */ }
  }, 100);
  return () => clearInterval(timer);
}
async function screenshot(page, name) {
  try {
    await page.screenshot({ path: join(output, `${String(elapsed()).padStart(7, '0')}-${name}.png`), timeout: 10000 });
  } catch (error) {
    console.warn(`screenshot ${name} unavailable: ${error.message.split('\n')[0]}`);
  }
}
async function connect(page) {
  await page.goto(`${opts.base}/ground-station`);
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill(opts.url);
  await page.getByRole('button', { name: 'Connect' }).click();
  await page.getByText('DEVICE CONNECTED — RECEIVING TELEMETRY').waitFor({ timeout: 15000 });
}
async function go(action) {
  if (!realBox) return;
  console.log(`This action can turn the motor: ${action}. Julian's go applies to this run only.`);
  reader ??= createInterface({ input: process.stdin, output: process.stdout });
  const answer = await reader.question('After Julian explicitly says go, type julian-go for THIS action: ');
  if (answer.trim() !== 'julian-go') throw Error('BLOCKED: no Julian go for this action');
}
async function arm(page) {
  await go('Arm; the box enters SPINUP and ramps the wheel to bias speed');
  await page.getByRole('button', { name: 'Arm the box' }).click();
  await page.getByRole('checkbox').check();
  await page.getByRole('button', { name: 'Arm now' }).click();
  await page.locator('.controls__status').getByText('READY / DEVICE').waitFor({ timeout: 15000 });
  await screenshot(page, 'ready');
}
async function rotate(page, direction) {
  await go(`Rotate ${direction} 90 degrees and return; the UI sends zero_heading, SLEW, waits one second, then SLEW 0`);
  await page.getByRole('button', { name: `Rotate ${direction} ninety degrees and return` }).click();
}
async function finishTurn(page, label) {
  await page.locator('.controls__result').getByText('TURN COMPLETE: BACK AT THE START HEADING.').waitFor({ timeout: 18000 });
  await screenshot(page, label);
}
async function drainPongs(client) {
  for (let i = 0; i < 100; i++) {
    const pings = frames.filter((row) => row.client === client && row.direction === 'send' && row.frame.type === 'ping');
    const pongs = new Set(frames.filter((row) => row.client === client && row.direction === 'recv' && row.frame.type === 'pong')
      .map((row) => row.frame.ping_seq));
    if (pings.every((row) => pongs.has(row.frame.seq))) return;
    await delay(10);
  }
}
async function safeEnd(page, client) {
  if (!page || page.isClosed()) return;
  try {
    const status = await page.locator('.controls__status').textContent({ timeout: 1000 });
    if (status?.includes('/ DEVICE') && !status.includes('SAFE')) {
      console.log('Ending the run with UI E-stop; the wheel ramps down.');
      await page.getByRole('button', { name: 'Emergency stop the box' }).click({ timeout: 3000 });
      await page.locator('.controls__status').getByText('SAFE / DEVICE').waitFor({ timeout: 5000 });
    }
    await drainPongs(client);
    await page.getByRole('button', { name: 'Disconnect' }).click({ timeout: 3000 });
  } catch (error) {
    console.error(`Safety cleanup could not be confirmed for ${client}: ${error.message.split('\n')[0]}`);
    process.exitCode = 1;
  }
}
async function freePort() {
  return new Promise((accept, reject) => {
    const server = createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const port = server.address().port;
      server.close(() => accept(port));
    });
  });
}
async function plainChrome() {
  const bin = process.env.CHROME_BIN ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
  if (!existsSync(bin)) throw Error(`plain Chrome not found at ${bin}; set CHROME_BIN`);
  const port = await freePort();
  const profile = join(tmpdir(), `direcsun-ui-recorder-${process.pid}`);
  chrome = spawn(bin, [`--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', 'about:blank'], { stdio: 'ignore' });
  for (let i = 0; i < 40; i++) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/json/version`);
      if (response.ok) return { browser: await chromium.connectOverCDP(`http://127.0.0.1:${port}`), port };
    } catch { /* Chrome has not opened its debugging port yet */ }
    await delay(250);
  }
  throw Error('plain Chrome did not start its debugging port');
}

let stopTimers = [];
try {
  let cdpPort;
  if (opts.scenario === 'hidden') {
    ({ browser, port: cdpPort } = await plainChrome());
  } else {
    browser = await chromium.launch({ channel: 'chrome', headless: true,
      // The recorder tests controls and wire traffic, not 3D rendering. The
      // schematic fallback avoids WebGL starving the UI on headless laptops.
      args: ['--disable-webgl'] });
  }
  const context = opts.scenario === 'hidden' ? browser.contexts()[0] : await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  const operator = await context.newPage();
  operatorPage = operator;
  stopTimers.push(await watchPage(operator, 'operator'));
  await connect(operator);
  await screenshot(operator, 'connected');
  if (opts.scenario === 'watch') {
    const watcher = await context.newPage();
    watchPageTab = watcher;
    stopTimers.push(await watchPage(watcher, 'watch'));
    await connect(watcher);
    await arm(operator);
    await delay(1000);
    await screenshot(watcher, 'watch-only');
    await watcher.getByRole('button', { name: 'Emergency stop the box' }).click();
    await screenshot(watcher, 'watch-estop');
  } else {
    await arm(operator);
    if (opts.scenario === 'turn') {
      for (let i = 0; i < Number(opts.turns); i++) {
        await rotate(operator, i % 2 ? 'left' : 'right');
        await finishTurn(operator, `turn-${i + 1}`);
      }
    } else if (opts.scenario === 'stop') {
      await rotate(operator, 'right');
      await operator.locator('.controls__status').getByText(/TURNING/).waitFor();
      await operator.getByRole('button', { name: 'Stop the turn' }).click();
      await screenshot(operator, 'stopped');
    } else if (opts.scenario === 'estop') {
      await operator.getByRole('button', { name: 'Disconnect' }).focus();
      await operator.keyboard.press('Space');
      await screenshot(operator, 'estop');
    } else if (opts.scenario === 'hidden') {
      const response = await fetch(`http://127.0.0.1:${cdpPort}/json/new?about:blank`, { method: 'PUT' });
      if (!response.ok) throw Error('could not open the occluding tab');
      for (let i = 0; i < 50 && !visibility.some((entry) => entry.client === 'operator' && entry.state === 'hidden'); i++) {
        await delay(100);
      }
      if (!visibility.some((entry) => entry.client === 'operator' && entry.state === 'hidden')) {
        throw Error('instrument check failed: no visibilitychange to hidden');
      }
      await delay(30000);
      const pages = await fetch(`http://127.0.0.1:${cdpPort}/json`).then((res) => res.json());
      const target = pages.find((item) => item.url.startsWith(opts.base));
      await fetch(`http://127.0.0.1:${cdpPort}/json/activate/${target.id}`);
      await screenshot(operator, 'after-hidden');
    }
  }
  console.log(`capture complete: ${opts.scenario}`);
} catch (error) {
  console.error(`FAIL: ${error.message}`);
  process.exitCode = 1;
} finally {
  await safeEnd(operatorPage, 'operator');
  await safeEnd(watchPageTab, 'watch');
  stopTimers.forEach((stop) => stop());
  reader?.close();
  write('frames.jsonl', frames);
  write('timeline.jsonl', timeline);
  write('visibility.jsonl', visibility);
  await browser?.close();
  chrome?.kill();
  console.log(`captures written to ${output}`);
}
