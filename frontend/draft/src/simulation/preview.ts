import { SIM_STEP_S } from './constants';
import {
  advancePlant,
  applyToPlant,
  commandRotate,
  createPlant,
  type PlantModel,
} from './stateMachine';
import type { PlantSnapshot, SimState } from './types';

function runUntil(plant: PlantModel, done: (state: SimState) => boolean, limitS = 30): PlantModel {
  let next = plant;
  for (let t = 0; t < limitS && !done(next.state); t += SIM_STEP_S) {
    next = advancePlant(next, SIM_STEP_S);
  }
  return next;
}

export function createPreviewSnapshot(): PlantSnapshot {
  let plant = createPlant();
  plant = applyToPlant(plant, commandRotate(plant.state, -1));
  plant = runUntil(plant, (state) => state.phase === 'READY');
  plant = applyToPlant(plant, commandRotate(plant.state, 1));
  plant = runUntil(plant, (state) => state.phase === 'SETTLING_OUT');
  const holdStart = plant.state.timeS;
  plant = runUntil(plant, (state) => state.timeS - holdStart >= 0.4);
  return { state: plant.state, history: plant.history };
}
