import { normalizeDeg } from '../simulation/physics';
import type { Direction } from '../simulation/types';
import { DeviceCommandError, type DeviceEvent, type DeviceLink } from './deviceLink';

export type TurnPhase = 'idle' | 'rotating' | 'holding' | 'returning';

export interface OperatorResult {
  readonly tone: 'ok' | 'error';
  readonly text: string;
}

export interface OperatorState {
  readonly controlling: boolean;
  readonly phase: TurnPhase;
  readonly direction: Direction | null;
  readonly originDeg: number | null;
  readonly targetDeg: number | null;
  readonly busy: 'arm' | 'disarm' | 'estop' | 'mode' | null;
  readonly result: OperatorResult | null;
}

export type CommandableMode = 'READY' | 'HOLD' | 'MANUAL' | 'DETUMBLE' | 'SUN_POINT';
export type ManualInput = 'rate' | 'throttle' | 'wheel';

const MODE_CHANGE_FROM = ['READY', 'HOLD', 'SLEW', 'MANUAL', 'DETUMBLE', 'SUN_POINT'];

export const TURN_STEP_DEG = 90;
export const HOLD_MS = 1000;
export const SLEW_WAIT_MS = 10000;
export const SLEW_EVENT_WAIT_MS = 1000;

type WatchVerdict = 'continue' | 'done' | string | { waitMs: number; timeoutMessage: string };

const IDLE: OperatorState = {
  controlling: false,
  phase: 'idle',
  direction: null,
  originDeg: null,
  targetDeg: null,
  busy: null,
  result: null,
};

const wireAngle = (deg: number) => {
  const rounded = Math.round(normalizeDeg(deg) * 100) / 100;
  if (rounded <= -180) return 180;
  return rounded === 0 ? 0 : rounded;
};

class TurnAborted extends Error {}

const describe = (error: unknown): string => {
  if (error instanceof DeviceCommandError) {
    if (error.code === 'REFUSED') return `${error.command.toUpperCase()} REFUSED: ${error.reason ?? 'no reason given'}`;
    if (error.code === 'TIMEOUT') return `${error.command.toUpperCase()}: NO ANSWER FROM THE BOX`;
    return `${error.command.toUpperCase()} NOT SENT: THE LINK IS NOT OPEN`;
  }
  return error instanceof Error ? error.message : String(error);
};

export class DeviceOperator {
  private state: OperatorState = IDLE;
  private readonly listeners = new Set<() => void>();
  private armedSeen = false;
  private turnToken = 0;
  private cancelWait: ((reason: string) => void) | null = null;
  private unsubscribe: (() => void)[] = [];

  constructor(private readonly link: DeviceLink) {}

  attach(): void {
    if (this.unsubscribe.length) return;
    this.unsubscribe = [
      this.link.subscribeFrame(this.onFrame),
      this.link.subscribeState(this.onLinkState),
      this.link.subscribeEvents(this.onEvent),
    ];
  }

  detach(): void {
    this.abortTurn('Closed');
    for (const unsubscribe of this.unsubscribe) unsubscribe();
    this.unsubscribe = [];
  }

  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getState = (): OperatorState => this.state;

  get boxMode(): string | null {
    return this.link.latestReading?.mode ?? null;
  }

  get canRotate(): boolean {
    return this.state.controlling && this.state.phase === 'idle' && this.boxMode === 'READY';
  }

  get canChangeMode(): boolean {
    return this.state.controlling && this.state.phase === 'idle' && this.state.busy === null && MODE_CHANGE_FROM.includes(this.boxMode ?? '');
  }

