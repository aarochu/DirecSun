import { act, fireEvent, render, screen, within } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { DeviceLink, flagSeverity, parseTelemetry, validateLinkUrl } from '../hardware/deviceLink';
import { FakeSocket, tlm } from './fakeSocket';

const nextFrame = () => new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));

afterEach(() => {
  FakeSocket.instances = [];
  vi.unstubAllGlobals();
});

describe('parseTelemetry', () => {
  it('reads the v1 telemetry shape', () => {
    expect(
      parseTelemetry(
        '{"v":1,"type":"tlm","seq":1042,"t_ms":83120,"mode":"HOLD","psi_deg":47.2,"psi_ref_deg":47,"rate_dps":0.3,"wheel_rpm":2504,"vbat":10.1}',
      ),
    ).toEqual({
      seq: 1042,
      tMs: 83120,
      mode: 'HOLD',
      manualInput: null,
      headingDeg: 47.2,
      targetDeg: 47,
      rateDegS: 0.3,
      wheelRpm: 2504,
      wheelRefRpm: null,
      throttleBand: null,
      flags: [],
      batteryV: 10.1,
      sensors: {
        rateDps: 0.3,
        gyroRaw: null,
        accelRmsG: null,
        magDeg: null,
        magRaw: null,
        sunDeg: null,
        sunValid: null,
        sunAmV: null,
        sunBmV: null,
        wheelRpm: 2504,
        batteryV: 10.1,
        tempC: null,
        pressurePa: null,
        loopUs: null,
      },
      throttle: null,
      tauCmdMNm: null,
      tlmDropped: null,
      droppedEvents: null,
    });
  });

  it('reads the motor command, torque and the box drop counters', () => {
    expect(parseTelemetry(tlm({ psi_deg: 0, throttle: 412, tau_cmd_mNm: 0.8, tlm_dropped: 3, dropped_events: 1 }))).toMatchObject({
      throttle: 412,
      tauCmdMNm: 0.8,
      tlmDropped: 3,
      droppedEvents: 1,
    });
  });

  it('tolerates missing optional fields', () => {
    expect(parseTelemetry(tlm({ psi_deg: 10 }))).toMatchObject({
      headingDeg: 10,
      wheelRpm: null,
      wheelRefRpm: null,
      throttleBand: null,
      flags: [],
      batteryV: null,
    });
  });

  it('reads every sensor field, including the bench-mode raw values', () => {
    expect(
      parseTelemetry(
        tlm({
          psi_deg: 12,
          rate_dps: -1.5,
          mag_deg: 13.2,
          sun_deg: -4.1,
          sun_valid: true,
          wheel_rpm: 2400,
          vbat: 10.4,
          t_c: 23.5,
          p_pa: 101325,
          loop_us: 812,
          gyro_raw: [12, -3, 410],
          mag_raw: [1500, -220, 3100],
          sun_a_mv: 812,
          sun_b_mv: 790,
          accel_rms_g: 0.04,
        }),
      )?.sensors,
    ).toEqual({
      rateDps: -1.5,
      gyroRaw: [12, -3, 410],
      accelRmsG: 0.04,
      magDeg: 13.2,
      magRaw: [1500, -220, 3100],
      sunDeg: -4.1,
      sunValid: true,
      sunAmV: 812,
      sunBmV: 790,
      wheelRpm: 2400,
      batteryV: 10.4,
      tempC: 23.5,
      pressurePa: 101325,
      loopUs: 812,
    });
  });

  it('reads flags, the wheel speed reference and the optional throttle band', () => {
    expect(
      parseTelemetry(
        tlm({
          psi_deg: 0,
          wheel_ref_rpm: 2400,
          throttle_band: [200, 600],
          flags: ['MOMENTUM_NEAR_LIMIT', 'HALL_BYPASS', 'ESTOP', 'ESTOP', 7, ''],
        }),
      ),
    ).toMatchObject({ wheelRefRpm: 2400, throttleBand: [200, 600], flags: ['MOMENTUM_NEAR_LIMIT', 'HALL_BYPASS', 'ESTOP'] });
    expect(parseTelemetry(tlm({ psi_deg: 0, throttle_band: [200] }))!.throttleBand).toBeNull();
    expect(parseTelemetry(tlm({ psi_deg: 0, throttle_band: [200, 'x'] }))!.throttleBand).toBeNull();
    expect(parseTelemetry(tlm({ psi_deg: 0, flags: 'ESTOP' }))!.flags).toEqual([]);
  });
});

