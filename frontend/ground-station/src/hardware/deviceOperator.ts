import { normalizeDeg } from '../simulation/physics';
import type { Direction } from '../simulation/types';
import { ARMED_MODES, AUTO_EXITS, canEnter, MANUAL_NUDGE, modeCommand, REFUSAL_HINTS, RESPIN_RANGE_RPM, type CommandableMode, type ManualInput } from './modes';
import { DeviceCommandError, type DeviceEvent, type DeviceLink } from './deviceLink';

export type TurnPhase = 'idle' | 'rotating' | 'holding' | 'returning';

export interface OperatorResult {
  readonly tone: 'ok' | 'error';
  readonly text: string;
}

export interface OperatorState {
  readonly controlling: boolean;
  readonly linkLost: boolean;
  readonly phase: TurnPhase;
  readonly direction: Direction | null;
  readonly originDeg: number | null;
  readonly targetDeg: number | null;
  readonly busy: 'arm' | 'disarm' | 'estop' | 'mode' | null;
  readonly manual: ManualState | null;
  readonly result: OperatorResult | null;
}

export type { CommandableMode, ManualInput } from './modes';

export interface ManualState {
  readonly input: ManualInput;
  readonly value: number;
  readonly min: number;
  readonly max: number;
}

export const MANUAL_PERIOD_MS = 100;
const MANUAL_SETPOINT_KEY: Record<ManualInput, string> = { rate: 'rate_dps', throttle: 'throttle', wheel: 'wheel_rpm' };
const WHEEL_RANGE: readonly [number, number] = [800, 4000];
const RATE_RANGE: readonly [number, number] = [-90, 90];

export const TURN_STEP_DEG = 90;
export const HOLD_MS = 1000;
export const SLEW_WAIT_MS = 10000;
export const SLEW_EVENT_WAIT_MS = 1000;

type WatchVerdict = 'continue' | 'done' | string | { waitMs: number; timeoutMessage: string };

const IDLE: OperatorState = {
  controlling: false,
  linkLost: false,
  phase: 'idle',
  direction: null,
  originDeg: null,
  targetDeg: null,
  busy: null,
  manual: null,
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
    if (error.code === 'REFUSED') {
      const hint = error.reason ? REFUSAL_HINTS[error.reason] : undefined;
      return `${error.command.toUpperCase()} REFUSED: ${error.reason ?? 'no reason given'}${hint ? `. ${hint}` : ''}`;
    }
    if (error.code === 'TIMEOUT') return `${error.command.toUpperCase()}: NO ANSWER FROM THE BOX`;
    return `${error.command.toUpperCase()} NOT SENT: THE LINK IS NOT OPEN`;
  }
  return error instanceof Error ? error.message : String(error);
};

export class DeviceOperator {
  private state: OperatorState = IDLE;
  private readonly listeners = new Set<() => void>();
  private armedSeen = false;
  private ownEstopPending = false;
  private slewIssued = false;
  private turnToken = 0;
  private cancelWait: ((reason: string) => void) | null = null;
  private unsubscribe: (() => void)[] = [];
  private manualTimer: ReturnType<typeof setInterval> | null = null;
  private lastManualError = '';
  // Set by e-stop and disarm: the box keeps reporting MANUAL until its mode
  // event lands, and those frames must not restart the stream.
  private manualHeld = false;

  constructor(private readonly link: DeviceLink) {}

  attach(): void {
    if (this.unsubscribe.length) return;
    this.unsubscribe = [
      this.link.subscribeReadings(this.onFrame),
      this.link.subscribeState(this.onLinkState),
      this.link.subscribeEvents(this.onEvent),
    ];
  }

  detach(): void {
    this.stopManual();
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
    return this.state.controlling && this.state.phase === 'idle' && this.boxMode === 'READY' &&
      (this.link.latestReading?.wheelRefRpm ?? 0) > 0;
  }

  get canChangeMode(): boolean {
    return this.state.controlling && this.state.phase === 'idle' && this.state.busy === null && (ARMED_MODES as readonly string[]).includes(this.boxMode ?? '');
  }

