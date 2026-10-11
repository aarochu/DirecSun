// After connecting to the mock, how long do Playwright queries on the page take?
import { createRequire } from 'node:module';
const require = createRequire(process.argv[2] + '/package.json');
const { chromium } = require('@playwright/test');
const [, , , base, box] = process.argv;
const t0 = Date.now(); const log = (m) => console.log(`${String(Date.now() - t0).padStart(6)} ms ${m}`);
const browser = await chromium.launch({ channel: 'chrome' });
const page = await browser.newPage();
page.setDefaultTimeout(20000);
const timed = async (label, fn) => { const t = Date.now(); try { const v = await fn(); log(`${label}: ${Date.now() - t} ms -> ${v}`); } catch (e) { log(`${label}: FAILED after ${Date.now() - t} ms: ${String(e.message).split('\n')[0]}`); } };
try {
  await page.goto(base);
  await timed('role Connect count (before connect)', () => page.getByRole('button', { name: 'Connect' }).count());
  await timed('role Arm count (before connect)', () => page.getByRole('button', { name: 'Arm the box' }).count());
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill(box);
  await page.getByRole('button', { name: 'Connect' }).click();
  for (let i = 0; i < 3; i++) await timed(`evaluate ${i}`, () => page.evaluate(() => Math.round(performance.now())));
  await timed('css [aria-label="Arm the box"] count', () => page.locator('[aria-label="Arm the box"]').count());
  await timed('role Arm count (after connect)', () => page.getByRole('button', { name: 'Arm the box' }).count());
  await timed('role Disconnect count (after connect)', () => page.getByRole('button', { name: 'Disconnect' }).count());
  await timed('DOM element count', () => page.evaluate(() => document.querySelectorAll('*').length));
} finally {
  await browser.close();
}
