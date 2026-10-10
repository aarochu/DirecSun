import type { FrameClock } from '../simulation/useMockPlant';

export class ManualClock implements FrameClock {
  nowMs = 1000;
  private nextId = 1;
  private readonly callbacks = new Map<number, (timestampMs: number) => void>();

  now() {
    return this.nowMs;
  }

  requestFrame(callback: (timestampMs: number) => void) {
    const id = this.nextId++;
    this.callbacks.set(id, callback);
    return id;
  }

  cancelFrame(id: number) {
    this.callbacks.delete(id);
  }

  get pendingFrames() {
    return this.callbacks.size;
  }

  frame(ms = 1000 / 60) {
    this.nowMs += ms;
    const waiting = [...this.callbacks.values()];
    this.callbacks.clear();
    for (const callback of waiting) callback(this.nowMs);
  }

  run(seconds: number, each?: () => void, frameMs = 1000 / 60) {
    const frames = Math.round((seconds * 1000) / frameMs);
    for (let i = 0; i < frames; i++) {
      this.frame(frameMs);
      each?.();
    }
  }
}