  async setMode(mode: CommandableMode, input?: ManualInput): Promise<void> {
    if (mode === 'SLEW' || !this.canChangeMode || !canEnter(mode, this.boxMode)) return;
    const label = input ? `${mode} (${input.toUpperCase()})` : mode.replace('_', ' ');
    this.set({ busy: 'mode', result: null });
    try {
      await this.link.command('mode', modeCommand({ mode, input }));
      this.set({ busy: null, result: { tone: 'ok', text: `MODE ${label} ACCEPTED.` } });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  async slewToHeading(headingDeg: number): Promise<void> {
    if (!Number.isFinite(headingDeg) || !this.canChangeMode || !canEnter('SLEW', this.boxMode)) return;
    const target = wireAngle(headingDeg);
    this.set({ busy: 'mode', result: null });
    try {
      await this.link.command('mode', modeCommand({ mode: 'SLEW', psiRefDeg: target }));
      this.set({ busy: null, result: { tone: 'ok', text: `SLEW ACCEPTED: TURNING TO ${target}°.` } });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  async respin(wheelRpm: number | null): Promise<void> {
    if (!this.canChangeMode || this.boxMode !== 'READY') return;
    if (wheelRpm !== null && !(Number.isFinite(wheelRpm) && wheelRpm >= RESPIN_RANGE_RPM[0] && wheelRpm <= RESPIN_RANGE_RPM[1])) {
      this.set({ result: { tone: 'error', text: `RESPIN NOT SENT: THE START SPEED MUST BE ${RESPIN_RANGE_RPM[0]} TO ${RESPIN_RANGE_RPM[1]} RPM.` } });
      return;
    }
    this.set({ busy: 'mode', result: null });
    try {
      await this.link.command('respin', wheelRpm === null ? {} : { wheel_rpm: Math.round(wheelRpm) });
      this.set({
        busy: null,
        result: { tone: 'ok', text: `RESPIN ACCEPTED: SPINNING TO ${wheelRpm === null ? 'THE BIAS SPEED' : `${Math.round(wheelRpm)} RPM`}. KEEP THE PIN IN UNTIL READY.` },
      });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  nudgeManual(steps: number): void {
    const manual = this.state.manual;
    if (manual) this.setManual(manual.value + steps * MANUAL_NUDGE[manual.input]);
  }

  setManual(value: number): void {
    const manual = this.state.manual;
    if (!manual || !Number.isFinite(value)) return;
    const clamped = Math.min(manual.max, Math.max(manual.min, manual.input === 'rate' ? value : Math.round(value)));
    this.set({ manual: { ...manual, value: clamped } });
  }

  private manualRange(input: ManualInput): readonly [number, number] | null {
    if (input === 'rate') return RATE_RANGE;
    if (input === 'wheel') return WHEEL_RANGE;
    return this.link.latestReading?.throttleBand ?? null;
  }

  private syncManual(): void {
    const reading = this.link.latestReading;
    if (reading?.mode !== 'MANUAL') this.manualHeld = false;
    const input = this.state.controlling && !this.manualHeld && reading?.mode === 'MANUAL' ? reading.manualInput : null;
    if (input === null) return this.stopManual();
    if (this.state.manual?.input !== input) {
      const range = this.manualRange(input);
      if (!range) return this.stopManual();
      const start = input === 'rate' ? 0 : input === 'wheel' ? Math.round(reading?.wheelRpm ?? 2400) : range[0];
      this.set({ manual: { input, min: range[0], max: range[1], value: Math.min(range[1], Math.max(range[0], start)) } });
    }
    if (this.manualTimer === null) this.manualTimer = setInterval(() => this.sendManual(), MANUAL_PERIOD_MS);
  }

  private sendManual(): void {
    const manual = this.state.manual;
    if (!manual || !this.link.isOpen) return;
    this.link.command('manual', { [MANUAL_SETPOINT_KEY[manual.input]]: manual.value }).catch((error: unknown) => {
      const text = describe(error);
      if (/too fast/i.test(text) || text === this.lastManualError) return;
      this.lastManualError = text;
      this.set({ result: { tone: 'error', text } });
    });
  }

  private holdManual(): void {
    this.manualHeld = true;
    this.stopManual();
  }

  private stopManual(): void {
    if (this.manualTimer !== null) clearInterval(this.manualTimer);
    this.manualTimer = null;
    this.lastManualError = '';
    if (this.state.manual) this.set({ manual: null });
  }

  async arm(): Promise<void> {
    if (this.state.busy) return;
    this.set({ busy: 'arm', result: null });
    try {
      await this.link.command('arm');
      this.ownEstopPending = false;
      this.armedSeen = false;
      this.set({ busy: null, controlling: true, result: { tone: 'ok', text: 'ARM ACCEPTED: SPINNING UP. KEEP THE LOCK PIN IN.' } });
    } catch (error) {
      this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
    }
  }

  async disarm(): Promise<void> {
    if (this.state.busy) return;
    this.holdManual();
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
    this.holdManual();
    this.abortTurn('E-stop sent');
    this.ownEstopPending = true;
    let sent: Promise<unknown>;
    try {
      sent = this.link.estop();
    } catch (error) {
      this.ownEstopPending = false;
      this.set({ result: { tone: 'error', text: describe(error) } });
      return;
    }
    this.set({ busy: 'estop', result: { tone: 'ok', text: 'E-STOP SENT.' } });
    sent.then(
      () => this.set({ busy: null, result: { tone: 'ok', text: 'E-STOP ACCEPTED: THE WHEEL RAMPS DOWN.' } }),
      (error: unknown) => {
        this.ownEstopPending = false;
        this.set({ busy: null, result: { tone: 'error', text: describe(error) } });
      },
    );
  }

  async rotate(direction: Direction): Promise<void> {
    if (!this.canRotate) return;
    const token = ++this.turnToken;
    this.slewIssued = false;
    const origin = 0;
    const target = wireAngle(direction * TURN_STEP_DEG);
    this.set({ phase: 'rotating', direction, originDeg: origin, targetDeg: target, result: null });
    try {
      await this.link.command('zero_heading');
      if (token !== this.turnToken) throw new TurnAborted('Superseded');
      await this.slewTo(target, token);
      this.set({ phase: 'holding' });
      await this.watch(token, HOLD_MS, (_mode, event) => {
        if (event?.event !== 'mode' || event.mode === 'READY') return 'continue';
        return `THE BOX LEFT READY (${event.mode ?? 'NO MODE'})`;
      });
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
    const shouldSendReady = this.slewIssued || this.boxMode === 'SLEW';
    this.abortTurn('Stop requested');
    if (wasTurning) this.set({ result: { tone: 'ok', text: 'TURN STOPPED: HOLDING WHERE THE BOX IS.' } });
    if (!shouldSendReady) return;
    try {
      await this.link.command('mode', { mode: 'READY' });
    } catch (error) {
      this.set({ result: { tone: 'error', text: describe(error) } });
    }
  }

  private async slewTo(targetDeg: number, token: number) {
    this.slewIssued = true;
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
      const stopFrames = this.link.subscribeReadings(() => evaluate(null));
      const stopEvents = this.link.subscribeEvents((event) => evaluate(event));
      this.cancelWait = cancel;
    });
  }

  private abortTurn(reason: string) {
    if (this.state.phase === 'idle' && !this.cancelWait) return;
    this.turnToken += 1;
    this.slewIssued = false;
    const cancel = this.cancelWait;
    this.cancelWait = null;
    cancel?.(reason);
    this.set({ phase: 'idle', direction: null, originDeg: null, targetDeg: null });
  }

  private finishTurn(token: number, result: OperatorResult) {
    if (token !== this.turnToken) return;
    this.slewIssued = false;
    this.set({ phase: 'idle', direction: null, originDeg: null, targetDeg: null, result });
  }

  private onFrame = () => {
    this.syncManual();
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
    const exit = event.event === 'mode' && event.cause ? AUTO_EXITS[event.cause] : undefined;
    if (exit && this.state.controlling && this.state.phase === 'idle') this.set({ result: exit });
    if (event.event === 'mode' && event.mode === 'SAFE' && this.state.controlling && event.cause && event.cause !== 'disarm') {
      if (this.ownEstopPending && event.cause === 'ESTOP') {
        this.ownEstopPending = false;
        this.set({ result: { tone: 'ok', text: 'E-STOP ACCEPTED: THE WHEEL RAMPS DOWN.' } });
      } else {
        this.set({ result: { tone: 'error', text: `THE BOX WENT SAFE: ${event.cause.replace(/_/g, ' ')}` } });
      }
    }
  };

  private onLinkState = () => {
    if (!this.link.isOpen) this.stopManual();
    if (this.link.isOpen) {
      if (this.state.linkLost) this.set({ linkLost: false });
      return;
    }
    if (!this.state.controlling && this.state.phase === 'idle') return;
    this.abortTurn('Link closed');
    this.armedSeen = false;
    this.ownEstopPending = false;
    this.set({
      controlling: false,
      linkLost: true,
      busy: null,
      result: { tone: 'error', text: 'LINK CLOSED: AN ARMED BOX RAMPS DOWN BY ITSELF 1 S AFTER THE LAST HEARTBEAT.' },
    });
  };

  private set(patch: Partial<OperatorState>) {
    this.state = { ...this.state, ...patch };
    for (const listener of this.listeners) listener();
  }
}
