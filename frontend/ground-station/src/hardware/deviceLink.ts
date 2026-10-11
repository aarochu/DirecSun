import type { DisplayFrame, DisplaySource, DisplayTelemetry } from '../ground/displaySource';
import { normalizeDeg } from '../simulation/physics';
import type { HistorySample } from '../simulation/types';

export type LinkStatus = 'idle' | 'connecting' | 'open' | 'closed' | 'error';

export interface DeviceTelemetry {
  readonly seq: number | null;
  readonly tMs: number | null;
  readonly mode: string | null;
  readonly manualInput: 'rate' | 'throttle' | 'wheel' | null;
  readonly headingDeg: number;
  readonly targetDeg: number | null;
  readonly rateDegS: number | null;
  readonly wheelRpm: number | null;
  readonly wheelRefRpm: number | null;
  readonly throttleBand: readonly [number, number] | null;
  readonly flags: readonly string[];
  readonly batteryV: number | null;
  readonly sensors: SensorReadings;
  readonly throttle: number | null;
  readonly tauCmdMNm: number | null;
  readonly tlmDropped: number | null;
  readonly droppedEvents: number | null;
}

// Per-sensor values from the tlm frame (protocol/schema-v1.json). null when the
// box leaves a field out: no BMP280, no magnetometer, or not in bench mode.
export interface SensorReadings {
  // MPU-6050
  readonly rateDps: number | null;
  readonly gyroRaw: readonly [number, number, number] | null;
  readonly accelRmsG: number | null;
  // QMC5883P
  readonly magDeg: number | null;
  readonly magRaw: readonly [number, number, number] | null;
  // OP550 x 2
  readonly sunDeg: number | null;
  readonly sunValid: boolean | null;
  readonly sunAmV: number | null;
  readonly sunBmV: number | null;
  // A1104 Hall
  readonly wheelRpm: number | null;
  // 100k / 22k divider
  readonly batteryV: number | null;
  // BMP280
  readonly tempC: number | null;
  readonly pressurePa: number | null;
  // Control task
  readonly loopUs: number | null;
}

export type FlagSeverity = 'ok' | 'warning' | 'fault';

const FAULT_FLAGS = new Set([
  'ESTOP',
  'HEARTBEAT_LOST',
  'OVERSPEED',
  'UNDERSPEED',
  'BATTERY_LOW',
  'IMU_SILENT',
  'TILT',
  'CONTROL_STALL',
  'HALL_SILENT',
]);

export function flagSeverity(flag: string): FlagSeverity {
  if (flag === 'MOMENTUM_OK') return 'ok';
  return FAULT_FLAGS.has(flag) ? 'fault' : 'warning';
}

export interface DeviceEvent {
  readonly event: 'mode' | 'notice';
  readonly mode: string | null;
  readonly cause: string | null;
  readonly name: string | null;
  readonly detail: string | null;
}

export interface DeviceAck {
  readonly seq: number;
  readonly ok: boolean;
  readonly reason: string | null;
  readonly applied: Readonly<Record<string, unknown>> | null;
}

export type CommandErrorCode = 'REFUSED' | 'TIMEOUT' | 'CLOSED';

export class DeviceCommandError extends Error {
  constructor(
    message: string,
    readonly code: CommandErrorCode,
    readonly command: string,
    readonly reason: string | null = null,
  ) {
    super(message);
    this.name = 'DeviceCommandError';
  }
}

export interface LinkState {
  readonly status: LinkStatus;
  readonly url: string | null;
  readonly message: string | null;
  readonly packets: number;
  readonly ignored: number;
  readonly talking: boolean;
  readonly rttMs: number | null;
  readonly lastEvent: DeviceEvent | null;
  readonly notices: readonly DeviceNotice[];
  readonly tlmDropped: number;
  readonly droppedEvents: number;
}

export interface DeviceNotice {
  readonly name: string;
  readonly detail: string | null;
  readonly atMs: number;
}

export const MAX_NOTICES = 5;

const finite = (value: unknown): number | null =>
  typeof value === 'number' && Number.isFinite(value) ? value : null;

function band(value: unknown): readonly [number, number] | null {
  if (!Array.isArray(value) || value.length !== 2) return null;
  const [lo, hi] = value.map(finite);
  return lo === null || hi === null || lo === undefined || hi === undefined ? null : [lo, hi];
}

