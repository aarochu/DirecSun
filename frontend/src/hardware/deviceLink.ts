import type { DisplayFrame, DisplaySource, DisplayTelemetry } from '../ground/displaySource';
import { normalizeDeg } from '../simulation/physics';
import type { HistorySample } from '../simulation/types';

export type LinkStatus = 'idle' | 'connecting' | 'open' | 'closed' | 'error';

export interface DeviceTelemetry {
  readonly seq: number | null;
  readonly tMs: number | null;
  readonly mode: string | null;
  readonly headingDeg: number;
  readonly targetDeg: number | null;
  readonly rateDegS: number | null;
  readonly wheelRpm: number | null;
  readonly batteryV: number | null;
}

export interface LinkState {
  readonly status: LinkStatus;
  readonly url: string | null;
  readonly message: string | null;
  readonly packets: number;
  readonly ignored: number;
}

const finite = (value: unknown): number | null =>
  typeof value === 'number' && Number.isFinite(value) ? value : null;

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
    headingDeg,
    targetDeg: finite(message.psi_ref_deg),
    rateDegS: finite(message.rate_dps),
    wheelRpm: finite(message.wheel_rpm),
    batteryV: finite(message.vbat),
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

const HISTORY_WINDOW_S = 15;
const TELEMETRY_INTERVAL_MS = 50;
const SOCKET_CONNECTING = 0;
const SOCKET_OPEN = 1;

type SocketFactory = (url: string) => WebSocket;

export class DeviceLink implements DisplaySource {
  private socket: WebSocket | null = null;
  private state: LinkState = { status: 'idle', url: null, message: null, packets: 0, ignored: 0 };
  private latest: DeviceTelemetry | null = null;
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
    this.setState({ status: 'connecting', url, message: null, packets: 0, ignored: 0 });

    socket.onopen = () => {
      if (this.socket === socket) this.setState({ status: 'open', message: 'Waiting for telemetry…' });
    };
    socket.onmessage = (event: MessageEvent) => {
      if (this.socket === socket) this.receive(event.data);
    };
    socket.onerror = () => {
      if (this.socket === socket) this.setState({ status: 'error', message: 'Connection error' });
    };
    socket.onclose = (event: CloseEvent) => {
      if (this.socket !== socket) return;
      this.socket = null;
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
      this.setState({ ignored: this.state.ignored + 1 });
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
    this.setState({ packets: this.state.packets + 1, message: null });
    this.publishTelemetry(wasWaiting);
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
      wheelNote: 'DEVICE REPORTED',
      batteryV: reading?.batteryV ?? null,
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
