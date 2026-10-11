import { afterEach, describe, expect, it } from 'vitest';
import {
  ARRIVAL_ANGLE_TOL_DEG,
  HOLD_AT_TARGET_S,
  MAX_ACCEL_DEG_S2,
  MAX_RATE_DEG_S,
  SETTLE_AT_HOME_S,
  SIM_STEP_S,
  WHEEL_BIAS_RPM,
} from '../simulation/constants';
import { WHEEL_TO_BODY_RATIO, wheelRpmForBodyRate } from '../simulation/physics';
import {
  advancePlant,
  applyToPlant,
  commandRotate,
  createInitialState,
  createPlant,
  step,
} from '../simulation/stateMachine';
import type { Direction, SimPhase, SimState } from '../simulation/types';
import { MockPlant } from '../simulation/useMockPlant';
import { ManualClock } from './clock';

const plants: MockPlant[] = [];

function setup(initialHeadingDeg = 0) {
  const clock = new ManualClock();
  const plant = new MockPlant(clock, initialHeadingDeg);
  plant.attach();
  plants.push(plant);
  return { clock, plant, state: () => plant.getSnapshot().state };
}

afterEach(() => {
  for (const plant of plants.splice(0)) plant.detach();
});

function runUntil(
  clock: ManualClock,
  plant: MockPlant,
  done: (state: SimState) => boolean,
  limitS = 20,
): SimState[] {
  const trace: SimState[] = [];
  const frames = Math.round(limitS * 60);
  for (let i = 0; i < frames; i++) {
    clock.frame();
    const state = plant.getSnapshot().state;
    trace.push(state);
    if (done(state)) return trace;
  }
  throw new Error(`condition not reached within ${limitS}s`);
}

function transitions(direction: Direction, initialHeadingDeg = 0) {
  let state = commandRotate(createInitialState(initialHeadingDeg), direction);
  const entered: Partial<Record<SimPhase, SimState>> = { ROTATING_OUT: state };
  const all: SimState[] = [state];
  for (let i = 0; i < 20 / SIM_STEP_S && state.phase !== 'READY'; i++) {
    const next = step(state, SIM_STEP_S);
    if (next.phase !== state.phase) entered[next.phase] = next;
    state = next;
    all.push(state);
  }
  return { entered, all, final: state };
}

