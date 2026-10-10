import { act, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { ACK_TIMEOUT_MS, DeviceCommandError, DeviceLink, PING_INTERVAL_MS } from '../hardware/deviceLink';
import { DeviceOperator, HOLD_MS, SLEW_EVENT_WAIT_MS, SLEW_WAIT_MS } from '../hardware/deviceOperator';
import { ack, FakeSocket, modeEvent, tlm } from './fakeSocket';

const flush = () => vi.advanceTimersByTimeAsync(0);

beforeEach(() => {
  vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout', 'setInterval', 'clearInterval', 'performance'] });
});

afterEach(() => {
  vi.useRealTimers();
  FakeSocket.instances = [];
  vi.unstubAllGlobals();
});

function openLink() {
  let now = 0;
  const link = new DeviceLink((url) => new FakeSocket(url) as unknown as WebSocket, () => (now += 5));
  link.connect('ws://box/ws');
  const socket = FakeSocket.instances.at(-1)!;
  socket.open();
  return { link, socket };
}

describe('DeviceLink commands', () => {
  it('stays silent until the first command, then pings at 5 Hz', () => {
    const { link, socket } = openLink();
    socket.message(tlm({ psi_deg: 0, mode: 'SAFE' }));
    vi.advanceTimersByTime(2000);
    expect(socket.sent).toEqual([]);
    expect(link.getState().talking).toBe(false);

    void link.command('arm').catch(() => {});
    expect(socket.frames().map((frame) => frame.type)).toEqual(['ping', 'cmd']);
    expect(link.getState().talking).toBe(true);
    vi.advanceTimersByTime(PING_INTERVAL_MS * 5);
    expect(socket.frames().filter((frame) => frame.type === 'ping')).toHaveLength(6);
  });

  it('resolves a command on the ack with its seq and ignores acks for other seqs', async () => {
    const { link, socket } = openLink();
    let settled: unknown = null;
    void link.command('arm').then((value) => (settled = value));
    const { seq } = socket.lastCommand();
    expect(socket.lastCommand()).toEqual({ v: 1, type: 'cmd', seq, cmd: 'arm' });
    socket.message(ack((seq as number) + 1000));
    await flush();
    expect(settled).toBeNull();
    socket.message(ack(seq));
    await flush();
    expect(settled).toEqual({ seq, ok: true, reason: null });
    expect(link.getState().ignored).toBe(0);
  });

  it('rejects with the box reason, after the ack timeout, and when the link closes', async () => {
    const { link, socket } = openLink();
    const refused = link.command('arm').catch((error: DeviceCommandError) => error);
    socket.message(ack(socket.lastCommand().seq, false, 'box moving'));
    expect(await refused).toMatchObject({ code: 'REFUSED', reason: 'box moving', command: 'arm' });

    const late = link.command('disarm').catch((error: DeviceCommandError) => error);
    await vi.advanceTimersByTimeAsync(ACK_TIMEOUT_MS);
    expect(await late).toMatchObject({ code: 'TIMEOUT', command: 'disarm' });

    const cut = link.command('mode', { mode: 'READY' }).catch((error: DeviceCommandError) => error);
    socket.drop();
    expect(await cut).toMatchObject({ code: 'CLOSED', command: 'mode' });
    const sentAtDrop = socket.sent.length;
    vi.advanceTimersByTime(2000);
    expect(socket.sent).toHaveLength(sentAtDrop);
    expect(FakeSocket.instances).toHaveLength(1);
  });

  it('sends e-stop at once even with a command pending, and throws when the link is not open', () => {
    const { link, socket } = openLink();
    void link.command('arm').catch(() => {});
    void link.estop().catch(() => {});
    expect(socket.lastCommand()).toMatchObject({ cmd: 'estop' });

    link.disconnect();
    expect(() => link.estop()).toThrow(DeviceCommandError);
  });

  it('measures round-trip time from pongs and reads events without counting them as ignored', () => {
    const { link, socket } = openLink();
    void link.command('arm').catch(() => {});
    const ping = socket.frames().find((frame) => frame.type === 'ping')!;
    socket.message(JSON.stringify({ v: 1, type: 'pong', seq: 5, ping_seq: ping.seq, t_client_ms: ping.t_client_ms, t_ms: 10 }));
    expect(link.getState().rttMs).not.toBeNull();
    socket.message(modeEvent('SPINUP', 'arm'));
    expect(link.getState().lastEvent).toMatchObject({ event: 'mode', mode: 'SPINUP', cause: 'arm' });
    socket.message(JSON.stringify({ v: 2, type: 'ack', seq: 1, ok: true }));
    expect(link.getState().ignored).toBe(1);
  });
});

describe('DeviceOperator', () => {
  function armed(headingDeg = 10) {
    const { link, socket } = openLink();
    const operator = new DeviceOperator(link);
    operator.attach();
    socket.message(tlm({ psi_deg: headingDeg, mode: 'SAFE' }));
    return { link, socket, operator };
  }

  async function armAndReady(socket: FakeSocket, operator: DeviceOperator, headingDeg = 10) {
    const arming = operator.arm();
    socket.message(ack(socket.lastCommand().seq));
    await arming;
    socket.message(tlm({ psi_deg: headingDeg, mode: 'SPINUP' }));
    socket.message(tlm({ psi_deg: headingDeg, mode: 'READY' }));
  }

  async function finishSlew(socket: FakeSocket, headingDeg: number) {
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: headingDeg, mode: 'SLEW' }));
    socket.message(tlm({ psi_deg: headingDeg, mode: 'READY' }));
    socket.message(modeEvent('READY', 'slew_complete'));
    await flush();
  }

  it('cannot rotate before it controls an armed box in READY', async () => {
    const { socket, operator } = armed();
    socket.message(tlm({ psi_deg: 10, mode: 'READY' }));
    await operator.rotate(1);
    expect(socket.commands()).toEqual([]);
  });

  it('runs right 90: SLEW to start + 90, hold 1.0 s, SLEW back to the captured start', async () => {
    const { socket, operator } = armed(10);
    await armAndReady(socket, operator, 10);
    expect(operator.getState().controlling).toBe(true);

    const turn = operator.rotate(1);
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'SLEW', psi_ref_deg: 100 });
    expect(operator.getState()).toMatchObject({ phase: 'rotating', originDeg: 10, targetDeg: 100 });

    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 10, mode: 'READY' }));
    await flush();
    expect(operator.getState().phase).toBe('rotating');
    socket.message(tlm({ psi_deg: 60, mode: 'SLEW' }));
    socket.message(tlm({ psi_deg: 100, mode: 'READY' }));
    await flush();
    expect(operator.getState().phase).toBe('rotating');
    socket.message(modeEvent('READY', 'slew_complete'));
    await flush();
    expect(operator.getState().phase).toBe('holding');
    const commandsDuringHold = socket.commands().length;
    await vi.advanceTimersByTimeAsync(HOLD_MS - 10);
    expect(socket.commands()).toHaveLength(commandsDuringHold);
    await vi.advanceTimersByTimeAsync(10);
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'SLEW', psi_ref_deg: 10 });
    expect(operator.getState().phase).toBe('returning');

    await finishSlew(socket, 10);
    await turn;
    expect(operator.getState()).toMatchObject({ phase: 'idle', result: { tone: 'ok' } });
  });

  it('sends clean two-decimal targets, never float noise or -180', async () => {
    const { socket, operator } = armed(-111.06);
    await armAndReady(socket, operator, -111.06);
    void operator.rotate(1);
    expect(socket.lastCommand().psi_ref_deg).toBe(-21.06);
    const second = armed(90);
    await armAndReady(second.socket, second.operator, 90);
    void second.operator.rotate(1);
    expect(second.socket.lastCommand().psi_ref_deg).toBe(180);
    const third = armed(-90);
    await armAndReady(third.socket, third.operator, -90);
    void third.operator.rotate(-1);
    expect(third.socket.lastCommand().psi_ref_deg).toBe(180);
  });

  it('wraps the target into (-180, 180] and turns left with a negative step', async () => {
    const { socket, operator } = armed(-120);
    await armAndReady(socket, operator, -120);
    void operator.rotate(-1);
    expect(socket.lastCommand()).toMatchObject({ psi_ref_deg: 150 });
  });

  it('Stop turn cancels the sequence and sends mode READY while the box is slewing', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 40, mode: 'SLEW' }));
    const stopping = operator.stopTurn();
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'READY' });
    socket.message(ack(socket.lastCommand().seq));
    await stopping;
    await turn;
    expect(operator.getState().phase).toBe('idle');
    const count = socket.commands().length;
    await vi.advanceTimersByTimeAsync(HOLD_MS * 3);
    expect(socket.commands()).toHaveLength(count);
  });

  it('e-stop aborts the turn, and no return SLEW follows', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    operator.estop();
    expect(socket.lastCommand()).toMatchObject({ cmd: 'estop' });
    await turn;
    await vi.advanceTimersByTimeAsync(SLEW_WAIT_MS * 2);
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(1);
    expect(operator.getState().phase).toBe('idle');
  });

  it('stops on a refused SLEW, on the box SLEW_TIMEOUT, and when the box goes SAFE', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);

    let turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq, false, 'magnetometer not OK'));
    await turn;
    expect(operator.getState().result).toMatchObject({ tone: 'error', text: expect.stringContaining('magnetometer not OK') });

    turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 30, mode: 'SLEW' }));
    socket.message(tlm({ psi_deg: 30, mode: 'READY' }));
    await flush();
    expect(operator.getState().phase).toBe('rotating');
    socket.message(modeEvent('READY', 'SLEW_TIMEOUT'));
    await turn;
    expect(operator.getState().result!.text).toContain('TIMED OUT');
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(2);

    socket.message(tlm({ psi_deg: 30, mode: 'READY' }));
    turn = operator.rotate(-1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 30, mode: 'SLEW' }));
    socket.message(modeEvent('SAFE', 'HEARTBEAT_LOST'));
    socket.message(tlm({ psi_deg: 30, mode: 'SAFE' }));
    await turn;
    expect(operator.getState()).toMatchObject({ controlling: false, phase: 'idle' });
  });

  it('fails if READY telemetry arrives after SLEW but no completion event follows', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 30, mode: 'SLEW' }));
    socket.message(tlm({ psi_deg: 30, mode: 'READY' }));
    await flush();
    expect(operator.getState().phase).toBe('rotating');
    await vi.advanceTimersByTimeAsync(SLEW_EVENT_WAIT_MS);
    await turn;
    expect(operator.getState().result).toMatchObject({ tone: 'error', text: expect.stringContaining('WITHOUT A SLEW COMPLETION EVENT') });
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(1);
  });

  it('gives up when the slew never finishes', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 30, mode: 'SLEW' }));
    await vi.advanceTimersByTimeAsync(SLEW_WAIT_MS);
    await turn;
    expect(operator.getState().result!.text).toContain('DID NOT FINISH');
  });

  it('drops control when the link closes and never reconnects', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    socket.drop();
    expect(operator.getState()).toMatchObject({ controlling: false, result: { tone: 'error' } });
    expect(FakeSocket.instances).toHaveLength(1);
  });
});

