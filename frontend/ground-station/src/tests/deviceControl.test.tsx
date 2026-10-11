import { act, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { operatorDisplaySource } from '../ground/displaySource';
import { ACK_TIMEOUT_MS, DeviceCommandError, DeviceLink, PING_INTERVAL_MS } from '../hardware/deviceLink';
import { DeviceOperator, HOLD_MS, MANUAL_PERIOD_MS, SLEW_EVENT_WAIT_MS, SLEW_WAIT_MS } from '../hardware/deviceOperator';
import { ack, FakeSocket, modeEvent, tlm as rawTlm } from './fakeSocket';

const tlm = (fields: Record<string, unknown>) => rawTlm({ wheel_ref_rpm: fields.mode === 'READY' ? 2400 : 0, ...fields });

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

  it.each(['error', 'messageerror'] as const)('falls back to 5 Hz pings when the heartbeat Worker has a %s', (eventName) => {
    let worker!: WorkerStub;
    class WorkerStub {
      onmessage: (() => void) | null = null;
      onerror: (() => void) | null = null;
      onmessageerror: (() => void) | null = null;
      postMessage = vi.fn();
      terminate = vi.fn();
      constructor() { worker = this; }
    }
    vi.stubGlobal('Worker', WorkerStub);
    const { link, socket } = openLink();
    void link.command('arm').catch(() => {});
    expect(socket.frames().filter((frame) => frame.type === 'ping')).toHaveLength(1);
    worker[`on${eventName}`]?.();
    vi.advanceTimersByTime(PING_INTERVAL_MS * 5);
    expect(worker.terminate).toHaveBeenCalledOnce();
    expect(socket.frames().filter((frame) => frame.type === 'ping')).toHaveLength(7);
    link.disconnect();
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

  async function startTurn(socket: FakeSocket, operator: DeviceOperator, direction: 1 | -1) {
    const turn = operator.rotate(direction);
    expect(socket.lastCommand()).toMatchObject({ cmd: 'zero_heading' });
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    return { turn };
  }

  it('sends the chosen mode with its manual input, and reports the box answer', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);

    const hold = operator.setMode('HOLD');
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'HOLD' });
    expect(socket.lastCommand()).not.toHaveProperty('input');
    socket.message(ack(socket.lastCommand().seq));
    await hold;
    expect(operator.getState().result).toEqual({ tone: 'ok', text: 'MODE HOLD ACCEPTED.' });

    const manual = operator.setMode('MANUAL', 'rate');
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'MANUAL', input: 'rate' });
    socket.message(ack(socket.lastCommand().seq));
    await manual;

    const sun = operator.setMode('SUN_POINT');
    socket.message(ack(socket.lastCommand().seq, false, 'not implemented'));
    await sun;
    expect(operator.getState().result).toEqual({ tone: 'error', text: 'MODE REFUSED: not implemented. THE FIRMWARE DOES NOT RUN THIS YET' });
    expect(operator.getState().busy).toBeNull();
  });

  it('streams the manual setpoint every 100 ms while the box is in MANUAL, and stops when it leaves', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const manualFrames = () => socket.commands().filter((frame) => frame.cmd === 'manual');

    socket.message(tlm({ psi_deg: 10, mode: 'MANUAL', manual_input: 'rate' }));
    expect(operator.getState().manual).toMatchObject({ input: 'rate', value: 0, min: -90, max: 90 });
    await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS * 3);
    expect(manualFrames()).toHaveLength(3);
    expect(manualFrames().every((frame) => frame.rate_dps === 0 && Object.keys(frame).length === 5)).toBe(true);

    operator.setManual(30);
    operator.setManual(500);
    expect(operator.getState().manual?.value).toBe(90);
    operator.setManual(30);
    await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS);
    expect(manualFrames().at(-1)).toMatchObject({ cmd: 'manual', rate_dps: 30 });

    socket.message(tlm({ psi_deg: 10, mode: 'READY', manual_input: null }));
    expect(operator.getState().manual).toBeNull();
    const count = manualFrames().length;
    await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS * 5);
    expect(manualFrames()).toHaveLength(count);
  });

  it('uses the throttle band from the box, starts wheel input at the current speed, and e-stop stops the stream', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    socket.message(tlm({ psi_deg: 10, mode: 'MANUAL', manual_input: 'throttle', throttle_band: [150, 400] }));
    expect(operator.getState().manual).toMatchObject({ input: 'throttle', value: 150, min: 150, max: 400 });
    operator.setManual(1000);
    expect(operator.getState().manual?.value).toBe(400);
    await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS);
    expect(socket.commands().filter((frame) => frame.cmd === 'manual').at(-1)).toMatchObject({ throttle: 400 });

    operator.estop();
    expect(operator.getState().manual).toBeNull();
    const count = socket.commands().length;
    await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS * 5);
    expect(socket.commands()).toHaveLength(count);
  });

  it('does not restart the stream from a MANUAL frame that arrives after e-stop or disarm', async () => {
    for (const stop of ['estop', 'disarm'] as const) {
      const { socket, operator } = armed();
      await armAndReady(socket, operator);
      socket.message(tlm({ psi_deg: 10, mode: 'MANUAL', manual_input: 'throttle', throttle_band: [150, 400] }));
      await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS);

      if (stop === 'estop') operator.estop();
      else void operator.disarm();
      const count = socket.commands().filter((frame) => frame.cmd === 'manual').length;
      // The box's next frames still say MANUAL until its mode event lands.
      socket.message(tlm({ psi_deg: 10, mode: 'MANUAL', manual_input: 'throttle', throttle_band: [150, 400] }));
      await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS * 5);
      expect(operator.getState().manual).toBeNull();
      expect(socket.commands().filter((frame) => frame.cmd === 'manual')).toHaveLength(count);
      operator.detach();
    }
  });

  it('does not stream for throttle input when the box sends no throttle band', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    socket.message(tlm({ psi_deg: 10, mode: 'MANUAL', manual_input: 'throttle' }));
    expect(operator.getState().manual).toBeNull();
    await vi.advanceTimersByTimeAsync(MANUAL_PERIOD_MS * 3);
    expect(socket.commands().filter((frame) => frame.cmd === 'manual')).toEqual([]);
  });

  it('only sends a mode the box can enter from its current mode, and names automatic exits', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const hold = operator.setMode('HOLD');
    socket.message(ack(socket.lastCommand().seq));
    await hold;
    socket.message(tlm({ psi_deg: 10, psi_ref_deg: 10, mode: 'HOLD' }));
    const before = socket.commands().length;
    await operator.setMode('DETUMBLE');
    expect(socket.commands()).toHaveLength(before);

    const ready = operator.setMode('READY');
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'READY' });
    socket.message(ack(socket.lastCommand().seq));
    await ready;

    socket.message(modeEvent('READY', 'sun_invalid'));
    expect(operator.getState().result).toEqual({ tone: 'error', text: 'SUN POINT ENDED: THE SUN READING WAS INVALID FOR 0.5 S.' });
  });

  it('does not change mode unless it controls the box in a mode that allows it', async () => {
    const { socket, operator } = armed();
    await operator.setMode('HOLD');
    expect(socket.commands()).toEqual([]);

    await armAndReady(socket, operator);
    socket.message(tlm({ psi_deg: 10, mode: 'SPINUP' }));
    await operator.setMode('HOLD');
    expect(socket.commands().filter((frame) => frame.cmd === 'mode')).toEqual([]);
  });

  it('cannot rotate before it controls an armed box in READY', async () => {
    const { socket, operator } = armed();
    socket.message(tlm({ psi_deg: 10, mode: 'READY' }));
    await operator.rotate(1);
    expect(socket.commands()).toEqual([]);
  });

  it('does not send a turn when READY has no closed wheel reference', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    socket.message(tlm({ psi_deg: 10, mode: 'READY', wheel_ref_rpm: 0 }));
    expect(operator.canRotate).toBe(false);
    await operator.rotate(1);
    expect(socket.commands().map((command) => command.cmd)).toEqual(['arm']);

    socket.message(rawTlm({ psi_deg: 10, mode: 'READY' }));
    expect(operator.canRotate).toBe(false);
    socket.message(tlm({ psi_deg: 10, mode: 'READY', wheel_ref_rpm: 2400 }));
    expect(operator.canRotate).toBe(true);
  });

  it('zeros heading, runs right 90, holds 1.0 s, and SLEWs back to zero', async () => {
    const { socket, operator } = armed(10);
    await armAndReady(socket, operator, 10);
    expect(operator.getState().controlling).toBe(true);

    const turn = operator.rotate(1);
    expect(socket.lastCommand()).toMatchObject({ cmd: 'zero_heading' });
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(0);
    expect(operator.getState()).toMatchObject({ phase: 'rotating', originDeg: 0, targetDeg: 90 });

    socket.message(ack(socket.lastCommand().seq));
    await flush();
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'SLEW', psi_ref_deg: 90 });
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(tlm({ psi_deg: 10, mode: 'READY' }));
    await flush();
    expect(operator.getState().phase).toBe('rotating');
    socket.message(tlm({ psi_deg: 60, mode: 'SLEW' }));
    socket.message(tlm({ psi_deg: 90, mode: 'READY' }));
    await flush();
    expect(operator.getState().phase).toBe('rotating');
    socket.message(modeEvent('READY', 'slew_complete'));
    await flush();
    expect(operator.getState().phase).toBe('holding');
    const commandsDuringHold = socket.commands().length;
    await vi.advanceTimersByTimeAsync(HOLD_MS - 10);
    expect(socket.commands()).toHaveLength(commandsDuringHold);
    await vi.advanceTimersByTimeAsync(10);
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'SLEW', psi_ref_deg: 0 });
    expect(operator.getState().phase).toBe('returning');

    await finishSlew(socket, 0);
    await turn;
    expect(operator.getState()).toMatchObject({ phase: 'idle', result: { tone: 'ok' } });
    expect(socket.commands().map((command) => command.cmd)).toEqual(['arm', 'zero_heading', 'mode', 'mode']);
  });

  it('uses zero-based targets regardless of the old heading, with a negative step for left', async () => {
    const { socket, operator } = armed(-111.06);
    await armAndReady(socket, operator, -111.06);
    void startTurn(socket, operator, 1);
    await flush();
    expect(socket.lastCommand().psi_ref_deg).toBe(90);
    const second = armed(90);
    await armAndReady(second.socket, second.operator, 90);
    void startTurn(second.socket, second.operator, -1);
    await flush();
    expect(second.socket.lastCommand().psi_ref_deg).toBe(-90);
    const third = armed(-90);
    await armAndReady(third.socket, third.operator, -90);
    void startTurn(third.socket, third.operator, 1);
    await flush();
    expect(third.socket.lastCommand().psi_ref_deg).toBe(90);
  });

  it('does not SLEW if zero_heading is refused or the turn is stopped before its ack', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    let turn = operator.rotate(1);
    socket.message(ack(socket.lastCommand().seq, false, 'heading unavailable'));
    await turn;
    expect(operator.getState().result).toMatchObject({ tone: 'error', text: expect.stringContaining('heading unavailable') });
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(0);

    turn = operator.rotate(-1);
    const zeroSeq = socket.lastCommand().seq;
    await operator.stopTurn();
    socket.message(ack(zeroSeq));
    await turn;
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(0);
    expect(operator.getState().result?.text).toBe('TURN STOPPED: HOLDING WHERE THE BOX IS.');
  });

  it('sends READY when Stop races the SLEW acknowledgement', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const { turn } = await startTurn(socket, operator, 1);
    const slewSeq = socket.lastCommand().seq;
    const stopping = operator.stopTurn();
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'READY' });
    socket.message(ack(slewSeq));
    socket.message(ack(socket.lastCommand().seq));
    await stopping;
    await turn;
    expect(operator.getState().result?.text).toBe('TURN STOPPED: HOLDING WHERE THE BOX IS.');
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(1);
  });

  it('Stop turn cancels the sequence and sends mode READY while the box is slewing', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const { turn } = await startTurn(socket, operator, 1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
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

  it('trusts the completion event during hold even if a stale SLEW telemetry frame follows it', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const { turn } = await startTurn(socket, operator, 1);
    await finishSlew(socket, 90);
    expect(operator.getState().phase).toBe('holding');
    socket.message(tlm({ psi_deg: 90, mode: 'SLEW' }));
    await vi.advanceTimersByTimeAsync(HOLD_MS);
    expect(operator.getState().phase).toBe('returning');
    await finishSlew(socket, 0);
    await turn;
    expect(operator.getState().result).toMatchObject({ tone: 'ok' });
  });

  it('keeps the operator target visible instead of the moving device profile', async () => {
    const { link, socket, operator } = armed();
    const source = operatorDisplaySource(link, operator);
    await armAndReady(socket, operator);
    const turn = operator.rotate(1);
    const zeroSeq = socket.lastCommand().seq;
    expect(source.getTelemetry().targetDeg).toBe(90);
    expect(source.getFrame().targetDeg).toBe(90);
    operator.estop();
    socket.message(ack(socket.lastCommand().seq));
    socket.message(ack(zeroSeq));
    await turn;
  });

  it('e-stop aborts the turn, and no return SLEW follows', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    const { turn } = await startTurn(socket, operator, 1);
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    operator.estop();
    expect(socket.lastCommand()).toMatchObject({ cmd: 'estop' });
    await turn;
    await vi.advanceTimersByTimeAsync(SLEW_WAIT_MS * 2);
    expect(socket.commands().filter((command) => command.mode === 'SLEW')).toHaveLength(1);
    expect(operator.getState().phase).toBe('idle');
  });

  it('presents its own accepted e-stop as a success, not a box fault', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);
    operator.estop();
    socket.message(ack(socket.lastCommand().seq));
    await flush();
    socket.message(modeEvent('SAFE', 'ESTOP'));
    expect(operator.getState().result).toEqual({ tone: 'ok', text: 'E-STOP ACCEPTED: THE WHEEL RAMPS DOWN.' });
  });

  it('stops on a refused SLEW, on the box SLEW_TIMEOUT, and when the box goes SAFE', async () => {
    const { socket, operator } = armed();
    await armAndReady(socket, operator);

    let { turn } = await startTurn(socket, operator, 1);
    socket.message(ack(socket.lastCommand().seq, false, 'magnetometer not OK'));
    await turn;
    expect(operator.getState().result).toMatchObject({ tone: 'error', text: expect.stringContaining('magnetometer not OK') });

    ({ turn } = await startTurn(socket, operator, 1));
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
    ({ turn } = await startTurn(socket, operator, -1));
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
    const { turn } = await startTurn(socket, operator, 1);
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
    const { turn } = await startTurn(socket, operator, 1);
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
      <MemoryRouter initialEntries={['/']}>
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

  async function armFromPage(socket: FakeSocket) {
    fireEvent.click(screen.getByRole('button', { name: 'Arm the box' }));
    fireEvent.click(screen.getByRole('checkbox'));
    fireEvent.click(screen.getByRole('button', { name: 'Arm now' }));
    await act(async () => {
      socket.message(ack(socket.lastCommand().seq));
      await flush();
    });
    act(() => {
      socket.message(tlm({ t_ms: 100, psi_deg: 0, mode: 'SPINUP' }));
      socket.message(tlm({ t_ms: 200, psi_deg: 0, mode: 'READY' }));
    });
  }

  it('disables Rotate and explains an unavailable wheel loop before a turn', async () => {
    const socket = connect();
    await armFromPage(socket);
    const rotate = screen.getByRole('button', { name: 'Rotate right ninety degrees and return' });
    expect(rotate.hasAttribute('disabled')).toBe(false);

    act(() => socket.message(tlm({ psi_deg: 0, mode: 'READY', wheel_ref_rpm: 0, flags: ['HALL_BYPASS'] })));
    expect(rotate.hasAttribute('disabled')).toBe(true);
    expect(screen.getByText(/HALL SENSOR IS BYPASSED/)).toBeTruthy();
    fireEvent.click(rotate);
    expect(socket.commands().map((command) => command.cmd)).toEqual(['arm']);

    act(() => socket.message(rawTlm({ psi_deg: 0, mode: 'READY' })));
    expect(rotate.hasAttribute('disabled')).toBe(true);
    expect(screen.getByText(/BOX MAY NEED CHARACTERISATION/)).toBeTruthy();

    act(() => socket.message(tlm({ psi_deg: 0, mode: 'READY', wheel_ref_rpm: 2400, flags: [] })));
    expect(rotate.hasAttribute('disabled')).toBe(false);
  });

  it('warns after ten seconds in SPINUP without commanding the box', async () => {
    const socket = connect();
    fireEvent.click(screen.getByRole('button', { name: 'Arm the box' }));
    fireEvent.click(screen.getByRole('checkbox'));
    fireEvent.click(screen.getByRole('button', { name: 'Arm now' }));
    await act(async () => {
      socket.message(ack(socket.lastCommand().seq));
      await flush();
    });
    act(() => socket.message(tlm({ psi_deg: 0, mode: 'SPINUP' })));
    await act(async () => vi.advanceTimersByTimeAsync(10_000));
    expect(screen.getByText(/SPIN-UP IS TAKING LONGER THAN EXPECTED/)).toBeTruthy();
    expect(screen.getByText(/DISARM AND ASK JULIAN/)).toBeTruthy();
    expect(socket.commands().map((command) => command.cmd)).toEqual(['arm']);
  });

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

  it('the mode menu sends the chosen mode, and stays disabled until the box is armed', async () => {
    const socket = connect();
    const trigger = () => screen.getByRole('button', { name: 'Set the box mode' });
    expect(trigger().hasAttribute('disabled')).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: 'Arm the box' }));
    fireEvent.click(screen.getByRole('checkbox'));
    fireEvent.click(screen.getByRole('button', { name: 'Arm now' }));
    await act(async () => {
      socket.message(ack(socket.lastCommand().seq));
      await flush();
    });
    await act(() => vi.advanceTimersByTimeAsync(60));
    act(() => socket.message(tlm({ t_ms: 100, psi_deg: 0, mode: 'READY' })));
    expect(trigger().hasAttribute('disabled')).toBe(false);

    fireEvent.click(trigger());
    expect(trigger().getAttribute('aria-expanded')).toBe('true');
    const items = screen.getAllByRole('menuitem').map((item) => item.getAttribute('aria-label'));
    expect(items).toEqual(['Ready', 'Hold heading', 'Manual: rate', 'Manual: throttle', 'Manual: wheel speed', 'Detumble', 'Sun point']);

    fireEvent.click(screen.getByRole('menuitem', { name: 'Manual: throttle' }));
    expect(socket.lastCommand()).toMatchObject({ cmd: 'mode', mode: 'MANUAL', input: 'throttle' });
    expect(trigger().getAttribute('aria-expanded')).toBe('false');
    await act(async () => {
      socket.message(ack(socket.lastCommand().seq));
      await flush();
    });
    expect(screen.getByText('MODE MANUAL (THROTTLE) ACCEPTED.')).toBeTruthy();

    await act(() => vi.advanceTimersByTimeAsync(60));
    act(() => socket.message(tlm({ t_ms: 200, psi_deg: 0, mode: 'MANUAL' })));
    fireEvent.click(trigger());
    const enabled = screen.getAllByRole('menuitem').filter((item) => !(item as HTMLButtonElement).disabled).map((item) => item.getAttribute('aria-label'));
    expect(enabled).toEqual(['Ready']);
    expect(screen.getAllByText('Firmware pending')).toHaveLength(2);
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' });
    expect(trigger().getAttribute('aria-expanded')).toBe('false');
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

  it('keeps an armed-link-loss warning visible instead of swapping to simulation controls', async () => {
    const socket = connect();
    await armFromPage(socket);
    act(() => socket.drop(1006));
    expect(screen.getByText('DEVICE LINK CLOSED — TELEMETRY FROZEN')).toBeTruthy();
    expect(screen.getByText(/LINK CLOSED: AN ARMED BOX RAMPS DOWN/)).toBeTruthy();
    expect(screen.queryByRole('button', { name: /Reset the simulation/ })).toBeNull();
  });

  it('does not flash another-client guidance when its own e-stop makes the box SAFE', async () => {
    const socket = connect();
    await armFromPage(socket);
    fireEvent.click(screen.getByRole('button', { name: 'Emergency stop the box' }));
    await act(async () => {
      socket.message(ack(socket.lastCommand().seq));
      socket.message(modeEvent('SAFE', 'ESTOP'));
      socket.message(tlm({ t_ms: 300, psi_deg: 0, mode: 'SAFE' }));
      await flush();
    });
    expect(screen.queryByText(/ANOTHER CLIENT/)).toBeNull();
    expect(screen.getByText('E-STOP ACCEPTED: THE WHEEL RAMPS DOWN.')).toBeTruthy();
  });
});
