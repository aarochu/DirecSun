import {
  COMMAND_STEP_DEG,
  HISTORY_SAMPLE_HZ,
  HISTORY_WINDOW_S,
  HOLD_AT_TARGET_S,
  MAX_FRAME_ELAPSED_S,
  SETTLE_AT_HOME_S,
  SIM_STEP_S,
} from './constants';
import { stepToward, wheelRpmForBodyRate } from './physics';
import type { Direction, HistorySample, PlantSnapshot, SimPhase, SimState } from './types';

export const PHASE_STATUS: Record<SimPhase, string> = {
  READY: 'READY / SIMULATED',
  ROTATING_OUT: 'ROTATING / SIMULATED',
  SETTLING_OUT: 'HOLDING TARGET / SIMULATED',
  RETURNING: 'RETURNING / SIMULATED',
  SETTLING_HOME: 'SETTLING / SIMULATED',
  STOPPED: 'STOPPED / SIMULATED',
};

export function isSequenceRunning(phase: SimPhase): boolean {
  return phase !== 'READY' && phase !== 'STOPPED';
}

export interface ControlAvailability {
  rotate: boolean;
  stop: boolean;
  reset: boolean;
}

export function controlAvailability(phase: SimPhase): ControlAvailability {
  return { rotate: phase === 'READY', stop: isSequenceRunning(phase), reset: true };
}

export function createInitialState(headingDeg = 0): SimState {
  const wheel = wheelRpmForBodyRate(0);
  return {
    source: 'simulation',
    phase: 'READY',
    timeS: 0,
    headingDeg,
    targetDeg: headingDeg,
    rateDegS: 0,
    originDeg: null,
    holdRemainingS: 0,
    wheelRpm: wheel.rpm,
    wheelLimited: wheel.limited,
    lastCommand: null,
  };
}

export function commandRotate(state: SimState, direction: Direction): SimState {
  if (state.phase !== 'READY') return state;
  const originDeg = state.headingDeg;
  return {
    ...state,
    phase: 'ROTATING_OUT',
    originDeg,
    targetDeg: originDeg + direction * COMMAND_STEP_DEG,
    holdRemainingS: 0,
    lastCommand: direction,
  };
}

export function stopSequence(state: SimState): SimState {
  if (!isSequenceRunning(state.phase)) return state;
  const wheel = wheelRpmForBodyRate(0);
  return {
    ...state,
    phase: 'STOPPED',
    targetDeg: state.headingDeg,
    rateDegS: 0,
    originDeg: null,
    holdRemainingS: 0,
    wheelRpm: wheel.rpm,
    wheelLimited: wheel.limited,
  };
}

const HOLD_EPSILON_S = 1e-9;

export function step(state: SimState, dt: number): SimState {
  if (!isSequenceRunning(state.phase)) return state;
  const timeS = state.timeS + dt;

  switch (state.phase) {
    case 'ROTATING_OUT':
    case 'RETURNING': {
      const motion = stepToward(state.headingDeg, state.rateDegS, state.targetDeg, dt);
      const wheel = wheelRpmForBodyRate(motion.rateDegS);
      const next: SimState = {
        ...state,
        timeS,
        headingDeg: motion.headingDeg,
        rateDegS: motion.rateDegS,
        wheelRpm: wheel.rpm,
        wheelLimited: wheel.limited,
      };
      if (!motion.arrived) return next;
      return state.phase === 'ROTATING_OUT'
        ? { ...next, phase: 'SETTLING_OUT', holdRemainingS: HOLD_AT_TARGET_S }
        : { ...next, phase: 'SETTLING_HOME', holdRemainingS: SETTLE_AT_HOME_S };
    }
    case 'SETTLING_OUT':
    case 'SETTLING_HOME': {
      const holdRemainingS = state.holdRemainingS - dt;
      if (holdRemainingS > HOLD_EPSILON_S) return { ...state, timeS, holdRemainingS };
      if (state.phase === 'SETTLING_OUT') {
        return {
          ...state,
          timeS,
          holdRemainingS: 0,
          phase: 'RETURNING',
          targetDeg: state.originDeg ?? state.headingDeg,
        };
      }
      return { ...state, timeS, holdRemainingS: 0, phase: 'READY', originDeg: null };
    }
  }
  return state;
}

export interface PlantModel extends PlantSnapshot {
  readonly accumulatorS: number;
}

const sampleOf = (state: SimState): HistorySample => ({
  t: state.timeS,
  heading: state.headingDeg,
  target: state.targetDeg,
});

export function createPlant(initialHeadingDeg = 0): PlantModel {
  const state = createInitialState(initialHeadingDeg);
  return { state, history: [sampleOf(state)], accumulatorS: 0 };
}

export function applyToPlant(plant: PlantModel, next: SimState): PlantModel {
  return next === plant.state ? plant : { ...plant, state: next, accumulatorS: 0 };
}

const sampleIndex = (t: number) => Math.floor(t * HISTORY_SAMPLE_HZ + 1e-6);

export function advancePlant(plant: PlantModel, elapsedS: number): PlantModel {
  if (!isSequenceRunning(plant.state.phase)) return plant;

  let accumulator = plant.accumulatorS + clampElapsed(elapsedS);
  let state = plant.state;
  let added: HistorySample[] | null = null;

  while (accumulator >= SIM_STEP_S - 1e-12 && isSequenceRunning(state.phase)) {
    const previous = state;
    state = step(state, SIM_STEP_S);
    accumulator -= SIM_STEP_S;
    if (sampleIndex(state.timeS) > sampleIndex(previous.timeS)) {
      (added ??= []).push(sampleOf(state));
    }
  }
  if (!isSequenceRunning(state.phase)) accumulator = 0;

  let history = plant.history;
  if (added) {
    const oldest = state.timeS - HISTORY_WINDOW_S - 1 / HISTORY_SAMPLE_HZ;
    history = [...history, ...added].filter((sample) => sample.t >= oldest);
  }
  return { state, history, accumulatorS: accumulator };
}

function clampElapsed(elapsedS: number): number {
  if (!Number.isFinite(elapsedS) || elapsedS <= 0) return 0;
  return Math.min(elapsedS, MAX_FRAME_ELAPSED_S);
}
