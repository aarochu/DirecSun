import { expect, test, type Page, type WebSocketRoute } from '@playwright/test';

type Frame = Record<string, unknown>;

async function attachFakeBox(page: Page, options: { wheelRefRpm?: number; flags?: string[] } = {}) {
  const received: Frame[] = [];
  let mode = 'SAFE';
  let heading = 0;
  let target: number | null = null;
  const flags: string[] = options.flags ?? ['MOMENTUM_OK'];
  let route: WebSocketRoute | null = null;
  let seq = 0;
  let sockets = 0;

  const send = (frame: Frame) => route?.send(JSON.stringify({ v: 1, seq: ++seq, ...frame }));
  const setMode = (next: string, cause: string) => {
    mode = next;
    send({ type: 'event', t_ms: Date.now() % 1e6, event: 'mode', mode: next, cause });
  };

  await page.routeWebSocket('ws://box.test/ws', (ws) => {
    sockets += 1;
    route = ws;
    const timer = setInterval(() => {
      send({
        type: 'tlm',
        t_ms: Date.now() % 1e6,
        mode,
        manual_input: null,
        psi_deg: heading,
        psi_ref_deg: mode === 'SLEW' ? target : null,
        rate_dps: 0,
        mag_deg: heading,
        sun_deg: null,
        sun_valid: false,
        wheel_rpm: mode === 'SAFE' ? 0 : 2400,
        wheel_ref_rpm: mode === 'SAFE' ? 0 : (options.wheelRefRpm ?? 2400),
        throttle: 0,
        tau_cmd_mNm: 0,
        vbat: 10.1,
        loop_us: 700,
        flags,
        t_c: null,
        p_pa: null,
        tlm_dropped: 0,
        dropped_events: 0,
      });
    }, 50);
    ws.onClose(() => clearInterval(timer));
    ws.onMessage((raw) => {
      const frame = JSON.parse(String(raw)) as Frame;
      received.push(frame);
      if (frame.type === 'ping') {
        send({ type: 'pong', ping_seq: frame.seq, t_client_ms: frame.t_client_ms, t_ms: Date.now() % 1e6 });
        return;
      }
      if (frame.type !== 'cmd') return;
      const ok = (extra: Frame = {}) => route?.send(JSON.stringify({ v: 1, type: 'ack', seq: frame.seq, ok: true, ...extra }));
      if (frame.cmd === 'set_params') {
        ok({ applied: frame.params });
        return;
      }
      ok();
      if (frame.cmd === 'arm') {
        setMode('SPINUP', 'arm');
        setTimeout(() => setMode('READY', 'spinup_complete'), 300);
      } else if (frame.cmd === 'zero_heading') {
        heading = 0;
      } else if (frame.cmd === 'estop') {
        flags.push('ESTOP');
        setMode('SAFE', 'ESTOP');
      } else if (frame.cmd === 'mode' && frame.mode === 'SLEW') {
        target = frame.psi_ref_deg as number;
        setMode('SLEW', 'mode');
        setTimeout(() => {
          heading = target!;
          setMode('READY', 'slew_complete');
          // The real firmware can flush one pre-transition frame after the
          // completion event. Keep this ordering in the browser gate.
          send({ type: 'tlm', t_ms: Date.now() % 1e6, mode: 'SLEW', psi_deg: heading, psi_ref_deg: target });
        }, 400);
      }
    });
  });
  return { received, sockets: () => sockets };
}

async function connect(page: Page) {
  await page.goto('/');
  await page.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }).fill('ws://box.test/ws');
  await page.getByRole('button', { name: 'Connect' }).click();
  await expect(page.getByText('DEVICE CONNECTED — RECEIVING TELEMETRY')).toBeVisible();
}

const commands = (received: Frame[]) => received.filter((frame) => frame.type === 'cmd');
const status = (page: Page) => page.locator('.controls__status');

