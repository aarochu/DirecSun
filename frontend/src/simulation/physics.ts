import {
  ARRIVAL_ANGLE_TOL_DEG,
  ARRIVAL_RATE_TOL_DEG_S,
  BODY_INERTIA_KG_M2,
  MAX_ACCEL_DEG_S2,
  MAX_RATE_DEG_S,
  WHEEL_BIAS_RPM,
  WHEEL_INERTIA_KG_M2,
  WHEEL_MAX_RPM,
  WHEEL_MIN_RPM,
  WHEEL_ORIENTATION,
} from './constants';

export const clamp = (value: number, min: number, max: number) =>
  Math.min(max, Math.max(min, value));

export const degPerSecToRpm = (degPerSec: number) => degPerSec / 6;

export const WHEEL_TO_BODY_RATIO = (BODY_INERTIA_KG_M2 + WHEEL_INERTIA_KG_M2) / WHEEL_INERTIA_KG_M2;

export function wheelRpmForBodyRate(rateDegS: number): { rpm: number; limited: boolean } {
  const raw = WHEEL_BIAS_RPM - WHEEL_ORIENTATION * WHEEL_TO_BODY_RATIO * degPerSecToRpm(rateDegS);
  const rpm = clamp(raw, WHEEL_MIN_RPM, WHEEL_MAX_RPM);
  return { rpm, limited: rpm !== raw };
}

export function normalizeDeg(deg: number): number {
  let wrapped = ((deg % 360) + 360) % 360;
  if (wrapped > 180) wrapped -= 360;
  return Object.is(wrapped, -0) ? 0 : wrapped;
}

const LANDING_EPSILON_DEG = 1e-9;

export interface MotionStep {
  headingDeg: number;
  rateDegS: number;
  arrived: boolean;
}

export function stepToward(
  headingDeg: number,
  rateDegS: number,
  targetDeg: number,
  dt: number,
): MotionStep {
  const error = targetDeg - headingDeg;
  if (Math.abs(error) <= ARRIVAL_ANGLE_TOL_DEG && Math.abs(rateDegS) <= ARRIVAL_RATE_TOL_DEG_S) {
    return { headingDeg: targetDeg, rateDegS: 0, arrived: true };
  }

  const maxDelta = MAX_ACCEL_DEG_S2 * dt;
  const brakingRate = Math.sqrt((maxDelta / 2) ** 2 + 2 * MAX_ACCEL_DEG_S2 * Math.abs(error)) - maxDelta / 2;
  const desiredRate = Math.sign(error) * Math.min(MAX_RATE_DEG_S, brakingRate);
  const nextRate = clamp(
    rateDegS + clamp(desiredRate - rateDegS, -maxDelta, maxDelta),
    -MAX_RATE_DEG_S,
    MAX_RATE_DEG_S,
  );
  const nextHeading = headingDeg + nextRate * dt;
  const nextError = targetDeg - nextHeading;

  if (Math.abs(nextError) <= LANDING_EPSILON_DEG) {
    return { headingDeg: targetDeg, rateDegS: nextRate, arrived: false };
  }
  if (Math.sign(nextError) !== Math.sign(error)) {
    return { headingDeg: targetDeg, rateDegS: 0, arrived: true };
  }
  return { headingDeg: nextHeading, rateDegS: nextRate, arrived: false };
}