describe('flagSeverity', () => {
  it('marks latched interlocks as faults, normal status flags as ok, and everything else as a warning', () => {
    for (const fault of ['ESTOP', 'HEARTBEAT_LOST', 'OVERSPEED', 'UNDERSPEED', 'BATTERY_LOW', 'IMU_SILENT', 'TILT', 'CONTROL_STALL', 'HALL_SILENT']) {
      expect(flagSeverity(fault)).toBe('fault');
    }
    expect(flagSeverity('MOMENTUM_OK')).toBe('ok');
    expect(flagSeverity('HEADING_GYRO')).toBe('ok');
    for (const warning of ['MOMENTUM_NEAR_LIMIT', 'MOMENTUM_SATURATED', 'LOOP_OVERRUN', 'MAG_MISSING', 'IMU_MISSING', 'MANUAL_STALE', 'HALL_BYPASS', 'SOMETHING_NEW']) {
      expect(flagSeverity(warning)).toBe('warning');
    }
  });

  it('ignores acks, other versions, malformed JSON and frames without a heading', () => {
    expect(parseTelemetry('{"v":1,"type":"ack","seq":77}')).toBeNull();
    expect(parseTelemetry(JSON.stringify({ v: 2, type: 'tlm', psi_deg: 1 }))).toBeNull();
    expect(parseTelemetry('not json')).toBeNull();
    expect(parseTelemetry(tlm({ psi_deg: 'north' }))).toBeNull();
    expect(parseTelemetry(new ArrayBuffer(4))).toBeNull();
  });
});

