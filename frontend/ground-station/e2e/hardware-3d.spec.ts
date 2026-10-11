import { expect, test, type Page } from '@playwright/test';

const toggle = (page: Page, id: string) => page.locator(`#part-${id}-heading`).locator('xpath=ancestor::button');
const live = (page: Page, id: string) => page.locator(`#part-${id}-panel .part-view[data-view-state="ready"]`);
const azimuth = async (page: Page, id: string) =>
  Number(await page.locator(`#part-${id}-panel .part-view`).getAttribute('data-azimuth'));

async function openRow(page: Page, id: string) {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto('/overview#hardware');
  await toggle(page, id).click();
  await page.locator(`#part-${id}-panel`).scrollIntoViewIfNeeded();
  await expect(live(page, id)).toBeVisible({ timeout: 15000 });
}

test.describe('hardware CAD views', () => {
  test('a row opens a 3D view that rotates by dragging and resets', async ({ page }) => {
    await openRow(page, 'mpu6050');
    const stage = page.locator('#part-mpu6050-panel .part-view__stage');
    const box = (await stage.boundingBox())!;
    const before = await azimuth(page, 'mpu6050');
    const frameBefore = await page.locator('#part-mpu6050-panel .media-frame').screenshot();

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 160, box.y + box.height / 2 + 30, { steps: 8 });
    await page.mouse.up();
    await page.waitForTimeout(400);

    expect(await azimuth(page, 'mpu6050')).not.toBe(before);
    const frameAfter = await page.locator('#part-mpu6050-panel .media-frame').screenshot();
    expect(frameAfter.equals(frameBefore)).toBe(false);

    await page.getByRole('button', { name: 'Reset the 3D view' }).click();
    await expect.poll(() => azimuth(page, 'mpu6050')).toBe(before);
  });

  test('arrow keys rotate the focused view', async ({ page }) => {
    await openRow(page, 'esc');
    const before = await azimuth(page, 'esc');
    await page.locator('#part-esc-panel .part-view__stage').focus();
    await page.keyboard.press('ArrowRight');
    await expect.poll(() => azimuth(page, 'esc')).toBe((before + 10) % 360);
    await page.keyboard.press('ArrowLeft');
    await page.keyboard.press('ArrowLeft');
    await expect.poll(() => azimuth(page, 'esc')).toBe((before + 350) % 360);
  });

  test('plain scrolling over the view still scrolls the page; Ctrl + scroll zooms instead', async ({ page }) => {
    await openRow(page, 'power');
    const stage = page.locator('#part-power-panel .part-view__stage');
    const box = (await stage.boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);

    const scrollBefore = await page.evaluate(() => window.scrollY);
    await page.mouse.wheel(0, 240);
    await expect.poll(() => page.evaluate(() => window.scrollY)).toBeGreaterThan(scrollBefore);

    const scrollMid = await page.evaluate(() => window.scrollY);
    const frameBefore = await page.locator('#part-power-panel .media-frame').screenshot();
    const defaultAllowed = await page.locator('#part-power-panel canvas').evaluate((canvas) =>
      canvas.dispatchEvent(new WheelEvent('wheel', { deltaY: -240, ctrlKey: true, bubbles: true, cancelable: true })),
    );
    expect(defaultAllowed).toBe(false);
    await page.waitForTimeout(400);
    expect(await page.evaluate(() => window.scrollY)).toBe(scrollMid);
    const frameAfter = await page.locator('#part-power-panel .media-frame').screenshot();
    expect(frameAfter.equals(frameBefore)).toBe(false);
  });

  test('Part only hides the surrounding stack and brings it back', async ({ page }) => {
    await openRow(page, 'bmp280');
    const frame = page.locator('#part-bmp280-panel .media-frame');
    const full = await frame.screenshot();
    const button = page.getByRole('button', { name: 'Show only this part and its carrier' });
    await button.click();
    await expect(button).toHaveAttribute('aria-pressed', 'true');
    await page.waitForTimeout(300);
    const only = await frame.screenshot();
    expect(only.equals(full)).toBe(false);
    await button.click();
    await expect(button).toHaveAttribute('aria-pressed', 'false');
  });

  test('all ten posters are served as images, each labeled with what it highlights', async ({ page, request }) => {
    await page.goto('/overview#hardware');
    await page.getByRole('button', { name: 'Expand all' }).click();
    const sources = await page.locator('.hardware-row__media img').evaluateAll((images) => images.map((image) => image.getAttribute('src')!));
    expect(sources).toHaveLength(10);
    for (const source of sources) {
      const response = await request.get(source);
      expect(response.status(), source).toBe(200);
      expect(response.headers()['content-type'], source).toContain('image/webp');
    }
    await expect(page.locator('.hardware-row__shows')).toHaveCount(10);
  });

  test('expanding every row draws every view through one shared WebGL context', async ({ page }) => {
    await page.addInitScript(() => {
      const w = window as unknown as { __contexts: number };
      w.__contexts = 0;
      const original = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (this: HTMLCanvasElement, type: string, ...rest: unknown[]) {
        const context = (original as (...args: unknown[]) => unknown).call(this, type, ...rest);
        if (context && (type === 'webgl' || type === 'webgl2')) w.__contexts += 1;
        return context as never;
      } as typeof original;
    });
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto('/overview#hardware');
    await page.waitForTimeout(1500);
    const before = await page.evaluate(() => (window as unknown as { __contexts: number }).__contexts);
    await page.getByRole('button', { name: 'Expand all' }).click();
    await expect(page.locator('.part-view[data-view-state="ready"]')).toHaveCount(10, { timeout: 30000 });
    const created = (await page.evaluate(() => (window as unknown as { __contexts: number }).__contexts)) - before;
    expect(created).toBe(1);
  });

  test('a phone-width view opens without horizontal overflow and keeps its controls inside the frame', async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 800 });
    await page.goto('/overview#hardware');
    await toggle(page, 'qmc5883p').click();
    await page.locator('#part-qmc5883p-panel').scrollIntoViewIfNeeded();
    await expect(live(page, 'qmc5883p')).toBeVisible({ timeout: 15000 });
    expect(await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth)).toBeLessThanOrEqual(0);
    const frame = (await page.locator('#part-qmc5883p-panel .media-frame').boundingBox())!;
    for (const name of ['Reset the 3D view', 'Show only this part and its carrier']) {
      const button = (await page.getByRole('button', { name }).boundingBox())!;
      expect(button.x).toBeGreaterThanOrEqual(frame.x);
      expect(button.x + button.width).toBeLessThanOrEqual(frame.x + frame.width);
    }
  });

  test('opening a row stalls the page briefly: the first pays for the context, the rest only draw', async ({ page }) => {
    await page.addInitScript(() => {
      const w = window as unknown as { __long: number[] };
      w.__long = [];
      new PerformanceObserver((list) => list.getEntries().forEach((entry) => w.__long.push(entry.duration))).observe({
        entryTypes: ['longtask'],
      });
    });
    await openRow(page, 'frame');
    await page.waitForTimeout(500);
    const longest = () => page.evaluate(() => Math.max(0, ...(window as unknown as { __long: number[] }).__long));
    expect(await longest()).toBeLessThan(1200);

    for (const id of ['esp32s3', 'mpu6050', 'esc']) {
      await page.evaluate(() => ((window as unknown as { __long: number[] }).__long = []));
      await toggle(page, id).click();
      await page.locator(`#part-${id}-panel`).scrollIntoViewIfNeeded();
      await expect(live(page, id)).toBeVisible({ timeout: 15000 });
      await page.waitForTimeout(300);
      expect(await longest(), id).toBeLessThan(500);
    }
  });

  test('opening and closing rows never creates more than one WebGL context', async ({ page }) => {
    await page.addInitScript(() => {
      const w = window as unknown as { __contexts: number };
      w.__contexts = 0;
      const original = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (this: HTMLCanvasElement, type: string, ...rest: unknown[]) {
        const context = (original as (...args: unknown[]) => unknown).call(this, type, ...rest);
        if (context && (type === 'webgl' || type === 'webgl2')) w.__contexts += 1;
        return context as never;
      } as typeof original;
    });
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto('/overview#hardware');
    await page.waitForTimeout(1500);
    const before = await page.evaluate(() => (window as unknown as { __contexts: number }).__contexts);
    const ids = ['frame', 'esp32s3', 'mpu6050', 'qmc5883p', 'esc', 'power'];
    for (let cycle = 0; cycle < 12; cycle++) {
      const id = ids[cycle % ids.length]!;
      await toggle(page, id).click();
      await page.locator(`#part-${id}-panel`).scrollIntoViewIfNeeded();
      await expect(live(page, id)).toBeVisible({ timeout: 15000 });
      await toggle(page, id).click();
    }
    const created = (await page.evaluate(() => (window as unknown as { __contexts: number }).__contexts)) - before;
    expect(created).toBe(1);
  });

  test('the focused 3D view shows a visible focus ring, and the hint is readable', async ({ page }) => {
    await openRow(page, 'mpu6050');
    await page.locator('#part-mpu6050-panel .part-view__stage').focus();
    await page.keyboard.press('ArrowRight');
    const ring = await page.locator('#part-mpu6050-panel .part-view__stage').evaluate((element) => {
      const style = getComputedStyle(element);
      return { width: parseFloat(style.outlineWidth), offset: parseFloat(style.outlineOffset), style: style.outlineStyle };
    });
    expect(ring.style).not.toBe('none');
    expect(ring.width).toBeGreaterThan(0);
    expect(ring.offset).toBeLessThanOrEqual(0);
    const hint = await page.locator('#part-mpu6050-panel .part-view__hint').evaluate((element) => getComputedStyle(element).backgroundColor);
    expect(hint).not.toBe('rgba(0, 0, 0, 0)');
  });
});