describe('ground station box controls', () => {
  function connect() {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/ground-station']}>
        <App />
      </MemoryRouter>,
    );
    fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'ws://127.0.0.1:8081/ws' } });
    fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
    const socket = FakeSocket.instances[0]!;
    act(() => {
      socket.open();
      socket.message(tlm({ t_ms: 0, psi_deg: 0, mode: 'SAFE' }));
    });
    return socket;
  }

  it('asks to confirm the lock pin before it sends arm', async () => {
    const socket = connect();
    const armNow = () => screen.getByRole('button', { name: 'Arm now' });
    const confirmGroup = () => document.querySelector<HTMLElement>('[aria-label="Confirm arming"]')!;
    expect(confirmGroup().hidden).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: 'Arm the box' }));
    expect(screen.getByRole('group', { name: 'Confirm arming' }).hidden).toBe(false);
    expect(armNow().hasAttribute('disabled')).toBe(true);
    expect(socket.sent).toEqual([]);

    fireEvent.click(screen.getByRole('checkbox'));
    fireEvent.click(armNow());
    expect(socket.lastCommand()).toMatchObject({ cmd: 'arm' });
    await act(async () => {
      socket.message(ack(socket.lastCommand().seq));
      await flush();
    });
    expect(screen.getByText(/ARM ACCEPTED/)).toBeTruthy();
  });

  it('the E-stop button sends estop; the space bar does too, only while the box is armed', async () => {
    const socket = connect();
    fireEvent.keyDown(window, { code: 'Space', key: ' ' });
    expect(socket.commands()).toEqual([]);

    fireEvent.click(screen.getByRole('button', { name: 'Emergency stop the box' }));
    expect(socket.lastCommand()).toMatchObject({ cmd: 'estop' });

    act(() => socket.message(tlm({ t_ms: 100, psi_deg: 0, mode: 'SPINUP' })));
    await act(() => vi.advanceTimersByTimeAsync(60));
    act(() => socket.message(tlm({ t_ms: 200, psi_deg: 0, mode: 'SPINUP' })));
    const before = socket.commands().length;
    fireEvent.keyDown(window, { code: 'Space', key: ' ' });
    expect(socket.commands()).toHaveLength(before + 1);
    expect(socket.lastCommand()).toMatchObject({ cmd: 'estop' });
  });
});