function triple(value: unknown): readonly [number, number, number] | null {
  if (!Array.isArray(value) || value.length !== 3) return null;
  const [x, y, z] = value.map(finite);
  return x == null || y == null || z == null ? null : [x, y, z];
}

export function parseSensors(message: Record<string, unknown>): SensorReadings {
  return {
    rateDps: finite(message.rate_dps),
    gyroRaw: triple(message.gyro_raw),
    accelRmsG: finite(message.accel_rms_g),
    magDeg: finite(message.mag_deg),
    magRaw: triple(message.mag_raw),
    sunDeg: finite(message.sun_deg),
    sunValid: typeof message.sun_valid === 'boolean' ? message.sun_valid : null,
    sunAmV: finite(message.sun_a_mv),
    sunBmV: finite(message.sun_b_mv),
    wheelRpm: finite(message.wheel_rpm),
    batteryV: finite(message.vbat),
    tempC: finite(message.t_c),
    pressurePa: finite(message.p_pa),
    loopUs: finite(message.loop_us),
  };
}

function flagList(value: unknown): readonly string[] {
  if (!Array.isArray(value)) return [];
  return [...new Set(value.filter((flag): flag is string => typeof flag === 'string' && flag.length > 0))];
}

export function parseTelemetry(raw: unknown): DeviceTelemetry | null {
  if (typeof raw !== 'string') return null;
  let message: Record<string, unknown>;
  try {
    const parsed: unknown = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return null;
    message = parsed as Record<string, unknown>;
  } catch {
    return null;
  }
  if (message.type !== 'tlm') return null;
  if (message.v !== undefined && message.v !== 1) return null;
  const headingDeg = finite(message.psi_deg);
  if (headingDeg === null) return null;
  return {
    seq: finite(message.seq),
    tMs: finite(message.t_ms),
    mode: typeof message.mode === 'string' ? message.mode : null,
    manualInput:
      message.manual_input === 'rate' || message.manual_input === 'throttle' || message.manual_input === 'wheel'
        ? message.manual_input
        : null,
    headingDeg,
    targetDeg: finite(message.psi_ref_deg),
    rateDegS: finite(message.rate_dps),
    wheelRpm: finite(message.wheel_rpm),
    wheelRefRpm: finite(message.wheel_ref_rpm),
    throttleBand: band(message.throttle_band),
    flags: flagList(message.flags),
    batteryV: finite(message.vbat),
    sensors: parseSensors(message),
    throttle: finite(message.throttle),
    tauCmdMNm: finite(message.tau_cmd_mNm),
    tlmDropped: finite(message.tlm_dropped),
    droppedEvents: finite(message.dropped_events),
  };
}

export function validateLinkUrl(text: string): string | null {
  let url: URL;
  try {
    url = new URL(text.trim());
  } catch {
    return 'Enter a full WebSocket URL, e.g. ws://192.168.4.1/ws';
  }
  if (url.protocol !== 'ws:' && url.protocol !== 'wss:') return 'The URL must start with ws:// or wss://';
  if (
    typeof window !== 'undefined' &&
    window.location.protocol === 'https:' &&
    url.protocol === 'ws:'
  ) {
    return 'This page is served over HTTPS, so the browser will block ws://. Use wss://.';
  }
  return null;
}

