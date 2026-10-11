import { expect, test } from '@playwright/test';

test('slow rendering displays recent telemetry instead of building a frame backlog', async ({ page }) => {
  await page.addInitScript(() => {
    const original = window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame = (callback) => original((time) => {
      if (document.querySelector('.link--open')) {
        const until = performance.now() + 35;
        while (performance.now() < until) { /* deliberately slow display */ }
      }
      callback(time);
    });
  });

  let sent = 0;
  await page.routeWebSocket('ws://backpressure.test/ws', (socket) => {
    const timer = setInterval(() => {
      const seq = ++sent;
      socket.send(JSON.stringify({
        v: 1, type: 'tlm', seq, t_ms: seq * 20, mode: 'SAFE',
        psi_deg: seq % 360, rate_dps: 0, wheel_rpm: 0, vbat: 10.1, flags: [],
      }));
    }, 20);
    socket.onClose(() => clearInterval(timer));
  });

  await page.goto('/');
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill('ws://backpressure.test/ws');
  await page.getByRole('button', { name: 'Connect' }).click();
  await expect(page.locator('.link__status')).toContainText('RECEIVING TELEMETRY');

  for (let sample = 0; sample < 3; sample++) {
    await page.waitForTimeout(2000);
    const before = Date.now();
    const displayed = await page.locator('.link__status').textContent();
    const readMs = Date.now() - before;
    const packets = Number(displayed?.match(/(\d+) PACKETS/)?.[1]);
    expect(packets).toBeGreaterThan(0);
    expect(sent - packets).toBeLessThanOrEqual(10);
    expect(readMs).toBeLessThan(200);
  }
});
