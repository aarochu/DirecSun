import { act, fireEvent, render, screen, within } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { DeviceLink, flagSeverity, parseTelemetry, validateLinkUrl } from '../hardware/deviceLink';
import { FakeSocket, tlm } from './fakeSocket';


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
      headingDeg: 47.2,
      targetDeg: 47,
      rateDegS: 0.3,
      wheelRpm: 2504,
      wheelRefRpm: null,
      throttleBand: null,
      flags: [],
      batteryV: 10.1,
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
  it('marks latched interlocks as faults, MOMENTUM_OK as ok, and everything else as a warning', () => {
    for (const fault of ['ESTOP', 'HEARTBEAT_LOST', 'OVERSPEED', 'UNDERSPEED', 'BATTERY_LOW', 'IMU_SILENT', 'TILT', 'CONTROL_STALL', 'HALL_SILENT']) {
      expect(flagSeverity(fault)).toBe('fault');
    }
    expect(flagSeverity('MOMENTUM_OK')).toBe('ok');
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

  it('is read-only: connects, receives, never sends', () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    expect(link.getState().status).toBe('connecting');
    socket().open();
    expect(link.getState().status).toBe('open');
    expect(link.receiving).toBe(false);

    socket().message(tlm({ t_ms: 1000, psi_deg: 45, psi_ref_deg: 90, rate_dps: 12, wheel_rpm: 2300, mode: 'SLEW' }));
    expect(link.receiving).toBe(true);
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

  it('unwraps headings across 0°/360° for the model and chart', () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    socket().open();
    socket().message(tlm({ t_ms: 0, psi_deg: 358 }));
    socket().message(tlm({ t_ms: 100, psi_deg: 2 }));
    expect(link.getFrame().headingDeg).toBeCloseTo(362, 9);
    const { history } = link.getTelemetry();
    expect(history.map((s) => s.t)).toEqual([0, 0.1]);
  });

  it('does not reconnect after the socket drops', () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    socket().open();
    socket().message(tlm({ psi_deg: 5, mode: 'READY' }));
    expect(link.getTelemetry().statusWord).toBe('READY');
    socket().drop(1006);
    expect(link.getState().status).toBe('closed');
    expect(link.receiving).toBe(false);
    expect(link.latestReading).toBeNull();
    expect(link.getTelemetry().statusWord).toBe('—');
    expect(link.getTelemetry().history).toEqual([]);
    expect(FakeSocket.instances).toHaveLength(1);
  });

  it('disconnect closes the socket and ignores late events', () => {
    const { link, socket } = setup();
    link.connect('ws://device/ws');
    const first = socket();
    first.open();
    first.message(tlm({ psi_deg: 5, mode: 'READY' }));
    link.disconnect();
    expect(first.closedWith).toBe(1000);
    expect(link.getState().status).toBe('idle');
    expect(link.latestReading).toBeNull();
    expect(link.getTelemetry().statusWord).toBe('—');
    first.message(tlm({ psi_deg: 5 }));
    expect(link.getState().packets).toBe(1);
  });
});

describe('ground station with a device link', () => {
  it('switches every view to device telemetry and swaps in the box controls, still sending nothing', () => {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/ground-station']}>
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
    expect(screen.getByText('DEVICE CONNECTED — RECEIVING TELEMETRY')).toBeTruthy();
    expect(screen.getByText('FROM DEVICE — WEBSOCKET')).toBeTruthy();
    expect(screen.getByText('+47.2°')).toBeTruthy();
    expect(screen.getByText(/10\.10 V/)).toBeTruthy();
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
      <MemoryRouter initialEntries={['/ground-station']}>
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
    const flags = within(rail()).getByRole('list', { name: 'Device flags' });
    expect(within(flags).getByText('MOMENTUM OK').className).toBe('flag flag--ok');
    expect(within(flags).getByText('HALL BYPASS').className).toBe('flag flag--warning');
    expect(within(flags).getByText('LATCHED HEARTBEAT LOST').className).toBe('flag flag--fault');
    expect(within(rail()).getByText('REF 2400 RPM')).toBeTruthy();
    expect(within(rail()).getByText('THROTTLE BAND')).toBeTruthy();
    expect(within(rail()).getByText('200–600')).toBeTruthy();

    await new Promise((resolve) => setTimeout(resolve, 60));
    act(() => socket.message(tlm({ t_ms: 100, psi_deg: 10, mode: 'READY', flags: [] })));
    expect(within(rail()).queryByRole('list', { name: 'Device flags' })).toBeNull();
    expect(within(rail()).queryByText('THROTTLE BAND')).toBeNull();
    expect(socket.sent).toEqual([]);
  });

  it('rejects a non-WebSocket URL without opening anything', () => {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/ground-station']}>
        <App />
      </MemoryRouter>,
    );
    fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'http://192.168.4.1' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    expect(FakeSocket.instances).toHaveLength(0);
    expect(screen.getByText('The URL must start with ws:// or wss://')).toBeTruthy();
  });
});
