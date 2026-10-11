export type SimPhase =
  | 'READY'
  | 'ROTATING_OUT'
  | 'SETTLING_OUT'
  | 'RETURNING'
  | 'SETTLING_HOME'
  | 'STOPPED';

export type Direction = -1 | 1;

export type TelemetrySource = 'simulation';

export interface SimState {
  readonly source: TelemetrySource;
  readonly phase: SimPhase;
  readonly timeS: number;
  readonly headingDeg: number;
  readonly targetDeg: number;
  readonly rateDegS: number;
  readonly originDeg: number | null;
  readonly holdRemainingS: number;
  readonly wheelRpm: number;
  readonly wheelLimited: boolean;
  readonly lastCommand: Direction | null;
}

export interface HistorySample {
  readonly t: number;
  readonly heading: number;
  readonly target: number;
  readonly rate?: number | null;
  readonly wheel?: number | null;
  readonly wheelRef?: number | null;
  readonly vbat?: number | null;
}

export interface PlantSnapshot {
  readonly state: SimState;
  readonly history: readonly HistorySample[];
}