describe('validateLinkUrl', () => {
  it('accepts ws:// and wss:// only', () => {
    expect(validateLinkUrl('ws://192.168.4.1/ws')).toBeNull();
    expect(validateLinkUrl('wss://example.local:8443/telemetry')).toBeNull();
    expect(validateLinkUrl('http://192.168.4.1')).toMatch(/ws:\/\//);
    expect(validateLinkUrl('192.168.4.1')).toMatch(/full WebSocket URL/);
  });
});

describe('DeviceLink', () => {
  const setup = () => {
    let now = 0;
    const link = new DeviceLink((url) => new FakeSocket(url) as unknown as WebSocket, () => (now += 60));
    return { link, socket: () => FakeSocket.instances.at(-1)! };
  };

  it('is read-only: connects, receives, never sends', async () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    expect(link.getState().status).toBe('connecting');
    socket().open();
    expect(link.getState().status).toBe('open');
    expect(link.receiving).toBe(false);

    socket().message(tlm({ t_ms: 1000, psi_deg: 45, psi_ref_deg: 90, rate_dps: 12, wheel_rpm: 2300, mode: 'SLEW' }));
    expect(link.receiving).toBe(true);
    await nextFrame();
    expect(link.getTelemetry()).toMatchObject({
      source: 'hardware',
      statusWord: 'SLEW',
      headingDeg: 45,
      targetDeg: 90,
      rateDegS: 12,
      wheelRpm: 2300,
    });
    socket().message('{"type":"log","msg":"hello"}');
    expect(link.getState()).toMatchObject({ packets: 1, ignored: 1 });
    expect(socket().sent).toEqual([]);
  });

  it('unwraps headings across 0°/360° for the model and chart', async () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    socket().open();
    socket().message(tlm({ t_ms: 0, psi_deg: 358 }));
    socket().message(tlm({ t_ms: 100, psi_deg: 2 }));
    expect(link.getFrame().headingDeg).toBeCloseTo(362, 9);
    await nextFrame();
    const { history } = link.getTelemetry();
    expect(history.map((s) => s.t)).toEqual([0, 0.1]);
  });

  it('keeps only one pending display render when rendering is slow, without dropping raw frames or acks', async () => {
    const frames = new Map<number, FrameRequestCallback>();
    let nextId = 0;
    vi.stubGlobal('requestAnimationFrame', (callback: FrameRequestCallback) => {
      frames.set(++nextId, callback);
      return nextId;
    });
    vi.stubGlobal('cancelAnimationFrame', (id: number) => frames.delete(id));
    const { link, socket } = setup();
    let stateUpdates = 0;
    let frameUpdates = 0;
    let telemetryUpdates = 0;
    let rawFrames = 0;
    link.subscribeState(() => stateUpdates++);
    link.subscribeFrame(() => frameUpdates++);
    link.subscribeTelemetry(() => telemetryUpdates++);
    link.subscribeRaw(() => rawFrames++);
    link.connect('ws://device/ws');
    socket().open();
    socket().message(tlm({ seq: 0, t_ms: 0, psi_deg: 0, mode: 'HOLD' }));
    const initialUpdates = stateUpdates;
    const initialFrameUpdates = frameUpdates;
    const initialTelemetryUpdates = telemetryUpdates;

    // Hold the animation callback while 250 telemetry messages arrive. The
    // browser's display may be slow, but only the newest snapshot is rendered.
    for (let seq = 1; seq <= 250; seq++) {
      socket().message(tlm({ seq, t_ms: seq * 20, psi_deg: seq, mode: 'HOLD' }));
    }
    expect(frames.size).toBe(1);
    expect(stateUpdates).toBe(initialUpdates);
    expect(frameUpdates).toBe(initialFrameUpdates);
    expect(telemetryUpdates).toBe(initialTelemetryUpdates);
    expect(rawFrames).toBe(251);

    const command = link.command('zero_heading');
    const commandSeq = socket().lastCommand().seq;
    socket().message(JSON.stringify({ v: 1, type: 'ack', seq: commandSeq, ok: true }));
    await expect(command).resolves.toMatchObject({ seq: commandSeq, ok: true });
    expect(frames.size).toBe(1);

    const [id, render] = [...frames][0]!;
    frames.delete(id);
    render(0);
    expect(link.getState().packets).toBe(251);
    expect(link.getTelemetry()).toMatchObject({ statusWord: 'HOLD', headingDeg: 250 });
    expect(frameUpdates).toBe(initialFrameUpdates + 1);
    expect(telemetryUpdates).toBe(initialTelemetryUpdates + 1);
    expect(stateUpdates).toBe(initialUpdates + 2); // command talking + one display update

    socket().message(tlm({ seq: 251, t_ms: 5020, psi_deg: 251, mode: 'SAFE' }));
    expect(frames.size).toBe(0); // safety transition is immediate
    expect(link.getState().packets).toBe(252);
    expect(link.getTelemetry()).toMatchObject({ statusWord: 'SAFE', headingDeg: 251 });
    socket().message(tlm({ seq: 252, t_ms: 5040, psi_deg: 252, mode: 'SAFE' }));
    expect(frames.size).toBe(1);
    link.disconnect();
    expect(frames.size).toBe(0); // old socket cannot publish after disconnect
  });

  it('bounds chart history even if the box repeats its timestamp', async () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    socket().open();
    for (let seq = 0; seq < 1100; seq++) {
      socket().message(tlm({ seq, t_ms: 0, psi_deg: seq % 360, mode: 'SAFE' }));
    }
    expect(link.getState().packets).toBe(1100);
    await nextFrame();
    expect(link.getTelemetry().history).toHaveLength(1000);
    link.disconnect();
  });

  it('does not reconnect after the socket drops', () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    socket().open();
    socket().message(tlm({ psi_deg: 5 }));
    socket().drop(1006);
    expect(link.getState().status).toBe('closed');
    expect(link.receiving).toBe(false);
    expect(FakeSocket.instances).toHaveLength(1);
  });

  it('disconnect closes the socket and ignores late events', () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    const first = socket();
    first.open();
    link.disconnect();
    expect(first.closedWith).toBe(1000);
    expect(link.getState().status).toBe('idle');
    first.message(tlm({ psi_deg: 5 }));
    expect(link.getState().packets).toBe(0);
  });

  it('runs command heartbeats from a worker when the browser provides one', () => {
    class FakeWorker {
      static instance: FakeWorker | null = null;
      onmessage: (() => void) | null = null;
      terminated = false;
      constructor() {
        FakeWorker.instance = this;
      }
      postMessage() {}
      terminate() {
        this.terminated = true;
      }
    }
    vi.stubGlobal('Worker', FakeWorker);
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    socket().open();
    void link.command('arm').catch(() => {});
    expect(socket().frames().filter((frame) => frame.type === 'ping')).toHaveLength(1);
    FakeWorker.instance!.onmessage!();
    expect(socket().frames().filter((frame) => frame.type === 'ping')).toHaveLength(2);
    link.disconnect();
    expect(FakeWorker.instance!.terminated).toBe(true);
  });
});

