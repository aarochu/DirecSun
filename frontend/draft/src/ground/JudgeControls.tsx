import { ArrowLeft, ArrowRight, RotateCcw, Square } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import { controlAvailability, PHASE_STATUS } from '../simulation/stateMachine';
import type { Direction, SimPhase } from '../simulation/types';
import { usePlantTelemetry, type MockPlant } from '../simulation/useMockPlant';

const SEQUENCE_STEPS = [
  { label: 'Turn 90°', phases: ['ROTATING_OUT'] },
  { label: 'Hold 1.0 s', phases: ['SETTLING_OUT'] },
  { label: 'Return to start', phases: ['RETURNING', 'SETTLING_HOME'] },
] as const satisfies readonly { label: string; phases: readonly SimPhase[] }[];

const ANNOUNCEMENT: Record<SimPhase, (direction: Direction | null) => string> = {
  READY: () => 'Simulation ready. Heading back at the captured start.',
  ROTATING_OUT: (d) => `Simulation rotating ${d === -1 ? 'left' : 'right'} 90 degrees.`,
  SETTLING_OUT: () => 'Holding the target heading.',
  RETURNING: () => 'Returning to the starting heading.',
  SETTLING_HOME: () => 'Settling at the starting heading.',
  STOPPED: () => 'Simulation stopped. Reset to continue.',
};

interface JudgeControlsProps {
  plant: MockPlant;
  deviceLive?: boolean;
}

export function JudgeControls({ plant, deviceLive = false }: JudgeControlsProps) {
  const { state } = usePlantTelemetry(plant);
  const { phase, lastCommand } = state;
  const available = controlAvailability(phase);
  const can = deviceLive ? { rotate: false, stop: false, reset: false } : available;
  const [announcement, setAnnouncement] = useState('');
  const previousPhase = useRef<SimPhase>(phase);
  const resetPending = useRef(false);

  useEffect(() => {
    if (previousPhase.current === phase) return;
    previousPhase.current = phase;
    if (resetPending.current) {
      resetPending.current = false;
      return;
    }
    setAnnouncement(ANNOUNCEMENT[phase](lastCommand));
  }, [phase, lastCommand]);

  const activeStep = SEQUENCE_STEPS.findIndex((step) => (step.phases as readonly SimPhase[]).includes(phase));

  return (
    <section className="controls" aria-labelledby="controls-title">
      <div className="controls__head">
        <h2 className="controls__title mono-label" id="controls-title">
          COMMAND ATTITUDE
        </h2>
        <p className="controls__status">{deviceLive ? 'SIMULATION PAUSED' : PHASE_STATUS[phase]}</p>
      </div>

      <div className="controls__row">
        <div className="controls__directions">
          <button
            type="button"
            className="button button--primary controls__direction"
            aria-label="Rotate left ninety degrees and return"
            disabled={!can.rotate}
            onClick={() => plant.rotate(-1)}
          >
            <ArrowLeft strokeWidth={1.75} aria-hidden />
            Rotate left 90°
          </button>
          <button
            type="button"
            className="button button--primary controls__direction"
            aria-label="Rotate right ninety degrees and return"
            disabled={!can.rotate}
            onClick={() => plant.rotate(1)}
          >
            Rotate right 90°
            <ArrowRight strokeWidth={1.75} aria-hidden />
          </button>
        </div>
        <div className="controls__secondary">
          <button
            type="button"
            className="button button--danger"
            aria-label="Stop the simulation"
            disabled={!can.stop}
            onClick={() => plant.stop()}
          >
            <Square strokeWidth={1.75} aria-hidden />
            Stop simulation
          </button>
          <button
            type="button"
            className="button button--quiet"
            aria-label="Reset the simulation"
            disabled={!can.reset}
            onClick={() => {
              resetPending.current = phase !== 'READY';
              plant.reset();
              setAnnouncement('Simulation reset to 0 degrees.');
            }}
          >
            <RotateCcw strokeWidth={1.75} aria-hidden />
            Reset simulation
          </button>
        </div>
      </div>

      <ol className="controls__sequence mono-label" aria-label="Command sequence">
        {SEQUENCE_STEPS.map((step, index) => (
          <li
            key={step.label}
            className={
              index === activeStep ? 'is-active' : activeStep > index ? 'is-done' : undefined
            }
            aria-current={index === activeStep ? 'step' : undefined}
          >
            {step.label}
          </li>
        ))}
      </ol>

      {deviceLive && (
        <p className="controls__note mono-label">
          SHOWING DEVICE TELEMETRY / THESE SIMULATION CONTROLS ARE LOCKED AND NOTHING IS SENT TO THE DEVICE
        </p>
      )}

      <p className="sr-only" aria-live="polite">
        {announcement}
      </p>
    </section>
  );
}
