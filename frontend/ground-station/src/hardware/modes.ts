import type { DisplayTelemetry } from '../ground/displaySource';

export type WireMode = 'SAFE' | 'SPINUP' | 'READY' | 'DETUMBLE' | 'HOLD' | 'SLEW' | 'SUN_POINT' | 'MANUAL';
export type CommandableMode = 'READY' | 'DETUMBLE' | 'HOLD' | 'SLEW' | 'SUN_POINT' | 'MANUAL';
export type ManualInput = 'rate' | 'throttle' | 'wheel';
export type FirmwareSupport = 'available' | 'pending';

export interface ModeSpec {
  readonly wire: WireMode;
  readonly label: string;
  readonly guidance: string;
  readonly enterFrom: readonly WireMode[];
  readonly firmware: FirmwareSupport;
  readonly reference: 'none' | 'heading' | 'sun';
}

export const ARMED_MODES: readonly WireMode[] = ['READY', 'DETUMBLE', 'HOLD', 'SLEW', 'SUN_POINT', 'MANUAL'];

export const MODES: Readonly<Record<WireMode, ModeSpec>> = {
  SAFE: {
    wire: 'SAFE',
    label: 'Safe',
    guidance: 'INSERT THE LOCK PIN, THEN ARM. WHILE ARMED, THE SPACE BAR SENDS E-STOP',
    enterFrom: [],
    firmware: 'available',
    reference: 'none',
  },
  SPINUP: {
    wire: 'SPINUP',
    label: 'Spin-up',
    guidance: 'SPINNING UP THE WHEEL: KEEP THE LOCK PIN IN',
    enterFrom: [],
    firmware: 'available',
    reference: 'none',
  },
  READY: {
    wire: 'READY',
    label: 'Ready',
    guidance: 'READY: PULL THE LOCK PIN, THEN ROTATE. SPACE BAR SENDS E-STOP',
    enterFrom: ['DETUMBLE', 'HOLD', 'SLEW', 'SUN_POINT', 'MANUAL', 'READY'],
    firmware: 'available',
    reference: 'none',
  },
  DETUMBLE: {
    wire: 'DETUMBLE',
    label: 'Detumble',
    guidance: 'DETUMBLE: THE BOX DAMPS ITS ROTATION TO ZERO. SPACE BAR SENDS E-STOP',
    enterFrom: ['READY'],
    firmware: 'pending',
    reference: 'none',
  },
  HOLD: {
    wire: 'HOLD',
    label: 'Hold heading',
    guidance: 'HOLD: KEEPING THE HEADING THE BOX HAD AT ENTRY. SPACE BAR SENDS E-STOP',
    enterFrom: ['READY'],
    firmware: 'available',
    reference: 'heading',
  },
  SLEW: {
    wire: 'SLEW',
    label: 'Slew',
    guidance: 'SLEW: TURNING TO THE TARGET. SPACE BAR SENDS E-STOP',
    enterFrom: ['READY'],
    firmware: 'available',
    reference: 'heading',
  },
  SUN_POINT: {
    wire: 'SUN_POINT',
    label: 'Sun point',
    guidance: 'SUN POINT: TURNING TO FACE THE LIGHT. THE BOX RETURNS TO READY IF THE SUN IS LOST FOR 0.5 S',
    enterFrom: ['READY'],
    firmware: 'pending',
    reference: 'sun',
  },
  MANUAL: {
    wire: 'MANUAL',
    label: 'Manual',
    guidance: 'MANUAL: THE SLIDER SETS THE SETPOINT. SPACE BAR SENDS E-STOP',
    enterFrom: ['READY'],
    firmware: 'available',
    reference: 'none',
  },
};

export interface ModeChoice {
  readonly label: string;
  readonly mode: CommandableMode;
  readonly input?: ManualInput;
  readonly needsTarget?: boolean;
}

export const MODE_CHOICES: readonly ModeChoice[] = [
  { label: 'Ready', mode: 'READY' },
  { label: 'Hold heading', mode: 'HOLD' },
  { label: 'Slew to heading…', mode: 'SLEW', needsTarget: true },
  { label: 'Manual: rate', mode: 'MANUAL', input: 'rate' },
  { label: 'Manual: throttle', mode: 'MANUAL', input: 'throttle' },
  { label: 'Manual: wheel speed', mode: 'MANUAL', input: 'wheel' },
  { label: 'Detumble', mode: 'DETUMBLE' },
  { label: 'Sun point', mode: 'SUN_POINT' },
];

