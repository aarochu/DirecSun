export type ParameterInput = 'number' | 'integer' | 'enum';

export interface ParameterField {
  readonly key: string;
  readonly label: string;
  readonly description: string;
  readonly input: ParameterInput;
  readonly minimum?: number;
  readonly maximum?: number;
  readonly options?: readonly number[];
  readonly defaultValue: string;
}

// FD section 16 and protocol/schema-v1.json are the source of truth. Complex
// calibration fields are deferred by issue #63 (the box accepts them since R5;
// ground/console and tools/calibrate.py set them), and the bench keys stay in
// the engineering bench tools rather than the operator UI: bench_mode needs
// Julian's go, and hall_installed, bench_underspeed_rpm and bench_cap_rpm only
// work inside it.
export const PARAMETER_FIELDS: readonly ParameterField[] = [
  { key: 'k_psi', label: 'Heading gain', description: 'k_psi', input: 'number', minimum: 0, maximum: 10, defaultValue: '3.1' },
  { key: 'k_omega', label: 'Rate gain', description: 'k_omega', input: 'number', minimum: 0, maximum: 0.05, defaultValue: '0.0129' },
  { key: 'k_i', label: 'Integral gain', description: 'k_i', input: 'number', minimum: 0, maximum: 0.1, defaultValue: '0.0163' },
  { key: 'rate_gain_scale', label: 'Gain scale for rate loop', description: 'rate_gain_scale · 1 is the design gains', input: 'number', minimum: 0, maximum: 1, defaultValue: '0.2' },
  { key: 'wheel_kp', label: 'Wheel proportional gain', description: 'wheel_kp', input: 'number', minimum: 0, defaultValue: 'from T-5' },
  { key: 'wheel_ki', label: 'Wheel integral gain', description: 'wheel_ki', input: 'number', minimum: 0, defaultValue: 'from T-5' },
  { key: 'omega_low_rpm', label: 'Wheel low limit', description: 'omega_low_rpm · RPM', input: 'number', minimum: 500, maximum: 1500, defaultValue: '800' },
  { key: 'omega_bias_rpm', label: 'Wheel bias speed', description: 'omega_bias_rpm · RPM', input: 'number', minimum: 0, defaultValue: '2400' },
  { key: 's_w', label: 'Wheel sign', description: 's_w', input: 'enum', options: [-1, 1], defaultValue: '+1' },
  { key: 's_gyro', label: 'Gyro sign', description: 's_gyro', input: 'enum', options: [-1, 1], defaultValue: '+1' },
  { key: 'dshot_rate', label: 'DShot rate', description: 'dshot_rate', input: 'enum', options: [150, 300], defaultValue: '150' },
  { key: 'manual_throttle_min', label: 'Manual throttle minimum', description: 'manual_throttle_min', input: 'integer', minimum: 48, maximum: 2047, defaultValue: 'from T-5' },
  { key: 'manual_throttle_max', label: 'Manual throttle maximum', description: 'manual_throttle_max', input: 'integer', minimum: 48, maximum: 2047, defaultValue: 'from T-5' },
  { key: 'spinup_throttle', label: 'Spin-up throttle', description: 'spinup_throttle · open loop', input: 'integer', minimum: 48, maximum: 2047, defaultValue: '300' },
  { key: 'hall_expect_throttle', label: 'Hall expected above', description: 'hall_expect_throttle · below the spin-up throttle', input: 'integer', minimum: 48, maximum: 2047, defaultValue: '200' },
] as const;

export const DEFERRED_PARAMETER_KEYS = [
  'mag_axis_map',
  'mag_cal',
  'sun_cal',
  'light_threshold',
  'bench_mode',
  'hall_installed',
  'bench_underspeed_rpm',
  'bench_cap_rpm',
] as const;

export type ParameterDraft = Readonly<Record<string, string>>;

export interface FeedForwardDraft {
  readonly rpm: string;
  readonly throttle: string;
}

export interface ParsedParameters {
  readonly params: Record<string, unknown>;
  readonly error: string | null;
}

function rangeText(field: ParameterField): string {
  if (field.options) return field.options.join(' or ');
  if (field.minimum !== undefined && field.maximum !== undefined) return `${field.minimum} to ${field.maximum}`;
  if (field.minimum !== undefined) return `${field.minimum} or greater`;
  return 'a valid number';
}

export function parameterRange(field: ParameterField): string {
  return rangeText(field);
}

export function parseParameterChanges(draft: ParameterDraft, ffTable: readonly FeedForwardDraft[]): ParsedParameters {
  const params: Record<string, unknown> = {};
  for (const field of PARAMETER_FIELDS) {
    const raw = draft[field.key]?.trim() ?? '';
    if (raw === '') continue;
    const value = Number(raw);
    const invalid = !Number.isFinite(value)
      || (field.input === 'integer' && !Number.isInteger(value))
      || (field.options !== undefined && !field.options.includes(value))
      || (field.minimum !== undefined && value < field.minimum)
      || (field.maximum !== undefined && value > field.maximum);
    if (invalid) return { params: {}, error: `${field.label} must be ${rangeText(field)}.` };
    params[field.key] = value;
  }

  const touchedTable = ffTable.some((point) => point.rpm.trim() !== '' || point.throttle.trim() !== '');
  if (touchedTable) {
    if (ffTable.length !== 8 || ffTable.some((point) => point.rpm.trim() === '' || point.throttle.trim() === '')) {
      return { params: {}, error: 'Feed-forward table needs all eight RPM and throttle points.' };
    }
    const points = ffTable.map((point) => ({ rpm: Number(point.rpm), throttle: Number(point.throttle) }));
    if (points.some((point) => !Number.isFinite(point.rpm) || point.rpm < 0)) {
      return { params: {}, error: 'Every feed-forward RPM must be 0 or greater.' };
    }
    if (points.some((point) => !Number.isInteger(point.throttle) || point.throttle < 48 || point.throttle > 2047)) {
      return { params: {}, error: 'Every feed-forward throttle must be an integer from 48 to 2047.' };
    }
    if (points.some((point, index) => index > 0 && point.rpm <= points[index - 1]!.rpm)) {
      return { params: {}, error: 'Feed-forward RPM points must be strictly increasing.' };
    }
    params.ff_table = points;
  }

  const throttleMin = params.manual_throttle_min;
  const throttleMax = params.manual_throttle_max;
  if (typeof throttleMin === 'number' && typeof throttleMax === 'number' && throttleMin >= throttleMax) {
    return { params: {}, error: 'Manual throttle minimum must be below the maximum.' };
  }
  if (Object.keys(params).length === 0) return { params, error: 'Change at least one parameter.' };
  return { params, error: null };
}

export function formatApplied(applied: Readonly<Record<string, unknown>>): string {
  return Object.entries(applied)
    .map(([key, value]) => `${key} = ${typeof value === 'object' ? JSON.stringify(value) : String(value)}`)
    .join(' · ');
}