describe('judge sequence', () => {
  it.each([
    ['LEFT_90_FROM_ZERO', -1, -90],
    ['RIGHT_90_FROM_ZERO', 1, 90],
  ] as const)('%s: turns, holds 1.0 s, returns to 0°, READY at rest', (_name, direction, target) => {
    const { clock, plant, state } = setup();

    expect(plant.rotate(direction)).toBe(true);
    expect(state()).toMatchObject({ phase: 'ROTATING_OUT', originDeg: 0, targetDeg: target });

    const trace = runUntil(clock, plant, (s) => s.phase === 'READY');
    const atTarget = trace.find((s) => s.phase === 'SETTLING_OUT')!;
    expect(Math.abs(atTarget.headingDeg - target)).toBeLessThanOrEqual(ARRIVAL_ANGLE_TOL_DEG);
    expect(atTarget.rateDegS).toBe(0);

    const returning = trace.find((s) => s.phase === 'RETURNING')!;
    expect(returning.targetDeg).toBe(0);

    const final = state();
    expect(final.phase).toBe('READY');
    expect(Math.abs(final.headingDeg)).toBeLessThanOrEqual(ARRIVAL_ANGLE_TOL_DEG);
    expect(final.rateDegS).toBe(0);
    expect(final.originDeg).toBeNull();
    expect(clock.pendingFrames).toBe(0);

    for (const s of trace) {
      expect(Math.abs(s.headingDeg)).toBeLessThanOrEqual(90 + 1e-9);
      expect(Math.sign(s.headingDeg) === -Math.sign(target)).toBe(false);
    }
  });

  it('holds exactly 1.0 s at the target and 0.5 s at home', () => {
    const { entered } = transitions(1);
    const hold = entered.RETURNING!.timeS - entered.SETTLING_OUT!.timeS;
    const settle = entered.READY!.timeS - entered.SETTLING_HOME!.timeS;
    expect(hold).toBeCloseTo(HOLD_AT_TARGET_S, 6);
    expect(settle).toBeCloseTo(SETTLE_AT_HOME_S, 6);
  });

  it('a 90° move takes about 2 s under the 60°/s and 120°/s² limits', () => {
    const { entered } = transitions(1);
    expect(entered.SETTLING_OUT!.timeS).toBeGreaterThan(1.95);
    expect(entered.SETTLING_OUT!.timeS).toBeLessThan(2.1);
  });

  it('respects the rate and acceleration limits at every step', () => {
    const { all } = transitions(-1);
    for (let i = 1; i < all.length; i++) {
      expect(Math.abs(all[i]!.rateDegS)).toBeLessThanOrEqual(MAX_RATE_DEG_S);
      const accel = Math.abs(all[i]!.rateDegS - all[i - 1]!.rateDegS) / SIM_STEP_S;
      expect(accel).toBeLessThanOrEqual(MAX_ACCEL_DEG_S2 + 1e-6);
    }
    expect(Math.max(...all.map((s) => Math.abs(s.rateDegS)))).toBeCloseTo(MAX_RATE_DEG_S, 6);
  });

  it('RETURN_TO_NONZERO_ORIGIN: returns to +175° unwrapped, not 0° and not via 360°', () => {
    const { clock, plant, state } = setup(175);
    plant.rotate(1);
    expect(state().targetDeg).toBe(265);
    const trace = runUntil(clock, plant, (s) => s.phase === 'READY');
    for (const s of trace) {
      expect(s.headingDeg).toBeGreaterThanOrEqual(175 - 1e-9);
      expect(s.headingDeg).toBeLessThanOrEqual(265 + 1e-9);
    }
    expect(state().headingDeg).toBeCloseTo(175, 9);
  });

  it('turns the short way from a start near 350°', () => {
    const { clock, plant, state } = setup(350);
    plant.rotate(1);
    const trace = runUntil(clock, plant, (s) => s.phase === 'SETTLING_OUT');
    expect(state().headingDeg).toBeCloseTo(440, 9);
    expect(Math.min(...trace.map((s) => s.headingDeg))).toBeGreaterThanOrEqual(350);
  });

  it('ten consecutive commands accumulate no drift', () => {
    const { clock, plant, state } = setup();
    for (let i = 0; i < 10; i++) {
      expect(plant.rotate(i % 2 === 0 ? 1 : -1)).toBe(true);
      runUntil(clock, plant, (s) => s.phase === 'READY');
      expect(state().headingDeg).toBe(0);
      expect(state().rateDegS).toBe(0);
    }
  });

  it('REJECT_DOUBLE_CLICK: only one motion transaction runs', () => {
    const { plant, state } = setup();
    expect(plant.rotate(1)).toBe(true);
    expect(plant.rotate(1)).toBe(false);
    expect(plant.rotate(-1)).toBe(false);
    expect(state()).toMatchObject({ targetDeg: 90, originDeg: 0, lastCommand: 1 });
  });
});

describe('stop and reset', () => {
  it('STOP_DURING_OUTWARD: STOPPED, zero rate, no pending return', () => {
    const { clock, plant, state } = setup();
    plant.rotate(1);
    clock.run(0.8);
    expect(state().phase).toBe('ROTATING_OUT');
    expect(plant.stop()).toBe(true);

    const stopped = state();
    expect(stopped).toMatchObject({ phase: 'STOPPED', rateDegS: 0, originDeg: null });
    expect(stopped.targetDeg).toBe(stopped.headingDeg);
    expect(stopped.wheelRpm).toBe(WHEEL_BIAS_RPM);
    expect(clock.pendingFrames).toBe(0);

    clock.run(10);
    expect(state()).toBe(stopped);
    expect(plant.rotate(1)).toBe(false);
  });

  it('STOP_DURING_DWELL: halts at the target and never returns until reset', () => {
    const { clock, plant, state } = setup();
    plant.rotate(1);
    runUntil(clock, plant, (s) => s.phase === 'SETTLING_OUT');
    plant.stop();
    clock.run(10);
    expect(state()).toMatchObject({ phase: 'STOPPED', headingDeg: 90, targetDeg: 90 });

    plant.reset();
    expect(state()).toEqual(createInitialState());
  });

  it('RESET_DURING_RETURN: deterministic initial state; nothing later alters it', () => {
    const { clock, plant, state } = setup();
    plant.rotate(-1);
    runUntil(clock, plant, (s) => s.phase === 'RETURNING');
    clock.run(0.3);
    plant.reset();

    const initial = createPlant(0);
    expect(plant.getSnapshot().state).toEqual(initial.state);
    expect(plant.getSnapshot().history).toEqual(initial.history);
    expect(clock.pendingFrames).toBe(0);

    clock.run(10);
    expect(state()).toEqual(initial.state);
  });

  it('detach (unmount) cancels the frame loop mid-sequence', () => {
    const { clock, plant, state } = setup();
    plant.rotate(1);
    clock.run(0.5);
    expect(clock.pendingFrames).toBe(1);
    plant.detach();
    expect(clock.pendingFrames).toBe(0);
    const frozen = state();
    clock.run(5);
    expect(state()).toBe(frozen);
  });
});