test.describe('box control against a scripted box', () => {
  test('READY with Hall bypass refuses turns in the UI without sending a command', async ({ page }) => {
    const { received } = await attachFakeBox(page, { wheelRefRpm: 0, flags: ['HALL_BYPASS'] });
    await connect(page);
    await page.getByRole('button', { name: 'Arm the box' }).click();
    await page.getByRole('checkbox').check();
    await page.getByRole('button', { name: 'Arm now' }).click();
    await expect(status(page)).toHaveText('READY / DEVICE', { timeout: 4000 });

    const rotate = page.getByRole('button', { name: 'Rotate right ninety degrees and return' });
    await expect(rotate).toBeDisabled();
    await expect(page.getByText(/HALL SENSOR IS BYPASSED/)).toBeVisible();
    await rotate.evaluate((button: HTMLButtonElement) => button.click());
    expect(commands(received).map((frame) => frame.cmd)).toEqual(['arm']);
  });

  test('connecting and watching sends nothing', async ({ page }) => {
    const { received } = await attachFakeBox(page);
    await connect(page);
    await page.waitForTimeout(1200);
    expect(received).toEqual([]);
  });

  test('SAFE parameters send only changed values and show the applied set', async ({ page }) => {
    const { received } = await attachFakeBox(page);
    await connect(page);
    await page.getByRole('button', { name: /PARAMETERS \/ SAFE ONLY/ }).click();
    await page.getByLabel(/Rate gain/).fill('0.02');
    await page.getByRole('button', { name: 'Apply changed parameters' }).click();

    await expect(page.getByText('Applied: k_omega = 0.02')).toBeVisible();
    expect(commands(received).at(-1)).toMatchObject({ cmd: 'set_params', params: { k_omega: 0.02 } });
  });

  test('arm needs the lock-pin confirmation, then right 90 turns, holds and returns', async ({ page }) => {
    const { received } = await attachFakeBox(page);
    await connect(page);

    await page.getByRole('button', { name: 'Arm the box' }).click();
    const armNow = page.getByRole('button', { name: 'Arm now' });
    await expect(armNow).toBeDisabled();
    expect(received).toEqual([]);
    await page.getByRole('checkbox').check();
    await armNow.click();
    await expect(status(page)).toHaveText('READY / DEVICE', { timeout: 4000 });

    await page.getByRole('button', { name: 'Rotate right ninety degrees and return' }).click();
    await expect(status(page)).toContainText('TURNING');
    await expect(status(page)).toContainText('HOLDING TARGET', { timeout: 4000 });
    await expect(page.locator('.readout--target .readout__value')).toHaveText('+90.0°');
    await expect(status(page)).toContainText('RETURNING', { timeout: 4000 });
    await expect(status(page)).toHaveText('READY / DEVICE', { timeout: 4000 });
    await expect(page.locator('.controls__result')).toHaveText('TURN COMPLETE: BACK AT THE START HEADING.');

    const sent = commands(received);
    expect(sent.map((frame) => frame.cmd)).toEqual(['arm', 'zero_heading', 'mode', 'mode']);
    expect(sent[2]).toMatchObject({ mode: 'SLEW', psi_ref_deg: 90 });
    expect(sent[3]).toMatchObject({ mode: 'SLEW', psi_ref_deg: 0 });
    const pings = received.filter((frame) => frame.type === 'ping');
    expect(pings.length).toBeGreaterThan(8);
  });

  test('the space bar sends e-stop while armed, and the latched fault shows', async ({ page }) => {
    const { received } = await attachFakeBox(page);
    await connect(page);
    await page.evaluate(() => (document.activeElement as HTMLElement).blur());
    await page.keyboard.press('Space');
    await page.waitForTimeout(300);
    expect(commands(received)).toEqual([]);

    await page.getByRole('button', { name: 'Arm the box' }).click();
    await page.getByRole('checkbox').check();
    await page.getByRole('button', { name: 'Arm now' }).click();
    await expect(status(page)).toHaveText('READY / DEVICE', { timeout: 4000 });

    await page.getByRole('button', { name: 'Disconnect' }).focus();
    await page.keyboard.press('Space');
    await expect(status(page)).toHaveText('SAFE / DEVICE');
    await expect(page.locator('.controls__result')).toHaveText('E-STOP ACCEPTED: THE WHEEL RAMPS DOWN.');
    await expect(page.getByText(/ANOTHER CLIENT IS CONTROLLING/)).toHaveCount(0);
    await expect(page.getByRole('button', { name: 'Disconnect' })).toBeVisible();
    await expect(page.getByRole('list', { name: 'Device flags' })).toContainText('LATCHED ESTOP');
    expect(commands(received).at(-1)).toMatchObject({ cmd: 'estop' });
  });

  test('a dropped link never reconnects by itself', async ({ page }) => {
    const box = await attachFakeBox(page);
    await connect(page);
    await page.getByRole('button', { name: 'Disconnect' }).click();
    await page.waitForTimeout(1500);
    expect(box.sockets()).toBe(1);
    await expect(page.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeVisible();
  });
});
