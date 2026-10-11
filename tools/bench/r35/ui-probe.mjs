// Drives the main UI (frontend/ground-station) or the bench console in the
// installed Google Chrome through the UI's own Playwright, and writes probe.json.
//
//   node ui-probe.mjs --mode connect|turn|watch|console --url ws://direcsun.local/ws
//                     --base http://127.0.0.1:<port> --ui-dir <frontend/ground-station> --out <dir>
//                     [--watch-s 5] [--turn-s 15]
//
// Two independent views of what the page receives are recorded:
//   1. the UI's own Device link status line (packets, and "N IGNORED" once N > 0);
//   2. the raw WebSocket frames the page receives and sends, from Chrome itself.
// The heading is read twice as well: the HEADING readout text and the dial needle's
// SVG end point. Screenshots are saved for a human to judge the dial direction.
//
// Progress lines go to stdout; the Python wrappers react to "CONNECTED" and "TURN NOW".
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';

const args = Object.fromEntries(
  process.argv.slice(2).reduce((pairs, arg, i, all) => (arg.startsWith('--') ? [...pairs, [arg.slice(2), all[i + 1]]] : pairs), []),
);
const mode = args.mode;
const out = args.out;
const watchS = Number(args['watch-s'] ?? 5);
const turnS = Number(args['turn-s'] ?? 15);
fs.mkdirSync(out, { recursive: true });

const require = createRequire(path.join(args['ui-dir'], 'package.json'));
const { chromium } = require('@playwright/test');

const result = { ok: false, mode, url: args.url, startedAt: new Date().toISOString(), screenshots: [] };
const log = (text) => console.log(`${((Date.now() - t0) / 1000).toFixed(1).padStart(6)} s  ${text}`);
const write = () => fs.writeFileSync(path.join(out, 'probe.json'), JSON.stringify(result, null, 2) + '\n');
const t0 = Date.now();

const wsFrames = { received: {}, sent: 0, sentSamples: [], receivedNonTlmSamples: [] };
function watchSockets(page) {
  page.on('websocket', (socket) => {
    log(`page opened a WebSocket to ${socket.url()}`);
    socket.on('framereceived', ({ payload }) => {
      let type = '(not JSON)';
      try {
        const message = JSON.parse(String(payload));
        type = message.type === 'event' ? `event:${message.event}` : String(message.type);
      } catch {}
      wsFrames.received[type] = (wsFrames.received[type] ?? 0) + 1;
      if (type !== 'tlm' && wsFrames.receivedNonTlmSamples.length < 20) wsFrames.receivedNonTlmSamples.push(String(payload).slice(0, 300));
    });
    socket.on('framesent', ({ payload }) => {
      wsFrames.sent += 1;
      if (wsFrames.sentSamples.length < 20) wsFrames.sentSamples.push(String(payload).slice(0, 300));
    });
    socket.on('close', () => log('page WebSocket closed'));
  });
}

async function shot(page, name, locator = null) {
  const file = path.join(out, `${String(result.screenshots.length + 1).padStart(2, '0')}-${name}.png`);
  // A page screenshot clipped to the element: an element screenshot waits for the
  // element to stop changing, which never happens while telemetry moves the dial.
  const clip = locator ? await locator.boundingBox() : null;
  await page.screenshot(clip ? { path: file, clip } : { path: file });
  result.screenshots.push(file);
  return file;
}

const parseDeg = (text) => {
  const m = /([+−-]?)(\d+(?:\.\d+)?)°/.exec(text ?? '');
  if (!m) return null;
  const value = Number(m[2]);
  return m[1] === '−' || m[1] === '-' ? -value : value;
};
const wrap = (deg) => {
  let d = ((deg + 180) % 360 + 360) % 360 - 180;
  return d === -180 ? 180 : d;
};

async function sampleGround(page) {
  return page.evaluate(() => {
    const status = document.querySelector('.link__status')?.textContent ?? '';
    const header = document.querySelector('.gs-header__status')?.textContent ?? '';
    const phase = document.querySelector('.readout--phase dd')?.textContent ?? '';
    const controls = document.querySelector('.controls__status')?.textContent ?? '';
    let heading = null;
    for (const row of document.querySelectorAll('.rail .readout')) {
      if (row.querySelector('dt')?.textContent?.trim() === 'HEADING') heading = row.querySelector('dd')?.textContent ?? null;
    }
    const needle = document.querySelector('.gs-main svg.dial line.dial__needle');
    return {
      t: performance.now(),
      status,
      header,
      phase,
      controls,
      heading,
      needle: needle ? { x2: Number(needle.getAttribute('x2')), y2: Number(needle.getAttribute('y2')) } : null,
    };
  });
}

function decorate(sample) {
  const packets = /RECEIVING TELEMETRY · (\d+) PACKETS/.exec(sample.status);
  const ignored = / · (\d+) IGNORED/.exec(sample.status);
  return {
    ...sample,
    packets: packets ? Number(packets[1]) : null,
    ignored: ignored ? Number(ignored[1]) : 0,
    headingDeg: parseDeg(sample.heading),
    // Needle end point: x = r sin(h), y = -r cos(h), so the dial angle is atan2(x, -y), clockwise from up.
    needleDeg: sample.needle ? (Math.atan2(sample.needle.x2, -sample.needle.y2) * 180) / Math.PI : null,
  };
}