  async setMode(mode: CommandableMode, input?: ManualInput): Promise<void> {
    if (!this.canChangeMode) return;
    const label = input ? `${mode} (${input.toUpperCase()})` : mode.replace('_', ' ');
    this.set({ busy: 'mode', result: null });
    try {
      await this.link.command('mode', input ? { mode, input } : { mode });
      this.set({ busy: null, result: { tone: 'ok', text: `MODE ${label} ACCEPTED.` } });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  async arm(): Promise<void> {
    if (this.state.busy) return;
    this.set({ busy: 'arm', result: null });
    try {
      await this.link.command('arm');
      this.armedSeen = false;
      this.set({ busy: null, controlling: true, result: { tone: 'ok', text: 'ARM ACCEPTED: SPINNING UP. KEEP THE LOCK PIN IN.' } });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  async disarm(): Promise<void> {
    if (this.state.busy) return;
    this.abortTurn('Disarm requested');
    this.set({ busy: 'disarm', result: null });
    try {
      await this.link.command('disarm');
      this.set({ busy: null, result: { tone: 'ok', text: 'DISARM ACCEPTED: THE WHEEL RAMPS DOWN.' } });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  estop(): void {
    this.abortTurn('E-stop sent');
    let sent: Promise<unknown>;
    try {
      sent = this.link.estop();
    } catch (error) {
      this.set({ result: { tone: 'error', text: describe(error) } });
      return;
    }
    this.set({ busy: 'estop', result: { tone: 'ok', text: 'E-STOP SENT.' } });
    sent.then(
      () => this.set({ busy: null, result: { tone: 'ok', text: 'E-STOP ACCEPTED: THE WHEEL RAMPS DOWN.' } }),
      (error: unknown) => this.set({ busy: null, result: { tone: 'error', text: describe(error) } }),
    );
  }

  async rotate(direction: Direction): Promise<void> {
    if (!this.canRotate) return;
    const token = ++this.turnToken;
    const origin = 0;
    const target = wireAngle(direction * TURN_STEP_DEG);
    this.set({ phase: 'rotating', direction, originDeg: origin, targetDeg: target, result: null });
    try {
      await this.link.command('zero_heading');
      if (token !== this.turnToken) throw new TurnAborted('Superseded');
      await this.slewTo(target, token);
      this.set({ phase: 'holding' });
      await this.watch(token, HOLD_MS, (mode) => (mode === 'READY' ? 'continue' : `THE BOX LEFT READY (${mode})`));
      this.set({ phase: 'returning', targetDeg: origin });
      await this.slewTo(origin, token);
      this.finishTurn(token, { tone: 'ok', text: 'TURN COMPLETE: BACK AT THE START HEADING.' });
    } catch (error) {
      if (error instanceof TurnAborted && token !== this.turnToken) return;
      this.finishTurn(token, { tone: 'error', text: `TURN STOPPED: ${describe(error)}` });
    }
  }

  async stopTurn(): Promise<void> {
    const wasTurning = this.state.phase !== 'idle';
    this.abortTurn('Stop requested');
    if (this.boxMode !== 'SLEW' && !wasTurning) return;
    this.set({ result: { tone: 'ok', text: 'TURN STOPPED: HOLDING WHERE THE BOX IS.' } });
    if (this.boxMode !== 'SLEW') return;
    try {
      await this.link.command('mode', { mode: 'READY' });
    } catch (error) {
      this.set({ result: { tone: 'error', text: describe(error) } });
    }
  }

  private async slewTo(targetDeg: number, token: number) {
    await this.link.command('mode', { mode: 'SLEW', psi_ref_deg: targetDeg });
    if (token !== this.turnToken) throw new TurnAborted('Superseded');
    let seenSlew = false;
    await this.watch(token, SLEW_WAIT_MS, (mode, event) => {
      if (event?.event === 'mode' && event.cause === 'SLEW_TIMEOUT') return 'THE BOX TIMED OUT THE SLEW';
      if (event?.event === 'mode' && event.mode === 'READY' && event.cause === 'slew_complete') return 'done';
      if (event?.event === 'mode' && event.mode === 'READY') return `THE BOX ENDED THE SLEW (${event.cause ?? 'NO CAUSE'})`;
      if (mode === 'SLEW') {
        seenSlew = true;
        return 'continue';
      }
      if (mode === 'READY') return seenSlew
        ? { waitMs: SLEW_EVENT_WAIT_MS, timeoutMessage: 'THE BOX RETURNED READY WITHOUT A SLEW COMPLETION EVENT' }
        : 'continue';
      return `THE BOX LEFT THE SLEW (${mode ?? 'NO MODE'})`;
    }, 'THE SLEW DID NOT FINISH IN TIME');
  }

  private watch(
    token: number,
    timeoutMs: number,
    check: (mode: string | null, event: DeviceEvent | null) => WatchVerdict,
    timeoutMessage: string | null = null,
  ): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      let settled = false;
      let eventTimer: ReturnType<typeof setTimeout> | null = null;
      const finish = (error: Error | null) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        if (eventTimer !== null) clearTimeout(eventTimer);
        stopFrames();
        stopEvents();
        if (this.cancelWait === cancel) this.cancelWait = null;
        if (error) reject(error);
        else resolve();
      };
      const evaluate = (event: DeviceEvent | null) => {
        if (token !== this.turnToken) return finish(new TurnAborted('Superseded'));
        const verdict = check(this.boxMode, event);
        if (verdict === 'done') finish(null);
        else if (typeof verdict === 'object') {
          if (eventTimer === null) eventTimer = setTimeout(() => finish(new Error(verdict.timeoutMessage)), verdict.waitMs);
        } else if (verdict !== 'continue') finish(new Error(verdict));
      };
      const cancel = (reason: string) => finish(new TurnAborted(reason));
      const timer = setTimeout(() => finish(timeoutMessage ? new Error(timeoutMessage) : null), timeoutMs);
      const stopFrames = this.link.subscribeFrame(() => evaluate(null));
      const stopEvents = this.link.subscribeEvents((event) => evaluate(event));
      this.cancelWait = cancel;
    });
  }

  private abortTurn(reason: string) {
    if (this.state.phase === 'idle' && !this.cancelWait) return;
    this.turnToken += 1;
    const cancel = this.cancelWait;
    this.cancelWait = null;
    cancel?.(reason);
    this.set({ phase: 'idle', direction: null, originDeg: null, targetDeg: null });
  }

  private finishTurn(token: number, result: OperatorResult) {
    if (token !== this.turnToken) return;
    this.set({ phase: 'idle', direction: null, originDeg: null, targetDeg: null, result });
  }

  private onFrame = () => {
    const mode = this.boxMode;
    if (!this.state.controlling) return;
    if (mode !== null && mode !== 'SAFE') this.armedSeen = true;
    if (mode === 'SAFE' && this.armedSeen) {
      this.armedSeen = false;
      this.abortTurn('The box is SAFE');
      this.set({ controlling: false });
    }
  };

  private onEvent = (event: DeviceEvent) => {
    if (event.event === 'mode' && event.mode === 'SAFE' && this.state.controlling && event.cause && event.cause !== 'disarm') {
      this.set({ result: { tone: 'error', text: `THE BOX WENT SAFE: ${event.cause.replace(/_/g, ' ')}` } });
    }
  };

  private onLinkState = () => {
    if (this.link.isOpen || (!this.state.controlling && this.state.phase === 'idle')) return;
    this.abortTurn('Link closed');
    this.armedSeen = false;
    this.set({
      controlling: false,
      busy: null,
      result: { tone: 'error', text: 'LINK CLOSED: AN ARMED BOX RAMPS DOWN BY ITSELF 1 S AFTER THE LAST HEARTBEAT.' },
    });
  };

  private set(patch: Partial<OperatorState>) {
    this.state = { ...this.state, ...patch };
    for (const listener of this.listeners) listener();
  }
}
