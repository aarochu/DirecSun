import { useEffect, useState, useSyncExternalStore } from 'react';
import {
  advancePlant,
  applyToPlant,
  commandRotate,
  createPlant,
  isSequenceRunning,
  stopSequence,
  type PlantModel,
} from './stateMachine';
import type { Direction, PlantSnapshot } from './types';

export interface FrameClock {
  now(): number;
  requestFrame(callback: (timestampMs: number) => void): number;
  cancelFrame(id: number): void;
}

export const TELEMETRY_INTERVAL_MS = 50;

const browserClock: FrameClock = {
  now: () => performance.now(),
  requestFrame: (callback) => requestAnimationFrame(callback),
  cancelFrame: (id) => cancelAnimationFrame(id),
};

export class MockPlant {
  private model: PlantModel;
  private telemetry: PlantModel;
  private readonly listeners = new Set<() => void>();
  private readonly telemetryListeners = new Set<() => void>();
  private lastTelemetryMs = -Infinity;
  private frameId: number | null = null;
  private lastFrameMs: number | null = null;
  private attached = false;

  constructor(
    private readonly clock: FrameClock = browserClock,
    private readonly initialHeadingDeg = 0,
  ) {
    this.model = createPlant(initialHeadingDeg);
    this.telemetry = this.model;
  }

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getSnapshot = (): PlantSnapshot => this.model;

  subscribeTelemetry = (listener: () => void): (() => void) => {
    this.telemetryListeners.add(listener);
    return () => this.telemetryListeners.delete(listener);
  };

  getTelemetry = (): PlantSnapshot => this.telemetry;

  rotate(direction: Direction): boolean {
    const next = applyToPlant(this.model, commandRotate(this.model.state, direction));
    if (next === this.model) return false;
    this.commit(next);
    this.ensureRunning();
    return true;
  }

  stop(): boolean {
    const next = applyToPlant(this.model, stopSequence(this.model.state));
    this.halt();
    if (next === this.model) return false;
    this.commit(next);
    return true;
  }

  reset(): void {
    this.halt();
    this.commit(createPlant(this.initialHeadingDeg));
  }

  attach(): void {
    if (this.attached) return;
    this.attached = true;
    if (typeof document !== 'undefined') {
      document.addEventListener('visibilitychange', this.handleVisibility);
    }
    this.ensureRunning();
  }

  detach(): void {
    this.attached = false;
    this.halt();
    if (typeof document !== 'undefined') {
      document.removeEventListener('visibilitychange', this.handleVisibility);
    }
  }

  get hasPendingFrame(): boolean {
    return this.frameId !== null;
  }

  private commit(next: PlantModel, timestampMs = this.clock.now()) {
    const phaseChanged = next.state.phase !== this.model.state.phase;
    this.model = next;
    for (const listener of this.listeners) listener();

    const due = timestampMs - this.lastTelemetryMs >= TELEMETRY_INTERVAL_MS;
    if (phaseChanged || due || !isSequenceRunning(next.state.phase)) {
      this.lastTelemetryMs = timestampMs;
      this.telemetry = next;
      for (const listener of this.telemetryListeners) listener();
    }
  }

  private ensureRunning() {
    if (!this.attached || this.frameId !== null) return;
    if (!isSequenceRunning(this.model.state.phase)) return;
    if (typeof document !== 'undefined' && document.visibilityState === 'hidden') return;
    this.lastFrameMs = this.clock.now();
    this.frameId = this.clock.requestFrame(this.tick);
  }

  private halt() {
    if (this.frameId !== null) this.clock.cancelFrame(this.frameId);
    this.frameId = null;
    this.lastFrameMs = null;
  }

  private tick = (timestampMs: number) => {
    this.frameId = null;
    if (!this.attached) return;
    const elapsedS = this.lastFrameMs === null ? 0 : (timestampMs - this.lastFrameMs) / 1000;
    this.lastFrameMs = timestampMs;
    const next = advancePlant(this.model, elapsedS);
    if (next !== this.model) this.commit(next, timestampMs);
    if (isSequenceRunning(this.model.state.phase)) {
      this.frameId = this.clock.requestFrame(this.tick);
    } else {
      this.lastFrameMs = null;
    }
  };

  private handleVisibility = () => {
    if (document.visibilityState === 'hidden') this.halt();
    else this.ensureRunning();
  };
}

export function useMockPlant(clock?: FrameClock): MockPlant {
  const [plant] = useState(() => new MockPlant(clock));
  useEffect(() => {
    plant.attach();
    return () => plant.detach();
  }, [plant]);
  return plant;
}

export function usePlantFrame(plant: MockPlant): PlantSnapshot {
  return useSyncExternalStore(plant.subscribe, plant.getSnapshot);
}

export function usePlantTelemetry(plant: MockPlant): PlantSnapshot {
  return useSyncExternalStore(plant.subscribeTelemetry, plant.getTelemetry);
}
