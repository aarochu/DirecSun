export class FakeSocket {
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
  frames(): Record<string, unknown>[] {
    return this.sent.map((data) => JSON.parse(String(data)) as Record<string, unknown>);
  }
  commands(): Record<string, unknown>[] {
    return this.frames().filter((frame) => frame.type === 'cmd');
  }
  lastCommand(): Record<string, unknown> {
    const commands = this.commands();
    return commands[commands.length - 1]!;
  }
}

export const tlm = (fields: Record<string, unknown>) => JSON.stringify({ v: 1, type: 'tlm', ...fields });
export const ack = (seq: unknown, ok = true, reason?: string, fields: Record<string, unknown> = {}) =>
  JSON.stringify({ v: 1, type: 'ack', seq, ok, ...(reason ? { reason } : {}), ...fields });
export const modeEvent = (mode: string, cause: string) =>
  JSON.stringify({ v: 1, type: 'event', seq: 900, t_ms: 0, event: 'mode', mode, cause });
