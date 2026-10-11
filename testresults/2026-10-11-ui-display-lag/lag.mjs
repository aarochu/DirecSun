// Main-thread lag of the ground station after connecting to a box: one evaluate every 2 s for 30 s.
// usage: node lag.mjs <ui-dir> <base> <box ws url> <headless|headed>
import { createRequire } from 'node:module';
const require = createRequire(process.argv[2] + '/package.json');
const { chromium } = require('@playwright/test');
const [, , , base, box, mode] = process.argv;
const t0 = Date.now(); const log = (m) => console.log(`${String(Date.now() - t0).padStart(6)} ms ${m}`);
const browser = await chromium.launch({ channel: 'chrome', headless: mode !== 'headed' });
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
page.setDefaultTimeout(60000);
try {
  await page.goto(base);
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill(box);
  await page.getByRole('button', { name: 'Connect' }).click();
  const tc = Date.now();
  while (Date.now() - tc < 30000) {
    const t = Date.now();
    const pkts = await page.evaluate(() => document.body.innerText.match(/(\d+) PACKETS/)?.[1] ?? null);
    log(`evaluate took ${Date.now() - t} ms, packets shown ${pkts}`);
    await new Promise((r) => setTimeout(r, 2000));
  }
} finally {
  await browser.close();
}
