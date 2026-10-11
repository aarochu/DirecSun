import { expect, test, type Page } from '@playwright/test';

const phase = (page: Page) => page.locator('.controls__status');
const readout = (page: Page, label: string) =>
  page.locator('.rail .readout').filter({ has: page.locator(`dt:text-is("${label}")`) }).locator('dd');

test.describe('ground station judge flow', () => {
  test('ROUTE_REFRESH: the ground station loads directly and after a reload', async ({ page }) => {
    await page.goto('/');
    await expect(page.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeVisible();
    await page.reload();
    await expect(page.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeVisible();
    await expect(readout(page, 'HEADING')).toHaveText('0.0°');
    await expect(readout(page, 'REACTION WHEEL')).toContainText('2400 RPM');
  });

  for (const [direction, label, target] of [
    ['right', 'Rotate right ninety degrees and return', '+90.0°'],
    ['left', 'Rotate left ninety degrees and return', '−90.0°'],
  ] as const) {
    test(`${direction} 90° → hold → automatic return → READY`, async ({ page }) => {
      await page.goto('/');
      const left = page.getByRole('button', { name: 'Rotate left ninety degrees and return' });
      const right = page.getByRole('button', { name: 'Rotate right ninety degrees and return' });

      await page.getByRole('button', { name: label }).click();
      await expect(phase(page)).toHaveText('ROTATING / SIMULATED');
      await expect(left).toBeDisabled();
      await expect(right).toBeDisabled();
      await expect(readout(page, 'TARGET')).toHaveText(target);

      await expect(phase(page)).toHaveText('HOLDING TARGET / SIMULATED', { timeout: 4000 });
      await expect(readout(page, 'HEADING')).toHaveText(target);
      await expect(phase(page)).toHaveText('RETURNING / SIMULATED', { timeout: 2000 });
      await expect(readout(page, 'TARGET')).toHaveText('0.0°');
      await expect(phase(page)).toHaveText('READY / SIMULATED', { timeout: 5000 });
      await expect(readout(page, 'HEADING')).toHaveText('0.0°');
      await expect(readout(page, 'BODY RATE')).toHaveText('0.0°/s');
      await expect(left).toBeEnabled();
    });
  }

  test('STOP cancels the pending return; RESET restores 0°', async ({ page }) => {
    await page.goto('/');
    await page.getByRole('button', { name: 'Rotate right ninety degrees and return' }).click();
    await page.waitForTimeout(700);
    await page.getByRole('button', { name: 'Stop the simulation' }).click();
    await expect(phase(page)).toHaveText('STOPPED / SIMULATED');
    const stoppedAt = await readout(page, 'HEADING').textContent();
    await page.waitForTimeout(3500);
    await expect(readout(page, 'HEADING')).toHaveText(stoppedAt!);
    await expect(phase(page)).toHaveText('STOPPED / SIMULATED');

    await page.getByRole('button', { name: 'Reset the simulation' }).click();
    await expect(phase(page)).toHaveText('READY / SIMULATED');
    await expect(readout(page, 'HEADING')).toHaveText('0.0°');
  });

  test('MOCK_ONLY: clicking every control makes no network request', async ({ page }) => {
    await page.goto('/');
    await page.waitForLoadState('networkidle');
    await page.evaluate(() => Promise.all([...document.fonts].map((face) => face.load())));
    await page.waitForLoadState('networkidle');
    const requests: string[] = [];
    page.on('request', (request) => requests.push(request.url()));
    page.on('websocket', (socket) => requests.push(socket.url()));

    await page.getByRole('button', { name: 'Rotate right ninety degrees and return' }).click();
    await page.waitForTimeout(600);
    await page.getByRole('button', { name: 'Stop the simulation' }).click();
    await page.getByRole('button', { name: 'Reset the simulation' }).click();
    await page.getByRole('button', { name: 'Rotate left ninety degrees and return' }).click();
    await expect(phase(page)).toHaveText('READY / SIMULATED', { timeout: 8000 });
    await page.getByRole('button', { name: 'Reset the simulation' }).click();

    expect(requests).toEqual([]);
  });

  test('OFFLINE: both pages load only same-origin resources, fonts included', async ({ page, baseURL }) => {
    const origin = new URL(baseURL!).origin;
    const external: string[] = [];
    page.on('request', (request) => {
      if (new URL(request.url()).origin !== origin) external.push(request.url());
    });
    for (const path of ['/overview', '/']) {
      await page.goto(path);
      await page.waitForLoadState('networkidle');
      await page.evaluate(() => Promise.all([...document.fonts].map((face) => face.load())));
      const families = await page.evaluate(() =>
        [...document.fonts].filter((face) => face.status === 'loaded').map((face) => face.family.replace(/"/g, '')),
      );
      expect(families).toContain('Inter Variable');
      expect(families).toContain('IBM Plex Mono');
    }
    expect(external).toEqual([]);
  });
});

test.describe('landing', () => {
  test('LANDING_NAV: header links scroll to their sections; CTA opens the ground station', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto('/overview');
    for (const [label, id] of [
      ['Hardware', 'hardware'],
      ['Control', 'control'],
      ['System', 'system'],
    ] as const) {
      await page.getByRole('navigation', { name: 'Primary' }).getByRole('link', { name: label }).click();
      await expect(page).toHaveURL(new RegExp(`#${id}$`));
      await expect
        .poll(() => page.evaluate((target) => Math.round(document.getElementById(target)!.getBoundingClientRect().top), id))
        .toBe(76);
    }
    await page.getByRole('link', { name: 'Open Ground Station' }).first().click();
    await expect(page).toHaveURL(/\/$/);
    await expect(page.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeVisible();
    await expect(page.getByRole('link', { name: 'Overview' })).toHaveAttribute('href', '/');
  });

  test('OrbitSpace renders in the hero only and leaves CTAs clickable', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto('/overview');
    const hero = page.locator('#top');
    await expect(hero).toHaveAttribute('data-background', 'orbit-space');
    await expect(hero.locator('canvas')).toHaveCount(1);
    await expect(page.locator('canvas')).toHaveCount(1);
    const box = await hero.locator('canvas').boundingBox();
    const heroBox = await hero.boundingBox();
    expect(box!.height).toBeLessThanOrEqual(heroBox!.height + 1);

    const hit = await page.evaluate(() => {
      const cta = [...document.querySelectorAll('a')].find((a) => a.textContent?.includes('Explore the system'))!;
      const rect = cta.getBoundingClientRect();
      return cta.contains(document.elementFromPoint(rect.x + rect.width / 2, rect.y + rect.height / 2));
    });
    expect(hit).toBe(true);

    await page.goto('/');
    await expect(page.locator('.hero__orbit')).toHaveCount(0);
  });

  test('REDUCED_MOTION: static hero, ground station still works', async ({ browser }) => {
    const context = await browser.newContext({ reducedMotion: 'reduce' });
    const page = await context.newPage();
    await page.goto('/overview');
    await expect(page.locator('#top')).toHaveAttribute('data-background', 'static');
    await expect(page.locator('canvas')).toHaveCount(0);
    await page.goto('/');
    await page.getByRole('button', { name: 'Rotate right ninety degrees and return' }).click();
    await expect(phase(page)).toHaveText('READY / SIMULATED', { timeout: 8000 });
    await expect(readout(page, 'HEADING')).toHaveText('0.0°');
    await context.close();
  });
});

test.describe('responsive', () => {
  for (const width of [320, 375, 390, 768, 1024, 1440]) {
    test(`MOBILE_320 and friends: no horizontal overflow at ${width}px`, async ({ page }) => {
      await page.setViewportSize({ width, height: 900 });
      for (const path of ['/overview', '/']) {
        await page.goto(path);
        await page.waitForLoadState('networkidle');
        const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
        expect(overflow, `${path} at ${width}px`).toBeLessThanOrEqual(0);
      }
      const controls = page.getByRole('button', { name: 'Rotate right ninety degrees and return' });
      await expect(controls).toBeVisible();
      const box = await controls.boundingBox();
      expect(box!.height).toBeGreaterThanOrEqual(44);
    });
  }

  test('at 1440×900 the model and both direction commands fit without scrolling', async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto('/');
    for (const name of ['Rotate left ninety degrees and return', 'Rotate right ninety degrees and return']) {
      const box = await page.getByRole('button', { name }).boundingBox();
      expect(box!.y + box!.height).toBeLessThanOrEqual(900);
    }
    const viewport = await page.locator('.viewport').boundingBox();
    expect(viewport!.height).toBeGreaterThanOrEqual(580);
  });

  for (const width of [320, 375, 390, 768, 1440]) {
    test(`device link URL field shows a full endpoint at ${width}px`, async ({ page }) => {
      await page.setViewportSize({ width, height: 900 });
      await page.goto('/');
      const input = page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' });
      for (const url of ['ws://127.0.0.1:8081/ws', 'ws://direcsun.local/ws', 'ws://192.168.4.1/ws']) {
        await input.fill(url);
        const fits = await input.evaluate((el: HTMLInputElement) => el.scrollWidth <= el.clientWidth);
        expect(fits, `${url} at ${width}px`).toBe(true);
      }
    });
  }
});