export function modeCommand(choice: Pick<ModeChoice, 'mode' | 'input'> & { psiRefDeg?: number }): Record<string, unknown> {
  if (choice.mode === 'MANUAL') return { mode: choice.mode, input: choice.input };
  if (choice.mode === 'SLEW') return { mode: choice.mode, psi_ref_deg: choice.psiRefDeg };
  return { mode: choice.mode };
}

export const MANUAL_NUDGE: Readonly<Record<ManualInput, number>> = { rate: 5, throttle: 10, wheel: 100 };

export function canEnter(mode: CommandableMode, from: string | null): boolean {
  return from !== null && (MODES[mode].enterFrom as readonly string[]).includes(from);
}

export function modeSpec(mode: string | null): ModeSpec | null {
  return mode !== null && mode in MODES ? MODES[mode as WireMode] : null;
}

export const REFUSAL_HINTS: Readonly<Record<string, string>> = {
  'not implemented': 'THE FIRMWARE DOES NOT RUN THIS YET',
  'not in READY': 'RETURN TO READY FIRST',
  'not armed': 'ARM THE BOX FIRST',
  'wheel not at bias': 'WAIT FOR THE SPIN-UP TO FINISH',
  'wheel loop not available': 'THE BOX HAS NO CLOSED WHEEL LOOP (NO HALL SENSOR OR NO FEED-FORWARD TABLE)',
  'input not available': 'THIS BOX DOES NOT OFFER THAT MANUAL INPUT',
  'sun not valid': 'NO VALID SUN READING: LIGHT THE SUN SENSORS FIRST',
  'not controlling client': 'ANOTHER CLIENT IS CONTROLLING THE BOX',
};

export const AUTO_EXITS: Readonly<Record<string, { readonly tone: 'ok' | 'error'; readonly text: string }>> = {
  slew_complete: { tone: 'ok', text: 'SLEW COMPLETE: HOLDING AT THE TARGET HEADING IN READY.' },
  SLEW_TIMEOUT: { tone: 'error', text: 'SLEW TIMED OUT: THE BOX DID NOT SETTLE WITHIN 6 S.' },
  sun_invalid: { tone: 'error', text: 'SUN POINT ENDED: THE SUN READING WAS INVALID FOR 0.5 S.' },
};

export type GuardReadout = Pick<DisplayTelemetry, 'flags' | 'rateDegS' | 'batteryV' | 'sensors'>;

export const ARM_MAX_RATE_DPS = 5;
export const ARM_MIN_BATTERY_V = 9.6;
export const RESPIN_RANGE_RPM: readonly [number, number] = [1000, 3800];
export const RESPIN_FIRMWARE: FirmwareSupport = 'pending';

const turning = (readout: GuardReadout) => readout.rateDegS !== null && Math.abs(readout.rateDegS) >= ARM_MAX_RATE_DPS;

export function choiceBlock(choice: ModeChoice, readout: GuardReadout): string | null {
  if (choice.mode === 'SUN_POINT' && readout.sensors?.sunValid !== true) return 'No valid sun';
  if (choice.mode === 'MANUAL' && choice.input !== 'throttle' && readout.flags.includes('HALL_BYPASS')) return 'Hall bypassed';
  return null;
}

export function armBlock(readout: GuardReadout): string | null {
  if (turning(readout)) return `THE BOX IS TURNING AT ${Math.abs(readout.rateDegS!).toFixed(1)} DEG/S: HOLD IT STILL (UNDER ${ARM_MAX_RATE_DPS})`;
  if (readout.batteryV !== null && readout.batteryV <= ARM_MIN_BATTERY_V) {
    return `BATTERY AT ${readout.batteryV.toFixed(2)} V: ARMING NEEDS MORE THAN ${ARM_MIN_BATTERY_V} V`;
  }
  return null;
}

export function respinBlock(mode: string | null, readout: GuardReadout): string | null {
  if (mode !== 'READY') return 'Ready only';
  if (turning(readout)) return 'Box turning';
  return null;
}

export const NEAR_LIMIT_GUIDANCE = 'WHEEL NEAR ITS LIMIT: RETURN TO READY, PIN THE BOX, THEN RESPIN FROM THE MODE MENU';