describe('ground station with a device link', () => {
  it('shows the motor command, the box drop counters and its notices', () => {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/']}>
        <App />
      </MemoryRouter>,
    );
    fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'ws://192.168.4.1/ws' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    const socket = FakeSocket.instances[0]!;
    act(() => {
      socket.open();
      socket.message(tlm({ t_ms: 0, psi_deg: 0, mode: 'MANUAL', throttle: 412, tau_cmd_mNm: 0.8, tlm_dropped: 3, dropped_events: 1 }));
      socket.message(JSON.stringify({ v: 1, type: 'event', seq: 2, t_ms: 5, event: 'notice', name: 'HALL_BYPASS', detail: 'on' }));
    });
    const rail = within(screen.getByRole('region', { name: 'TELEMETRY' }));
    expect(rail.getByText('MOTOR COMMAND')).toBeTruthy();
    expect(rail.getByText('412')).toBeTruthy();
    expect(rail.getByText(/TORQUE 0\.8 mN·m/)).toBeTruthy();
    expect(screen.getByText(/3 TLM SKIPPED BY THE BOX SINCE BOOT \(ALL CLIENTS\) · 1 EVENTS LOST BY THE BOX SINCE BOOT/)).toBeTruthy();
    expect(within(screen.getByRole('list', { name: 'Box notices' })).getByText(/NOTICE HALL BYPASS · ON/)).toBeTruthy();
    expect(screen.getByText(/1 PACKETS(?!.*IGNORED)/)).toBeTruthy();
  });

  it('switches every view to device telemetry and swaps in the box controls, still sending nothing', async () => {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/']}>
        <App />
      </MemoryRouter>,
    );
    fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'ws://192.168.4.1/ws' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    const socket = FakeSocket.instances[0]!;
    expect(socket.url).toBe('ws://192.168.4.1/ws');

    act(() => {
      socket.open();
      socket.message(tlm({ t_ms: 0, psi_deg: 47.2, psi_ref_deg: 47, rate_dps: 0.3, wheel_rpm: 2504, vbat: 10.1, mode: 'HOLD' }));
    });
    await act(nextFrame);
    expect(screen.getByText('DEVICE CONNECTED — RECEIVING TELEMETRY')).toBeTruthy();
    expect(screen.getByText('FROM DEVICE — WEBSOCKET')).toBeTruthy();
    expect(screen.getByText('+47.2°')).toBeTruthy();
    const rail = within(screen.getByRole('region', { name: 'TELEMETRY' }));
    expect(rail.getByText(/10\.10 V/)).toBeTruthy();
    const sensors = within(screen.getByRole('region', { name: 'SENSOR READINGS' }));
    expect(sensors.getByText('FROM DEVICE')).toBeTruthy();
    const toggle = sensors.getByRole('button', { name: /SENSOR READINGS/ });
    expect(toggle.getAttribute('aria-expanded')).toBe('false');
    fireEvent.click(toggle);
    expect(toggle.getAttribute('aria-expanded')).toBe('true');
    expect(sensors.getByText('10.10 V')).toBeTruthy();
    act(() => socket.message(tlm({ t_ms: 20, psi_deg: 47.3, mode: 'HOLD', vbat: 10.1 })));
    expect(toggle.getAttribute('aria-expanded')).toBe('true');
    expect(screen.getByRole('button', { name: /Rotate right ninety/ }).hasAttribute('disabled')).toBe(true);
    expect(screen.queryByRole('button', { name: /Reset the simulation/ })).toBeNull();
    expect(screen.getByRole('button', { name: 'Emergency stop the box' }).hasAttribute('disabled')).toBe(false);
    expect(screen.getByText('ANOTHER CLIENT IS CONTROLLING THE BOX. E-STOP STILL WORKS FROM HERE')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: 'Disconnect' }));
    expect(screen.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeTruthy();
    expect(socket.sent).toEqual([]);
  });

  it('shows device flags, the wheel reference and the throttle band; none of them in simulation', async () => {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/']}>
        <App />
      </MemoryRouter>,
    );
    const rail = () => screen.getByRole('region', { name: 'TELEMETRY' });
    expect(within(rail()).queryByRole('list', { name: 'Device flags' })).toBeNull();
    expect(within(rail()).queryByText('THROTTLE BAND')).toBeNull();

    fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'ws://192.168.4.1/ws' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    const socket = FakeSocket.instances[0]!;
    act(() => {
      socket.open();
      socket.message(
        tlm({
          t_ms: 0,
          psi_deg: 10,
          mode: 'MANUAL',
          wheel_rpm: 2380,
          wheel_ref_rpm: 2400,
          throttle_band: [200, 600],
          flags: ['MOMENTUM_OK', 'HALL_BYPASS', 'HEARTBEAT_LOST'],
        }),
      );
    });
    await act(nextFrame);
    const flags = within(rail()).getByRole('list', { name: 'Device flags' });
    expect(within(flags).getByText('MOMENTUM OK').className).toBe('flag flag--ok');
    expect(within(flags).getByText('HALL BYPASS').className).toBe('flag flag--warning');
    expect(within(flags).getByText('LATCHED HEARTBEAT LOST').className).toBe('flag flag--fault');
    expect(within(rail()).getByText('REF 2400 RPM')).toBeTruthy();
    expect(within(rail()).getByText('THROTTLE BAND')).toBeTruthy();
    expect(within(rail()).getByText('200–600')).toBeTruthy();

    await new Promise((resolve) => setTimeout(resolve, 60));
    act(() => socket.message(tlm({ t_ms: 100, psi_deg: 10, mode: 'READY', flags: [] })));
    await act(nextFrame);
    expect(within(rail()).queryByRole('list', { name: 'Device flags' })).toBeNull();
    expect(within(rail()).queryByText('THROTTLE BAND')).toBeNull();
    expect(socket.sent).toEqual([]);
  });

  it('rejects a non-WebSocket URL without opening anything', () => {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/']}>
        <App />
      </MemoryRouter>,
    );
    fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'http://192.168.4.1' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(FakeSocket.instances).toHaveLength(0);
    expect(screen.getByText('The URL must start with ws:// or wss://')).toBeTruthy();
  });
});
