import { PHASE_STATUS } from '../simulation/stateMachine';
import type { HistorySample, PlantSnapshot } from '../simulation/types';
import type { MockPlant } from '../simulation/useMockPlant';

export interface DisplayFrame {
  readonly headingDeg: number;
  readonly targetDeg: number;
  readonly originDeg: number | null;
}

export interface DisplayTelemetry {
  readonly source: 'simulation' | 'hardware';
  readonly statusLabel: string;
  readonly statusWord: string;
  readonly statusSuffix: string;
  readonly headingDeg: number;
  readonly targetDeg: number;
  readonly rateDegS: number | null;
  readonly wheelRpm: number | null;
  readonly wheelNote: string;
  readonly wheelRefRpm: number | null;
  readonly throttleBand: readonly [number, number] | null;
  readonly flags: readonly string[];
  readonly batteryV: number | null;
  readonly history: readonly HistorySample[];
  readonly nowS: number;
}

export interface DisplaySource {
  subscribeFrame(listener: () => void): () => void;
  getFrame(): DisplayFrame;
  subscribeTelemetry(listener: () => void): () => void;
  getTelemetry(): DisplayTelemetry;
}

export function simulationTelemetry({ state, history }: PlantSnapshot): DisplayTelemetry {
  const [statusWord = '', ...rest] = PHASE_STATUS[state.phase].split(' / ');
  return {
    source: 'simulation',
    statusLabel: 'SIMULATION PHASE',
    statusWord,
    statusSuffix: rest.join(' / '),
    headingDeg: state.headingDeg,
    targetDeg: state.targetDeg,
    rateDegS: state.rateDegS,
    wheelRpm: state.wheelRpm,
    wheelNote: state.wheelLimited ? 'SIMULATED LIMIT' : 'MOMENTUM MODEL',
    wheelRefRpm: null,
    throttleBand: null,
    flags: [],
    batteryV: null,
    history,
    nowS: state.timeS,
  };
}

export function simulationSource(plant: MockPlant): DisplaySource {
  let frameFor: PlantSnapshot | null = null;
  let frame: DisplayFrame | null = null;
  let telemetryFor: PlantSnapshot | null = null;
  let telemetry: DisplayTelemetry | null = null;
  return {
    subscribeFrame: plant.subscribe,
    getFrame() {
      const snapshot = plant.getSnapshot();
      if (snapshot !== frameFor || !frame) {
        frameFor = snapshot;
        const { headingDeg, targetDeg, originDeg } = snapshot.state;
        frame = { headingDeg, targetDeg, originDeg };
      }
      return frame;
    },
    subscribeTelemetry: plant.subscribeTelemetry,
    getTelemetry() {
      const snapshot = plant.getTelemetry();
      if (snapshot !== telemetryFor || !telemetry) {
        telemetryFor = snapshot;
        telemetry = simulationTelemetry(snapshot);
      }
      return telemetry;
    },
  };
}
