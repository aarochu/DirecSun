import { test } from '@playwright/test';

test.skip(!process.env.QA_SCREENSHOTS, 'set QA_SCREENSHOTS=1 to capture');

const WIDTHS = [320, 375, 768, 1024, 1440];

for (const width of WIDTHS) {
  test(`capture ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: width < 640 ? 812 : 900 });

    await page.goto('/');
    await page.waitForLoadState('networkidle');
    await page.waitForTimeout(3200);
    await page.evaluate(() => document.querySelectorAll<HTMLElement>('[data-reveal]').forEach((el) => (el.dataset.reveal = 'shown')));
    await page.screenshot({ path: `qa-screenshots/landing-${width}.jpg`, fullPage: true, type: 'jpeg', quality: 70 });

    await page.goto('/ground-station');
    await page.waitForLoadState('networkidle');
    await page.screenshot({ path: `qa-screenshots/ground-${width}-ready.jpg`, fullPage: true, type: 'jpeg', quality: 75 });
    await page.getByRole('button', { name: 'Rotate right ninety degrees and return' }).click();
    await page.waitForTimeout(1000);
    await page.screenshot({ path: `qa-screenshots/ground-${width}-rotating.jpg`, fullPage: true, type: 'jpeg', quality: 75 });
  });
}