async function connectGround(page) {
  await page.goto(`${args.base}/ground-station`);
  await page.getByText('SIMULATED \u2014 NO DEVICE CONNECTION').waitFor({ timeout: 15000 });
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill(args.url);
  await shot(page, 'before-connect');
  await page.getByRole('button', { name: 'Connect' }).click();
  await page.getByText('DEVICE CONNECTED \u2014 RECEIVING TELEMETRY').waitFor({ timeout: 15000 });
  log('CONNECTED: the ground station shows DEVICE CONNECTED');
}

async function groundSession(page) {
  await connectGround(page);
  const samples = [];
  const take = async () => {
    const s = decorate(await sampleGround(page));
    samples.push(s);
    return s;
  };
  const first = await take();
  result.first = first;
  await shot(page, 'connected');
  await shot(page, 'connected-dial', page.locator('.gs-main svg.dial'));
  if (mode === 'turn') {
    log(`TURN NOW: turn the rig clockwise by hand, about 90 degrees, then hold it still (sampling ${turnS} s)`);
    const end = Date.now() + turnS * 1000;
    let nextShot = Date.now() + 2000;
    while (Date.now() < end) {
      const s = await take();
      if (Date.now() >= nextShot) {
        nextShot += 2000;
        await shot(page, `turn-dial-${(s.headingDeg ?? 0).toFixed(1)}deg`, page.locator('.gs-main svg.dial'));
      }
      await page.waitForTimeout(100);
    }
    await shot(page, 'after-turn');
    let heading = 0;
    let needle = 0;
    let minHeading = 0;
    let maxHeading = 0;
    for (let i = 1; i < samples.length; i += 1) {
      const [a, b] = [samples[i - 1], samples[i]];
      if (a.headingDeg !== null && b.headingDeg !== null) heading += wrap(b.headingDeg - a.headingDeg);
      if (a.needleDeg !== null && b.needleDeg !== null) needle += wrap(b.needleDeg - a.needleDeg);
      minHeading = Math.min(minHeading, heading);
      maxHeading = Math.max(maxHeading, heading);
    }
    result.turn = {
      samples: samples.length,
      headingStart: samples[0].heading,
      headingEnd: samples.at(-1).heading,
      headingChangeDeg: Number(heading.toFixed(2)),
      needleChangeDeg: Number(needle.toFixed(2)),
      headingMinDeg: Number(minHeading.toFixed(2)),
      headingMaxDeg: Number(maxHeading.toFixed(2)),
    };
    log(`turn: readout ${result.turn.headingStart} -> ${result.turn.headingEnd}, net ${result.turn.headingChangeDeg} deg; needle net ${result.turn.needleChangeDeg} deg (clockwise positive)`);
  } else {
    const end = Date.now() + watchS * 1000;
    while (Date.now() < end) {
      await take();
      await page.waitForTimeout(250);
    }
    await shot(page, 'end');
  }
  const last = samples.at(-1);
  result.last = last;
  result.samples = samples.length;
  result.ignoredMax = Math.max(...samples.map((s) => s.ignored));
  result.packetsLast = last.packets;
  result.modesSeen = [...new Set(samples.map((s) => s.controls.trim()))];
  result.headerAlwaysLive = samples.every((s) => s.header.includes('DEVICE CONNECTED \u2014 RECEIVING TELEMETRY'));
  result.statusAlwaysReceiving = samples.every((s) => s.packets !== null);
  fs.writeFileSync(path.join(out, 'samples.json'), JSON.stringify(samples, null, 1) + '\n');
}

async function consoleSession(page) {
  await page.goto(`${args.base}/ground/console/index.html`);
  await page.locator('#endpoint').fill(args.url);
  await page.locator('#connect').click();
  await page.locator('#state').getByText('CONNECTED', { exact: true }).waitFor({ timeout: 15000 });
  await page.locator('#m-seq').filter({ hasText: /TLM SEQ \d+/ }).waitFor({ timeout: 10000 });
  log('CONNECTED: the bench console shows CONNECTED and a tlm seq');
  await page.waitForTimeout(watchS * 1000);
  result.console = await page.evaluate(() => ({
    state: document.querySelector('#state')?.textContent ?? '',
    seq: document.querySelector('#m-seq')?.textContent ?? '',
    mode: document.querySelector('#m-mode')?.textContent ?? '',
    badge: document.querySelector('#badge-mode')?.textContent ?? '',
  }));
  await shot(page, 'console');
  log(`console: state ${result.console.state.trim()}, ${result.console.seq}, mode ${result.console.mode}`);
}

let browser;
try {
  // Render WebGL on the GPU: the ground station's CAD view, rendered in software
  // while telemetry streams, starved the page and timed out every probe call.
  browser = await chromium.launch({
    channel: 'chrome',
    headless: true,
    args: ['--use-angle=vulkan', '--enable-features=Vulkan', '--ignore-gpu-blocklist'],
  });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  const consoleErrors = [];
  page.on('console', (message) => {
    if (message.type() === 'error') consoleErrors.push(message.text().slice(0, 300));
  });
  watchSockets(page);
  if (mode === 'console') await consoleSession(page);
  else await groundSession(page);
  result.wsFrames = wsFrames;
  result.consoleErrors = consoleErrors;
  result.ok = true;
  await page.close();
} catch (error) {
  result.error = String(error?.message ?? error).split('\n').slice(0, 6).join(' | ');
  result.wsFrames = wsFrames;
  log(`ERROR: ${result.error}`);
} finally {
  await browser?.close();
  result.finishedAt = new Date().toISOString();
  write();
  log(`wrote ${path.join(out, 'probe.json')}`);
}
process.exit(result.ok ? 0 : 1);