function parseObject(raw: unknown): Record<string, unknown> | null {
  if (typeof raw !== 'string') return null;
  try {
    const parsed: unknown = JSON.parse(raw);
    return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? (parsed as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

const text = (value: unknown): string | null => (typeof value === 'string' && value.length > 0 ? value : null);

const HISTORY_WINDOW_S = 15;
const TELEMETRY_INTERVAL_MS = 50;
const SOCKET_CONNECTING = 0;
const SOCKET_OPEN = 1;
export const ACK_TIMEOUT_MS = 1000;
export const PING_INTERVAL_MS = 200;
const PROTOCOL_VERSION = 1;
const MAX_SEQ = 4294967296;

type SocketFactory = (url: string) => WebSocket;

interface PendingCommand {
  readonly command: string;
  readonly resolve: (ack: DeviceAck) => void;
  readonly reject: (error: DeviceCommandError) => void;
  readonly timer: ReturnType<typeof setTimeout>;
}

const IDLE_STATE: LinkState = {
  status: 'idle',
  url: null,
  message: null,
  packets: 0,
  ignored: 0,
  talking: false,
  rttMs: null,
  lastEvent: null,
  notices: [],
  tlmDropped: 0,
  droppedEvents: 0,
};

export class DeviceLink implements DisplaySource {
  private socket: WebSocket | null = null;
  private state: LinkState = IDLE_STATE;
  private seq = 0;
  private readonly pending = new Map<number, PendingCommand>();
  private readonly pings = new Map<number, number>();
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private readonly eventListeners = new Set<(event: DeviceEvent) => void>();
  private latest: DeviceTelemetry | null = null;
  private readonly rawListeners = new Set<(dir: 'rx' | 'tx', raw: string) => void>();
  private frame: DisplayFrame = { headingDeg: 0, targetDeg: 0, originDeg: null };
  private telemetry: DisplayTelemetry;
  private history: HistorySample[] = [];
  private unwrappedHeading = 0;
  private firstTimeS: number | null = null;
  private nowS = 0;
  private lastTelemetryMs = -Infinity;
  private readonly stateListeners = new Set<() => void>();
  private readonly frameListeners = new Set<() => void>();
  private readonly telemetryListeners = new Set<() => void>();

  constructor(
    private readonly createSocket: SocketFactory = (url) => new WebSocket(url),
    private readonly now: () => number = () => performance.now(),
  ) {
    this.telemetry = this.buildTelemetry();
  }

  subscribeState = (listener: () => void) => {
    this.stateListeners.add(listener);
    return () => this.stateListeners.delete(listener);
  };

  getState = (): LinkState => this.state;

  get receiving(): boolean {
    return this.state.status === 'open' && this.latest !== null;
  }

  get latestReading(): DeviceTelemetry | null {
    return this.latest;
  }

  get isOpen(): boolean {
    return this.socket !== null && this.socket.readyState === SOCKET_OPEN && this.state.status === 'open';
  }

  subscribeRaw = (listener: (dir: 'rx' | 'tx', raw: string) => void) => {
    this.rawListeners.add(listener);
    return () => this.rawListeners.delete(listener);
  };

  // A raw listener (the CSV recorder) must never stop the link from handling
  // a frame: a throw here would skip acks, events and the heartbeat's pongs.
  private emitRaw(dir: 'rx' | 'tx', raw: string) {
    for (const listener of this.rawListeners) {
      try {
        listener(dir, raw);
      } catch (error) {
        console.error('raw frame listener failed', error);
      }
    }
  }

  subscribeEvents = (listener: (event: DeviceEvent) => void) => {
    this.eventListeners.add(listener);
    return () => this.eventListeners.delete(listener);
  };

  command(name: 'arm' | 'disarm' | 'mode' | 'manual' | 'respin' | 'zero_heading' | 'set_params', fields: Record<string, unknown> = {}): Promise<DeviceAck> {
    return this.sendCommand(name, fields);
  }

  estop(): Promise<DeviceAck> {
    if (!this.isOpen) throw new DeviceCommandError('E-stop not sent: the link is not open', 'CLOSED', 'estop');
    return this.sendCommand('estop', {});
  }

  connect(url: string): void {
    this.disconnect();
    this.resetData();
    let socket: WebSocket;
    try {
      socket = this.createSocket(url.trim());
    } catch (error) {
      this.setState({ status: 'error', url, message: error instanceof Error ? error.message : 'Could not open the socket' });
      return;
    }
    this.socket = socket;
    this.setState({ ...IDLE_STATE, status: 'connecting', url });

    socket.onopen = () => {
      if (this.socket === socket) this.setState({ status: 'open', message: 'Waiting for telemetry…' });
    };
    socket.onmessage = (event: MessageEvent) => {
      if (this.socket !== socket) return;
      if (this.rawListeners.size) this.emitRaw('rx', String(event.data));
      this.receive(event.data);
    };
    socket.onerror = () => {
      if (this.socket === socket) this.setState({ status: 'error', message: 'Connection error' });
    };
    socket.onclose = (event: CloseEvent) => {
      if (this.socket !== socket) return;
      this.socket = null;
      this.stopTalking('The link closed before the box answered');
      const wasError = this.state.status === 'error' || this.state.status === 'connecting';
      this.setState({
        status: wasError ? 'error' : 'closed',
        message: `Connection closed${event.code ? ` (code ${event.code})` : ''}. Not reconnecting automatically.`,
      });
      this.publishTelemetry(true);
    };
  }

  disconnect(): void {
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      this.stopTalking('The link was disconnected before the box answered');
      socket.onopen = socket.onmessage = socket.onerror = socket.onclose = null;
      if (socket.readyState === SOCKET_CONNECTING || socket.readyState === SOCKET_OPEN) socket.close(1000);
      this.setState({ status: 'idle', message: null });
      this.publishTelemetry(true);
    }
  }

  subscribeFrame = (listener: () => void) => {
    this.frameListeners.add(listener);
    return () => this.frameListeners.delete(listener);
  };

  getFrame = (): DisplayFrame => this.frame;

  subscribeTelemetry = (listener: () => void) => {
    this.telemetryListeners.add(listener);
    return () => this.telemetryListeners.delete(listener);
  };

  getTelemetry = (): DisplayTelemetry => this.telemetry;

  private receive(data: unknown) {
    const reading = parseTelemetry(data);
    if (!reading) {
      if (!this.receiveControl(parseObject(data))) this.setState({ ignored: this.state.ignored + 1 });
      return;
    }

    this.unwrappedHeading =
      this.latest === null
        ? reading.headingDeg
        : this.unwrappedHeading + normalizeDeg(reading.headingDeg - this.latest.headingDeg);
    this.latest = reading;

    const deviceTimeS = reading.tMs !== null ? reading.tMs / 1000 : this.now() / 1000;
    this.firstTimeS ??= deviceTimeS;
    this.nowS = deviceTimeS - this.firstTimeS;
    const targetDeg = this.unwrappedTarget(reading);
    this.history.push({ t: this.nowS, heading: this.unwrappedHeading, target: targetDeg });
    const oldest = this.nowS - HISTORY_WINDOW_S - 0.5;
    while (this.history.length > 1 && this.history[0]!.t < oldest) this.history.shift();

    this.frame = { headingDeg: this.unwrappedHeading, targetDeg, originDeg: null };
    for (const listener of this.frameListeners) listener();

    const wasWaiting = this.state.packets === 0;
    this.setState({
      packets: this.state.packets + 1,
      message: null,
      tlmDropped: reading.tlmDropped ?? this.state.tlmDropped,
      droppedEvents: reading.droppedEvents ?? this.state.droppedEvents,
    });
    this.publishTelemetry(wasWaiting);
  }

  private receiveControl(message: Record<string, unknown> | null): boolean {
    if (!message || message.v !== PROTOCOL_VERSION) return false;
    switch (message.type) {
      case 'ack':
        this.settle(message);
        return true;
      case 'pong':
        this.recordPong(message);
        return true;
      case 'event': {
        const kind = message.event;
        if (kind !== 'mode' && kind !== 'notice') return false;
        const event: DeviceEvent = {
          event: kind,
          mode: text(message.mode),
          cause: text(message.cause),
          name: text(message.name),
          detail: text(message.detail),
        };
        this.setState(
          kind === 'notice' && event.name
            ? { lastEvent: event, notices: [{ name: event.name, detail: event.detail, atMs: Date.now() }, ...this.state.notices].slice(0, MAX_NOTICES) }
            : { lastEvent: event },
        );
        for (const listener of this.eventListeners) listener(event);
        return true;
      }
      default:
        return false;
    }
  }

  private nextSeq(): number {
    this.seq = (this.seq + 1) % MAX_SEQ;
    return this.seq;
  }

  private send(message: Record<string, unknown>) {
    const raw = JSON.stringify(message);
    this.socket!.send(raw);
    this.emitRaw('tx', raw);
  }

  private sendCommand(name: string, fields: Record<string, unknown>): Promise<DeviceAck> {
    if (!this.isOpen) {
      return Promise.reject(new DeviceCommandError(`${name} not sent: the link is not open`, 'CLOSED', name));
    }
    this.startTalking();
    const seq = this.nextSeq();
    const promise = new Promise<DeviceAck>((resolve, reject) => {
      const timer = setTimeout(() => {
        if (!this.pending.delete(seq)) return;
        reject(new DeviceCommandError(`${name}: no answer from the box within ${ACK_TIMEOUT_MS} ms`, 'TIMEOUT', name));
      }, ACK_TIMEOUT_MS);
      this.pending.set(seq, { command: name, resolve, reject, timer });
    });
    this.send({ v: PROTOCOL_VERSION, type: 'cmd', seq, cmd: name, ...fields });
    return promise;
  }

  private settle(message: Record<string, unknown>) {
    const seq = finite(message.seq);
    const pending = seq === null ? undefined : this.pending.get(seq);
    if (seq === null || !pending) return;
    this.pending.delete(seq);
    clearTimeout(pending.timer);
    const reason = text(message.reason);
    if (message.ok === true) {
      const applied = message.applied;
      pending.resolve({
        seq,
        ok: true,
        reason,
        applied: applied && typeof applied === 'object' && !Array.isArray(applied)
          ? (applied as Readonly<Record<string, unknown>>)
          : null,
      });
    } else {
      pending.reject(
        new DeviceCommandError(`${pending.command} refused: ${reason ?? 'no reason given'}`, 'REFUSED', pending.command, reason),
      );
    }
  }

  private startTalking() {
    if (this.pingTimer !== null) return;
    this.setState({ talking: true });
    this.ping();
    this.pingTimer = setInterval(() => this.ping(), PING_INTERVAL_MS);
  }

  private ping() {
    if (!this.isOpen) return;
    const seq = this.nextSeq();
    const sentMs = this.now();
    this.pings.set(seq, sentMs);
    if (this.pings.size > 50) this.pings.delete(this.pings.keys().next().value!);
    this.send({ v: PROTOCOL_VERSION, type: 'ping', seq, t_client_ms: Math.max(0, Math.round(sentMs * 1000) / 1000) });
  }

  private recordPong(message: Record<string, unknown>) {
    const pingSeq = finite(message.ping_seq);
    const sentMs = pingSeq === null ? undefined : this.pings.get(pingSeq);
    if (pingSeq === null || sentMs === undefined) return;
    this.pings.delete(pingSeq);
    this.setState({ rttMs: Math.max(0, this.now() - sentMs) });
  }

  private stopTalking(reason: string) {
    if (this.pingTimer !== null) clearInterval(this.pingTimer);
    this.pingTimer = null;
    this.pings.clear();
    for (const [, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new DeviceCommandError(`${pending.command}: ${reason}`, 'CLOSED', pending.command));
    }
    this.pending.clear();
    if (this.state.talking || this.state.rttMs !== null) this.setState({ talking: false, rttMs: null });
  }

  private unwrappedTarget(reading: DeviceTelemetry): number {
    if (reading.targetDeg === null) return this.unwrappedHeading;
    return this.unwrappedHeading + normalizeDeg(reading.targetDeg - reading.headingDeg);
  }

  private publishTelemetry(force: boolean) {
    const nowMs = this.now();
    if (!force && nowMs - this.lastTelemetryMs < TELEMETRY_INTERVAL_MS) return;
    this.lastTelemetryMs = nowMs;
    this.telemetry = this.buildTelemetry();
    for (const listener of this.telemetryListeners) listener();
  }

  private buildTelemetry(): DisplayTelemetry {
    const reading = this.latest;
    return {
      source: 'hardware',
      statusLabel: 'DEVICE MODE',
      statusWord: reading?.mode ?? '—',
      statusSuffix: 'DEVICE',
      headingDeg: this.unwrappedHeading,
      targetDeg: reading ? this.unwrappedTarget(reading) : this.unwrappedHeading,
      rateDegS: reading?.rateDegS ?? null,
      wheelRpm: reading?.wheelRpm ?? null,
      wheelNote: reading?.wheelRefRpm == null ? 'DEVICE REPORTED' : `REF ${Math.round(reading.wheelRefRpm)} RPM`,
      wheelRefRpm: reading?.wheelRefRpm ?? null,
      throttleBand: reading?.throttleBand ?? null,
      flags: reading?.flags ?? [],
      batteryV: reading?.batteryV ?? null,
      sensors: reading?.sensors ?? null,
      throttle: reading?.throttle ?? null,
      torqueMNm: reading?.tauCmdMNm ?? null,
      history: [...this.history],
      nowS: this.nowS,
    };
  }

  private resetData() {
    this.latest = null;
    this.history = [];
    this.unwrappedHeading = 0;
    this.firstTimeS = null;
    this.nowS = 0;
    this.frame = { headingDeg: 0, targetDeg: 0, originDeg: null };
    this.telemetry = this.buildTelemetry();
  }

  private setState(patch: Partial<LinkState>) {
    this.state = { ...this.state, ...patch };
    for (const listener of this.stateListeners) listener();
  }
}
