import { normalizeDeg } from '../simulation/physics';

const MINUS = '−';

export function formatSigned(value: number, digits = 1): string {
  const rounded = Number(value.toFixed(digits));
  const magnitude = Math.abs(rounded).toFixed(digits);
  if (rounded === 0) return magnitude;
  return rounded > 0 ? `+${magnitude}` : `${MINUS}${magnitude}`;
}

export const formatHeading = (deg: number) => `${formatSigned(normalizeDeg(deg))}°`;

export const formatRate = (degPerSec: number) => `${formatSigned(degPerSec)}°/s`;

export const formatRpm = (rpm: number) => `${Math.round(rpm)} RPM`;

export function spokenHeading(deg: number): string {
  const value = Number(normalizeDeg(deg).toFixed(1));
  if (value === 0) return '0 degrees';
  return `${value > 0 ? 'plus' : 'minus'} ${Math.abs(value)} degrees`;
}
