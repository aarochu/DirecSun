import { describe, expect, it } from 'vitest';
import { formatHeading, formatRate, formatRpm } from '../lib/format';
import { normalizeDeg, stepToward } from '../simulation/physics';
import {
  commandRotate,
  controlAvailability,
  createInitialState,
  PHASE_STATUS,
  step,
  stopSequence,
} from '../simulation/stateMachine';
import type { SimPhase } from '../simulation/types';

const PHASES: SimPhase[] = ['READY', 'ROTATING_OUT', 'SETTLING_OUT', 'RETURNING', 'SETTLING_HOME', 'STOPPED'];

describe('UI status table', () => {
  it.each([
    ['READY', 'READY / SIMULATED', true, false],
    ['ROTATING_OUT', 'ROTATING / SIMULATED', false, true],
    ['SETTLING_OUT', 'HOLDING TARGET / SIMULATED', false, true],
    ['RETURNING', 'RETURNING / SIMULATED', false, true],
    ['SETTLING_HOME', 'SETTLING / SIMULATED', false, true],
    ['STOPPED', 'STOPPED / SIMULATED', false, false],
  ] as const)('%s → "%s", rotate=%s, stop=%s, reset always', (phase, text, rotate, stop) => {
    expect(PHASE_STATUS[phase]).toBe(text);
    expect(controlAvailability(phase)).toEqual({ rotate, stop, reset: true });
  });

  it('never uses live-hardware wording', () => {
    for (const phase of PHASES) {
      expect(PHASE_STATUS[phase]).not.toMatch(/\b(CONNECTED|ONLINE|LIVE|ARMED|NOMINAL|FLIGHT READY)\b/);
    }
  });
});

describe('transitions', () => {
  it('starts at 0°, READY, wheel bias 2400 rpm, no battery value', () => {
    expect(createInitialState()).toEqual({
      source: 'simulation',
      phase: 'READY',
      timeS: 0,
      headingDeg: 0,
      targetDeg: 0,
      rateDegS: 0,
      originDeg: null,
      holdRemainingS: 0,
      wheelRpm: 2400,
      wheelLimited: false,
      lastCommand: null,
    });
  });

  it('accepts a command only from READY', () => {
    const ready = createInitialState();
    const running = commandRotate(ready, 1);
    expect(running.phase).toBe('ROTATING_OUT');
    expect(commandRotate(running, -1)).toBe(running);

    const stopped = stopSequence(running);
    expect(stopped.phase).toBe('STOPPED');
    expect(commandRotate(stopped, 1)).toBe(stopped);
  });

  it('stop is a no-op when nothing is running', () => {
    const ready = createInitialState();
    expect(stopSequence(ready)).toBe(ready);
    const stopped = stopSequence(commandRotate(ready, 1));
    expect(stopSequence(stopped)).toBe(stopped);
  });

  it('step does nothing in READY or STOPPED (time does not advance)', () => {
    const ready = createInitialState();
    expect(step(ready, 1)).toBe(ready);
    const stopped = stopSequence(commandRotate(ready, 1));
    expect(step(stopped, 1)).toBe(stopped);
  });

  it('decelerates in the negative direction without overshoot', () => {
    let heading = 0;
    let rate = 0;
    let arrived = false;
    let minHeading = 0;
    for (let i = 0; i < 1000 && !arrived; i++) {
      ({ headingDeg: heading, rateDegS: rate, arrived } = stepToward(heading, rate, -90, 1 / 120));
      minHeading = Math.min(minHeading, heading);
    }
    expect(arrived).toBe(true);
    expect(heading).toBe(-90);
    expect(rate).toBe(0);
    expect(minHeading).toBeGreaterThanOrEqual(-90);
  });
});

describe('display formatting', () => {
  it('normalizes headings to (−180°, 180°] for display only', () => {
    expect(normalizeDeg(265)).toBe(-95);
    expect(normalizeDeg(-90)).toBe(-90);
    expect(normalizeDeg(180)).toBe(180);
    expect(normalizeDeg(-180)).toBe(180);
    expect(normalizeDeg(720)).toBe(0);
  });

  it('formats instrument values with units and no negative zero', () => {
    expect(formatHeading(0)).toBe('0.0°');
    expect(formatHeading(-0.01)).toBe('0.0°');
    expect(formatHeading(90)).toBe('+90.0°');
    expect(formatHeading(-90)).toBe('−90.0°');
    expect(formatHeading(265)).toBe('−95.0°');
    expect(formatRate(0)).toBe('0.0°/s');
    expect(formatRpm(2400)).toBe('2400 RPM');
  });
});
