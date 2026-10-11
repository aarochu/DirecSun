import { describe, expect, it } from 'vitest';
import schemaJson from '../../../../protocol/schema-v1.json';
import {
  DEFERRED_PARAMETER_KEYS,
  formatApplied,
  PARAMETER_FIELDS,
  parseParameterChanges,
} from '../hardware/parameters';

interface SchemaProperty {
  readonly type?: string;
  readonly minimum?: number;
  readonly maximum?: number;
  readonly enum?: readonly number[];
}

type Schema = { $defs: { params: { properties: Record<string, SchemaProperty> } } };
const properties = (schemaJson as unknown as Schema).$defs.params.properties;
const blankTable = () => Array.from({ length: 8 }, () => ({ rpm: '', throttle: '' }));

describe('parameter panel against FD section 16 and protocol/schema-v1.json', () => {
  it('offers every non-deferred, non-bench schema parameter', () => {
    const offered = [...PARAMETER_FIELDS.map((field) => field.key), 'ff_table'].sort();
    const expected = Object.keys(properties).filter((key) => !(DEFERRED_PARAMETER_KEYS as readonly string[]).includes(key)).sort();
    expect(offered).toEqual(expected);
  });

  it('keeps the scalar ranges and enums equal to the schema', () => {
    for (const field of PARAMETER_FIELDS) {
      const property = properties[field.key]!;
      expect(field.minimum, `${field.key} minimum`).toBe(property.minimum);
      expect(field.maximum, `${field.key} maximum`).toBe(property.maximum);
      expect(field.options, `${field.key} enum`).toEqual(property.enum);
      if (field.input === 'integer') expect(property.type).toBe('integer');
    }
  });

  it('builds an all-or-nothing changed set and validates the eight-point table', () => {
    expect(parseParameterChanges({ k_psi: '', k_omega: '0.02', s_w: '-1' }, blankTable())).toEqual({
      params: { k_omega: 0.02, s_w: -1 },
      error: null,
    });
    expect(parseParameterChanges({ k_omega: '0.051' }, blankTable()).error).toBe('Rate gain must be 0 to 0.05.');

    const table = Array.from({ length: 8 }, (_, index) => ({ rpm: String(index * 400), throttle: String(100 + index * 10) }));
    expect(parseParameterChanges({}, table)).toEqual({
      params: { ff_table: table.map((point) => ({ rpm: Number(point.rpm), throttle: Number(point.throttle) })) },
      error: null,
    });
    table[4] = { rpm: table[3]!.rpm, throttle: '140' };
    expect(parseParameterChanges({}, table).error).toBe('Feed-forward RPM points must be strictly increasing.');
  });

  it('formats the applied set without hiding nested values', () => {
    expect(formatApplied({ k_omega: 0.0129, ff_table: [{ rpm: 800, throttle: 120 }] }))
      .toBe('k_omega = 0.0129 · ff_table = [{"rpm":800,"throttle":120}]');
  });
});
