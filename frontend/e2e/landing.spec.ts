import { chromium, expect, test, type Page } from '@playwright/test';

const WIDTHS = [320, 375, 768, 1024, 1440];

async function open(page: Page, width = 1440, height = 900) {
  const errors: string[] = [];
  page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
  page.on('pageerror', (error) => errors.push(String(error)));
  await page.setViewportSize({ width, height });
  await page.goto('/');
  await expect(page.locator('.loader')).toHaveCount(0, { timeout: 30_000 });
  return errors;
}

async function settle(page: Page) {
  let last = -1;
  await expect
    .poll(async () => {
      const y = await page.evaluate(() => Math.round(window.scrollY));
      const stable = y === last;
      last = y;
      return stable;
    }, { intervals: [350, 350, 350], timeout: 15_000 })
    .toBe(true);
  await page.waitForTimeout(1600);
}

async function goTo(page: Page, name: string) {
  const link = page.getByRole('navigation', { name: /primary|sections|main/i }).getByRole('link', { name }).first();
  if (await link.isVisible().catch(() => false)) await link.click();
  else await page.locator(`[data-nav="${name.toLowerCase()}"]`).first().evaluate((element: HTMLElement) => element.click());
  await settle(page);
}

test.describe('landing page', () => {
  test('loads cleanly: no console errors, the loader clears, and the model is drawn', async ({ page }) => {
    const errors = await open(page);
    expect(errors).toEqual([]);
    expect(await page.evaluate(() => document.documentElement.dataset.webgl)).not.toBe('false');
    await page.waitForTimeout(1500);
    const drawn = await page.screenshot({ timeout: 20_000 });
    await page.locator('.stage-canvas').evaluate((element) => ((element as HTMLElement).style.visibility = 'hidden'));
    const blank = await page.screenshot({ timeout: 20_000 });
    expect(drawn.equals(blank)).toBe(false);
  });

  test('OFFLINE: every request stays on this origin, and the fonts load from it', async ({ page, baseURL }) => {
    const origin = new URL(baseURL!).origin;
    const external: string[] = [];
    page.on('request', (request) => {
      if (new URL(request.url()).origin !== origin && !request.url().startsWith('data:')) external.push(request.url());
    });
    await open(page);
    for (const name of ['System', 'Hardware', 'Control']) await goTo(page, name);
    expect(external).toEqual([]);
    const fonts = await page.evaluate(() => [...document.fonts].filter((font) => font.status === 'loaded').map((font) => font.family.replace(/"/g, '')));
    expect(fonts).toContain('Inter Variable');
    expect(fonts).toContain('IBM Plex Mono');
  });

  for (const width of WIDTHS) {
    test(`no horizontal overflow at ${width}px, and the system chapter fits its screen`, async ({ page }) => {
      await open(page, width, width < 640 ? 812 : 900);
      expect(await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth)).toBeLessThanOrEqual(0);
      await goTo(page, 'System');
      const viewport = page.viewportSize()!;
      const panel = page.locator('.panel--system');
      const box = (await panel.boundingBox())!;
      expect(box.x).toBeGreaterThanOrEqual(0);
      expect(box.x + box.width).toBeLessThanOrEqual(viewport.width + 1);

      if (width < 900) {
        await expect(page.locator('.flow__list')).toBeVisible();
        await expect(page.locator('.flow__scroll')).toBeHidden();
        const items = page.locator('.flow__item-button');
        expect(await items.count()).toBe(14);
        await items.last().scrollIntoViewIfNeeded();
        const itemBox = (await items.last().boundingBox())!;
        const panelBox = (await panel.boundingBox())!;
        expect(itemBox.y + itemBox.height).toBeLessThanOrEqual(panelBox.y + panelBox.height + 1);
        for (const item of await items.all()) {
          const itemRect = (await item.boundingBox())!;
          expect(itemRect.x).toBeGreaterThanOrEqual(0);
          expect(itemRect.x + itemRect.width).toBeLessThanOrEqual(viewport.width + 1);
        }
      } else {
        await expect(page.locator('.flow__list')).toBeHidden();
        const chart = (await page.locator('.flow__svg').boundingBox())!;
        const sheet = (await panel.boundingBox())!;
        expect(chart.x).toBeGreaterThanOrEqual(sheet.x - 1);
        expect(chart.x + chart.width).toBeLessThanOrEqual(sheet.x + sheet.width + 1);
        expect(chart.y + chart.height).toBeLessThanOrEqual(viewport.height);
      }
    });
  }

  test('the System link lands on a chart that is fully drawn', async ({ page }) => {
    await open(page);
    await goTo(page, 'System');
    const opacities = (selector: string) =>
      page.locator(selector).evaluateAll((elements) => elements.map((element) => Number(getComputedStyle(element).opacity)));
    expect(await page.locator('.flow__line').count()).toBe(18);
    await expect.poll(async () => (await opacities('.flow__line')).every((opacity) => opacity === 1), { timeout: 20_000 }).toBe(true);
    await expect.poll(async () => (await opacities('.flow__node')).every((opacity) => opacity === 1), { timeout: 20_000 }).toBe(true);
  });

  test('chips and parts work from the keyboard on the chart', async ({ page }) => {
    await open(page);
    await goTo(page, 'System');
    const power = page.getByRole('button', { name: 'Power', exact: true });
    await power.focus();
    await page.keyboard.press('Enter');
    await expect(power).toHaveAttribute('aria-pressed', 'true');
    await expect(page.locator('.flow__detail')).toContainText('fuse');
    await page.keyboard.press('Escape');
    await expect(power).toHaveAttribute('aria-pressed', 'false');

    const gyro = page.locator('.flow__node[data-node="mpu"]');
    await gyro.focus();
    await page.keyboard.press('Enter');
    await expect(page.locator('.flow__detail')).toContainText('how fast the box is turning');
    await page.keyboard.press(' ');
    await expect(page.locator('.flow__detail')).toContainText('Hover or tap a part');
  });

  test('on a phone the same parts are a list that pins a part on tap', async ({ page }) => {
    await open(page, 375, 812);
    await goTo(page, 'System');
    const gyro = page.locator('.flow__item-button[data-node="mpu"]');
    await gyro.scrollIntoViewIfNeeded();
    await gyro.click();
    await expect(gyro).toHaveAttribute('aria-pressed', 'true');
    await expect(page.locator('.flow__detail')).toContainText('how fast the box is turning');
    await expect(gyro).toContainText('→ XIAO ESP32-S3 (I²C)');
    await gyro.click();
    await expect(gyro).toHaveAttribute('aria-pressed', 'false');
  });

  test('the hero and the hardware rail keep text readable over the model', async ({ page }) => {
    await open(page, 375, 812);
    const hero = await page.locator('.panel--hero').evaluate((element) => getComputedStyle(element).backgroundImage);
    expect(hero).toContain('linear-gradient');
    const cta = page.getByRole('link', { name: 'See how it works' });
    const ctaIsTopmost = await cta.evaluate((link) => {
      const rect = link.getBoundingClientRect();
      return link.contains(document.elementFromPoint(rect.x + rect.width / 2, rect.y + rect.height / 2));
    });
    expect(ctaIsTopmost).toBe(true);
    await open(page, 1440, 900);
    const rail = await page.locator('.panel--rail a').first().evaluate((element) => Number(getComputedStyle(element).opacity));
    expect(rail).toBeGreaterThanOrEqual(0.8);
  });
});

test('without WebGL the page still loads, says so, and navigates', async ({ baseURL }) => {
  const browser = await chromium.launch({ channel: 'chrome', args: ['--disable-3d-apis', '--disable-gpu'] });
  const page = await browser.newPage({ baseURL: baseURL! });
  const errors = await open(page);
  expect(await page.evaluate(() => document.documentElement.dataset.webgl)).toBe('false');
  await expect(page.locator('h1')).toBeVisible();
  await goTo(page, 'System');
  await expect(page.locator('.flow')).toBeVisible();
  expect(errors.filter((message) => !/WebGL|GPU|context/i.test(message))).toEqual([]);
  await browser.close();
});
