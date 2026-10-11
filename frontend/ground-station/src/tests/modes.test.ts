import { describe, expect, it } from 'vitest';
import schemaJson from '../../../../protocol/schema-v1.json';
import { AUTO_EXITS, canEnter, MODE_CHOICES, modeCommand, MODES, type CommandableMode } from '../hardware/modes';

type Schema = { $defs: Record<string, { enum?: string[]; properties?: Record<string, unknown>; anyOf?: { enum?: string[] }[] }> };
const schema = schemaJson as unknown as Schema;
const defs = schema.$defs;

describe('mode registry against protocol/schema-v1.json', () => {
  it('names exactly the wire modes of the schema', () => {
    expect(Object.keys(MODES).sort()).toEqual([...defs.mode!.enum!].sort());
    for (const [key, spec] of Object.entries(MODES)) expect(spec.wire).toBe(key);
  });

  it('offers every commandable mode except SLEW (the rotate buttons own it) in the menu', () => {
    const menu = new Set(MODE_CHOICES.map((choice) => choice.mode));
    const commandable = defs.commandable_mode!.enum!.filter((mode) => mode !== 'SLEW');
    expect([...menu].sort()).toEqual([...commandable].sort());
    const inputs = MODE_CHOICES.filter((choice) => choice.mode === 'MANUAL').map((choice) => choice.input);
    expect(inputs.sort()).toEqual([...defs.manual_input!.enum!].sort());
  });

  it('builds mode commands that follow the cmd_mode rules', () => {
    const allowed = new Set(Object.keys(defs.cmd_mode!.properties!));
    for (const choice of MODE_CHOICES) {
      const frame = { v: 1, type: 'cmd', seq: 1, cmd: 'mode', ...modeCommand(choice) };
      for (const key of Object.keys(frame)) expect(allowed.has(key), `${choice.label}: ${key}`).toBe(true);
      expect('input' in frame, choice.label).toBe(choice.mode === 'MANUAL');
      expect('psi_ref_deg' in frame, choice.label).toBe(false);
    }
  });

  it('names an automatic exit only for causes the schema defines', () => {
    const causes = defs.event_mode!.properties!.cause as { anyOf: { enum?: string[] }[] };
    const automatic = causes.anyOf.flatMap((option) => option.enum ?? []);
    for (const cause of Object.keys(AUTO_EXITS)) expect(automatic).toContain(cause);
  });
});

describe('mode registry against the firmware mode machine (FD section 13.1)', () => {
  const fromReady: CommandableMode[] = ['DETUMBLE', 'HOLD', 'SUN_POINT', 'MANUAL'];

  it('enters the demonstration modes from READY only', () => {
    for (const mode of fromReady) {
      expect(canEnter(mode, 'READY'), mode).toBe(true);
      for (const other of ['SAFE', 'SPINUP', 'DETUMBLE', 'HOLD', 'SLEW', 'SUN_POINT', 'MANUAL', null]) {
        expect(canEnter(mode, other), `${mode} from ${other}`).toBe(false);
      }
    }
  });

  it('returns to READY from every armed mode but not from SAFE or SPINUP', () => {
    for (const mode of ['DETUMBLE', 'HOLD', 'SLEW', 'SUN_POINT', 'MANUAL']) expect(canEnter('READY', mode), mode).toBe(true);
    expect(canEnter('READY', 'SAFE')).toBe(false);
    expect(canEnter('READY', 'SPINUP')).toBe(false);
  });
});
