import { afterEach, describe, expect, it, vi } from 'vitest';
import schemaJson from '../../../../protocol/schema-v1.json';
import { DeviceLink } from '../hardware/deviceLink';
import { csvCell, FIELDS_AHEAD_OF_SCHEMA, MESSAGE_COLUMNS, TelemetryRecorder, TLM_COLUMNS, TLM_FIELDS, tlmRow } from '../hardware/telemetryLog';
import { FakeSocket, tlm } from './fakeSocket';

const tlmProps = Object.keys((schemaJson as { $defs: { tlm: { properties: Record<string, unknown> } } }).$defs.tlm.properties).filter(
  (key) => key !== 'v' && key !== 'type',
);

afterEach(() => {
  FakeSocket.instances = [];
});

function parseCsv(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [];
  let cell = '';
  let quoted = false;
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i]!;
    if (quoted) {
      if (ch === '"' && text[i + 1] === '"') {
        cell += '"';
        i += 1;
      } else if (ch === '"') quoted = false;
      else cell += ch;
    } else if (ch === '"') quoted = true;
    else if (ch === ',') {
      row.push(cell);
      cell = '';
    } else if (ch === '\n') {
      row.push(cell);
      rows.push(row);
      row = [];
      cell = '';
    } else cell += ch;
  }
  return rows;
}

describe('telemetry CSV columns', () => {
  it('cover every tlm field in protocol/schema-v1.json, and add only fields the box already sends ahead of it', () => {
    for (const field of tlmProps) expect(TLM_FIELDS as readonly string[], field).toContain(field);
    for (const field of TLM_FIELDS) if (!tlmProps.includes(field)) expect(FIELDS_AHEAD_OF_SCHEMA, field).toContain(field);
    expect(TLM_COLUMNS).toContain('gyro_raw_z');
    expect(TLM_COLUMNS).toContain('throttle_band_max');
  });

  it('writes one row per frame: arrays split, flags joined, nulls empty, unknown fields kept in extra', () => {
    const row = tlmRow(12.34, {
      v: 1,
      type: 'tlm',
      seq: 7,
      t_ms: 1000,
      mode: 'READY',
      psi_deg: -12.5,
      psi_ref_deg: null,
      sun_valid: false,
      throttle_band: [150, 400],
      gyro_raw: [1, -2, 3],
      flags: ['MOMENTUM_OK', 'HEADING_GYRO'],
      future_field: 'a,b',
    });
    const cells = parseCsv(`${row}\n`)[0]!;
    expect(cells).toHaveLength(TLM_COLUMNS.length);
    const at = (column: string) => cells[TLM_COLUMNS.indexOf(column)];
    expect(at('client_ms')).toBe('12.3');
    expect(at('psi_deg')).toBe('-12.5');
    expect(at('psi_ref_deg')).toBe('');
    expect(at('sun_valid')).toBe('false');
    expect(at('throttle_band_min')).toBe('150');
    expect(at('gyro_raw_y')).toBe('-2');
    expect(at('mag_raw_x')).toBe('');
    expect(at('flags')).toBe('MOMENTUM_OK|HEADING_GYRO');
    expect(JSON.parse(at('extra')!)).toEqual({ future_field: 'a,b' });
  });

  it('quotes cells with commas, quotes or newlines', () => {
    expect(csvCell('plain')).toBe('plain');
    expect(csvCell('a,b')).toBe('"a,b"');
    expect(csvCell('say "hi"')).toBe('"say ""hi"""');
    expect(csvCell(null)).toBe('');
  });
});

describe('TelemetryRecorder', () => {
  it('a raw listener that throws does not stop the link from handling the frame', async () => {
    const link = new DeviceLink((url) => new FakeSocket(url) as unknown as WebSocket, () => 0);
    link.connect('ws://box/ws');
    const socket = FakeSocket.instances.at(-1)!;
    socket.open();
    const quiet = vi.spyOn(console, 'error').mockImplementation(() => {});
    link.subscribeRaw(() => {
      throw new Error('recorder bug');
    });
    const sent = link.command('arm');
    socket.message(JSON.stringify({ v: 1, type: 'ack', seq: socket.lastCommand().seq, ok: true }));
    await expect(sent).resolves.toMatchObject({ ok: true });
    quiet.mockRestore();
  });

  it('records 60 s of 50 Hz telemetry and every other message in both directions, then stops', async () => {
    let now = 0;
    const link = new DeviceLink((url) => new FakeSocket(url) as unknown as WebSocket, () => now);
    link.connect('ws://box/ws');
    const socket = FakeSocket.instances.at(-1)!;
    socket.open();
    const recorder = new TelemetryRecorder(link, () => now);
    recorder.start();
    expect(recorder.getState().recording).toBe(true);

    for (let i = 0; i < 3000; i += 1) {
      now += 20;
      socket.message(tlm({ seq: i, t_ms: i * 20, psi_deg: i % 360, mode: 'SAFE', flags: [] }));
    }
    void link.command('arm').catch(() => {});
    socket.message(JSON.stringify({ v: 1, type: 'ack', seq: socket.lastCommand().seq, ok: false, reason: 'box not still' }));
    socket.message('not json');

    const files = recorder.stop()!;
    expect(recorder.getState()).toMatchObject({ recording: false, tlmRows: 3000 });
    const tlmRows = parseCsv(files.tlmCsv);
    expect(tlmRows[0]).toEqual(TLM_COLUMNS);
    expect(tlmRows).toHaveLength(3001);
    expect(tlmRows.every((row) => row.length === TLM_COLUMNS.length)).toBe(true);
    expect(tlmRows[3000]![TLM_COLUMNS.indexOf('seq')]).toBe('2999');

    const other = parseCsv(files.messagesCsv);
    expect(other[0]).toEqual(MESSAGE_COLUMNS);
    const kinds = other.slice(1).map((row) => `${row[1]}:${row[2]}`);
    expect(kinds).toContain('tx:cmd');
    expect(kinds).toContain('tx:ping');
    expect(kinds).toContain('rx:ack');
    expect(other.some((row) => row[MESSAGE_COLUMNS.indexOf('reason')] === 'box not still')).toBe(true);
    expect(other.at(-1)![MESSAGE_COLUMNS.indexOf('json')]).toBe('not json');

    const after = recorder.getState().tlmRows;
    socket.message(tlm({ seq: 9999, t_ms: 1, psi_deg: 0, mode: 'SAFE' }));
    expect(recorder.getState().tlmRows).toBe(after);
    expect(files.name).toMatch(/^direcsun-\d{8}-\d{6}$/);
  });
});