describe('time handling', () => {
  it('caps a long frame gap at 0.1 s of simulated time', () => {
    const plant = applyToPlant(createPlant(), commandRotate(createInitialState(), 1));
    const next = advancePlant(plant, 30);
    expect(next.state.timeS).toBeLessThanOrEqual(0.1 + 1e-9);
  });

  it('stops integrating while the page is hidden and resumes without a jump', () => {
    const { clock, plant, state } = setup();
    let visibility: DocumentVisibilityState = 'visible';
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => visibility });
    try {
      plant.rotate(1);
      clock.run(0.5);
      const before = state().timeS;

      visibility = 'hidden';
      document.dispatchEvent(new Event('visibilitychange'));
      expect(clock.pendingFrames).toBe(0);
      clock.nowMs += 60_000;

      visibility = 'visible';
      document.dispatchEvent(new Event('visibilitychange'));
      clock.frame();
      expect(state().timeS - before).toBeLessThanOrEqual(1 / 60 + SIM_STEP_S);
    } finally {
      delete (document as { visibilityState?: unknown }).visibilityState;
    }
  });

  it('telemetry channel: the same snapshot as the model, at most 20 Hz plus phase changes', () => {
    const { clock, plant } = setup();
    const emittedAt: number[] = [];
    plant.subscribeTelemetry(() => {
      emittedAt.push(clock.nowMs);
      expect(plant.getTelemetry()).toBe(plant.getSnapshot());
    });
    const start = clock.nowMs;
    plant.rotate(1);
    runUntil(clock, plant, (s) => s.phase === 'READY');
    const durationMs = clock.nowMs - start;
    expect(emittedAt.length).toBeLessThanOrEqual(Math.ceil(durationMs / 50) + 5);
    expect(emittedAt.length).toBeGreaterThan(durationMs / 50 / 2);
    expect(plant.getTelemetry()).toBe(plant.getSnapshot());
  });

  it('samples history at 10 Hz and keeps a 15 s window', () => {
    const { clock, plant } = setup();
    for (let i = 0; i < 4; i++) {
      plant.rotate(1);
      runUntil(clock, plant, (s) => s.phase === 'READY');
    }
    const { state, history } = plant.getSnapshot();
    expect(state.timeS).toBeGreaterThan(20);
    const newest = history[history.length - 1]!.t;
    expect(history[0]!.t).toBeGreaterThanOrEqual(newest - 15.1 - 1e-9);
    expect(history[0]!.t).toBeLessThanOrEqual(state.timeS - 15);
    const gaps = history.slice(1).map((s, i) => s.t - history[i]!.t);
    for (const gap of gaps) expect(gap).toBeCloseTo(0.1, 6);
  });
});

describe('WHEEL_MOMENTUM_RELATION', () => {
  it('wheel speed follows ΔΩ = −((I_b + I_w)/I_w)·Δω_body at every frame', () => {
    const { clock, plant, state } = setup();
    expect(state().wheelRpm).toBe(WHEEL_BIAS_RPM);
    plant.rotate(1);
    const trace = runUntil(clock, plant, (s) => s.phase === 'READY');
    for (const s of trace) {
      const expected = WHEEL_BIAS_RPM - WHEEL_TO_BODY_RATIO * (s.rateDegS / 6);
      expect(s.wheelRpm).toBeCloseTo(expected, 9);
      expect(s.wheelLimited).toBe(false);
    }
    expect(Math.min(...trace.map((s) => s.wheelRpm))).toBeCloseTo(WHEEL_BIAS_RPM - WHEEL_TO_BODY_RATIO * 10, 6);
    expect(state().wheelRpm).toBe(WHEEL_BIAS_RPM);
  });

  it('clamps to the simulated envelope and flags it instead of exceeding it', () => {
    expect(wheelRpmForBodyRate(1000)).toEqual({ rpm: 800, limited: true });
    expect(wheelRpmForBodyRate(-1000)).toEqual({ rpm: 4000, limited: true });
  });
});
