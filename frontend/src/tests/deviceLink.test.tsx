import { act, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { DeviceLink, parseTelemetry, validateLinkUrl } from '../hardware/deviceLink';

class FakeSocket {
  static instances: FakeSocket[] = [];
  readyState = 0;
  sent: unknown[] = [];
  closedWith: number | null = null;
  onopen: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;

  constructor(public url: string) {
    FakeSocket.instances.push(this);
  }
  send(data: unknown) {
    this.sent.push(data);
  }
  close(code?: number) {
    this.closedWith = code ?? 1000;
    this.readyState = 3;
  }
  open() {
    this.readyState = 1;
    this.onopen?.(new Event('open'));
  }
  message(data: unknown) {
    this.onmessage?.({ data } as MessageEvent);
  }
  drop(code = 1006) {
    this.readyState = 3;
    this.onclose?.({ code } as CloseEvent);
  }
}

const tlm = (fields: Record<string, unknown>) => JSON.stringify({ v: 1, type: 'tlm', ...fields });

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
      batteryV: 10.1,
    });
  });

  it('tolerates missing optional fields', () => {
    expect(parseTelemetry(tlm({ psi_deg: 10 }))).toMatchObject({ headingDeg: 10, wheelRpm: null, batteryV: null });
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
});

describe('ground station with a device link', () => {
  it('switches every view to device telemetry and locks the simulation controls', () => {
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
    expect(screen.getByRole('button', { name: /Reset the simulation/ }).hasAttribute('disabled')).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: 'Disconnect' }));
    expect(screen.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeTruthy();
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
