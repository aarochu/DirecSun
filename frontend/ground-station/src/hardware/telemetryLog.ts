import type { DeviceLink } from './deviceLink';

export const TLM_FIELDS = [
  'seq',
  't_ms',
  'mode',
  'manual_input',
  'psi_deg',
  'psi_ref_deg',
  'rate_dps',
  'mag_deg',
  'sun_deg',
  'sun_valid',
  'wheel_rpm',
  'wheel_ref_rpm',
  'throttle',
  'throttle_band',
  'tau_cmd_mNm',
  'vbat',
  'loop_us',
  'flags',
  't_c',
  'p_pa',
  'tlm_dropped',
  'dropped_events',
  'accel_rms_g',
  'gyro_raw',
  'mag_raw',
  'sun_a_mv',
  'sun_b_mv',
] as const;

export const FIELDS_AHEAD_OF_SCHEMA: readonly string[] = ['throttle_band'];

const SPLIT: Readonly<Record<string, readonly string[]>> = {
  throttle_band: ['min', 'max'],
  gyro_raw: ['x', 'y', 'z'],
  mag_raw: ['x', 'y', 'z'],
};

export const TLM_COLUMNS: readonly string[] = [
  'client_ms',
  ...TLM_FIELDS.flatMap((field) => (SPLIT[field] ? SPLIT[field]!.map((part) => `${field}_${part}`) : [field])),
  'extra',
];

const MESSAGE_FIELDS = ['type', 'seq', 'cmd', 'mode', 'input', 'psi_ref_deg', 'ok', 'reason', 'event', 'cause', 'name', 'detail', 't_ms'] as const;
export const MESSAGE_COLUMNS: readonly string[] = ['client_ms', 'dir', ...MESSAGE_FIELDS, 'json'];

const KNOWN = new Set<string>(['v', 'type', ...TLM_FIELDS]);

export function csvCell(value: unknown): string {
  if (value === null || value === undefined) return '';
  const text = typeof value === 'string' ? value : typeof value === 'object' ? JSON.stringify(value) : String(value);
  return /[",\r\n]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

export function tlmRow(clientMs: number, message: Record<string, unknown>): string {
  const cells: unknown[] = [clientMs.toFixed(1)];
  for (const field of TLM_FIELDS) {
    const value = message[field];
    const parts = SPLIT[field];
    if (parts) {
      parts.forEach((_, index) => cells.push(Array.isArray(value) ? value[index] : null));
    } else if (field === 'flags') {
      cells.push(Array.isArray(value) ? value.join('|') : null);
    } else {
      cells.push(value);
    }
  }
  const extra = Object.fromEntries(Object.entries(message).filter(([key]) => !KNOWN.has(key)));
  cells.push(Object.keys(extra).length ? extra : null);
  return cells.map(csvCell).join(',');
}

export function messageRow(clientMs: number, dir: 'rx' | 'tx', message: Record<string, unknown>, raw: string): string {
  return [clientMs.toFixed(1), dir, ...MESSAGE_FIELDS.map((field) => message[field]), raw].map(csvCell).join(',');
}

export interface RecorderState {
  readonly recording: boolean;
  readonly tlmRows: number;
  readonly messageRows: number;
  readonly startedAtMs: number | null;
}

export interface RecordingFiles {
  readonly name: string;
  readonly tlmCsv: string;
  readonly messagesCsv: string;
}

const NOTIFY_MS = 250;

export class TelemetryRecorder {
  private tlm: string[] = [];
  private messages: string[] = [];
  private startedAtMs: number | null = null;
  private stopRaw: (() => void) | null = null;
  private state: RecorderState = { recording: false, tlmRows: 0, messageRows: 0, startedAtMs: null };
  private readonly listeners = new Set<() => void>();
  private lastNotifyMs = 0;

  constructor(
    private readonly link: DeviceLink,
    private readonly now: () => number = () => Date.now(),
  ) {}

  subscribe = (listener: () => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getState = (): RecorderState => this.state;

  start(): void {
    if (this.stopRaw) return;
    this.tlm = [TLM_COLUMNS.join(',')];
    this.messages = [MESSAGE_COLUMNS.join(',')];
    this.startedAtMs = this.now();
    this.stopRaw = this.link.subscribeRaw((dir, raw) => this.record(dir, raw));
    this.publish(true);
  }

  stop(): RecordingFiles | null {
    if (!this.stopRaw || this.startedAtMs === null) return null;
    this.stopRaw();
    this.stopRaw = null;
    const files = { name: fileStamp(this.startedAtMs), tlmCsv: `${this.tlm.join('\n')}\n`, messagesCsv: `${this.messages.join('\n')}\n` };
    this.startedAtMs = null;
    this.publish(true);
    return files;
  }

  private record(dir: 'rx' | 'tx', raw: string) {
    let message: unknown;
    try {
      message = JSON.parse(raw);
    } catch {
      message = null;
    }
    const clientMs = this.now() - (this.startedAtMs ?? 0);
    if (!message || typeof message !== 'object' || Array.isArray(message)) {
      this.messages.push(messageRow(clientMs, dir, {}, raw));
    } else if (dir === 'rx' && (message as Record<string, unknown>).type === 'tlm') {
      this.tlm.push(tlmRow(clientMs, message as Record<string, unknown>));
    } else {
      this.messages.push(messageRow(clientMs, dir, message as Record<string, unknown>, raw));
    }
    this.publish(false);
  }

  private publish(force: boolean) {
    const now = this.now();
    if (!force && now - this.lastNotifyMs < NOTIFY_MS) return;
    this.lastNotifyMs = now;
    this.state = {
      recording: this.stopRaw !== null,
      tlmRows: Math.max(0, this.tlm.length - 1),
      messageRows: Math.max(0, this.messages.length - 1),
      startedAtMs: this.startedAtMs,
    };
    for (const listener of this.listeners) listener();
  }
}

function fileStamp(epochMs: number): string {
  const d = new Date(epochMs);
  const pad = (n: number) => String(n).padStart(2, '0');
  return `direcsun-${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
}

export function downloadRecording(files: RecordingFiles): void {
  for (const [suffix, csv] of [
    ['tlm', files.tlmCsv],
    ['messages', files.messagesCsv],
  ] as const) {
    const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }));
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `${files.name}-${suffix}.csv`;
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
}
