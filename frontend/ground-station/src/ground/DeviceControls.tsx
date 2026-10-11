import { ArrowLeft, ArrowRight, OctagonX, Power, PowerOff, Square } from 'lucide-react';
import { useEffect, useId, useRef, useState, useSyncExternalStore } from 'react';
import type { DeviceLink } from '../hardware/deviceLink';
import type { DeviceOperator, TurnPhase } from '../hardware/deviceOperator';
import { ARMED_MODES, MODES, modeSpec } from '../hardware/modes';
import { ModeMenu } from './ModeMenu';

const SEQUENCE_STEPS: readonly { label: string; phases: readonly TurnPhase[] }[] = [
  { label: 'Turn 90°', phases: ['rotating'] },
  { label: 'Hold 1.0 s', phases: ['holding'] },
  { label: 'Return to start', phases: ['returning'] },
];

const PHASE_TEXT: Record<TurnPhase, string> = {
  idle: '',
  rotating: 'TURNING',
  holding: 'HOLDING TARGET',
  returning: 'RETURNING',
};

const MANUAL_LABEL = { rate: 'BOX RATE SETPOINT', throttle: 'THROTTLE SETPOINT', wheel: 'WHEEL SPEED SETPOINT' } as const;
const MANUAL_UNIT = { rate: ' °/s', throttle: '', wheel: ' RPM' } as const;

function guidance(mode: string | null, controlling: boolean): string {
  if (mode === null) return 'WAITING FOR TELEMETRY FROM THE BOX';
  if (mode === 'SAFE') return MODES.SAFE.guidance;
  if (!controlling) return 'ANOTHER CLIENT IS CONTROLLING THE BOX. E-STOP STILL WORKS FROM HERE';
  return modeSpec(mode)?.guidance ?? `${mode}: SPACE BAR SENDS E-STOP`;
}

function isTypingTarget(target: EventTarget | null): boolean {
  return target instanceof HTMLInputElement && target.type !== 'checkbox' && target.type !== 'radio' && target.type !== 'range';
}

export function DeviceControls({ link, operator }: { link: DeviceLink; operator: DeviceOperator }) {
  const state = useSyncExternalStore(operator.subscribe, operator.getState);
  const telemetry = useSyncExternalStore(link.subscribeTelemetry, link.getTelemetry);
  const mode = telemetry.statusWord === '—' ? null : telemetry.statusWord;
  const armed = mode !== null && mode !== 'SAFE';
  const [confirming, setConfirming] = useState(false);
  const [pinIn, setPinIn] = useState(false);
  const confirmId = useId();
  const manualId = useId();
  const manual = state.manual;
  const armedRef = useRef(armed);
  armedRef.current = armed;

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.code !== 'Space' || event.repeat || !armedRef.current || isTypingTarget(event.target)) return;
      event.preventDefault();
      operator.estop();
    };
    const onKeyUp = (event: KeyboardEvent) => {
      if (event.code !== 'Space' || !armedRef.current || isTypingTarget(event.target)) return;
      event.preventDefault();
    };
    window.addEventListener('keydown', onKey, { capture: true });
    window.addEventListener('keyup', onKeyUp, { capture: true });
    return () => {
      window.removeEventListener('keydown', onKey, { capture: true });
      window.removeEventListener('keyup', onKeyUp, { capture: true });
    };
  }, [operator]);

  useEffect(() => {
    if (mode !== 'SAFE') setConfirming(false);
  }, [mode]);

  const turning = state.phase !== 'idle';
  const canRotate = state.controlling && !turning && mode === 'READY';
  const canStopTurn = turning || (state.controlling && mode === 'SLEW');
  const activeStep = SEQUENCE_STEPS.findIndex((step) => step.phases.includes(state.phase));
  const status = `${mode ?? '—'} / DEVICE${turning ? ` · ${PHASE_TEXT[state.phase]}` : ''}`;

  return (
    <section className="controls controls--device" aria-labelledby="controls-title">
      <div className="controls__head">
        <h2 className="controls__title mono-label" id="controls-title">
          COMMAND ATTITUDE
        </h2>
        <p className="controls__status">{status}</p>
      </div>

      <div className="controls__row">
        <div className="controls__directions">
          <button
            type="button"
            className="button button--primary controls__direction"
            aria-label="Rotate left ninety degrees and return"
            disabled={!canRotate}
            onClick={() => void operator.rotate(-1)}
          >
            <ArrowLeft strokeWidth={1.75} aria-hidden />
            Rotate left 90°
          </button>
          <button
            type="button"
            className="button button--primary controls__direction"
            aria-label="Rotate right ninety degrees and return"
            disabled={!canRotate}
            onClick={() => void operator.rotate(1)}
          >
            Rotate right 90°
            <ArrowRight strokeWidth={1.75} aria-hidden />
          </button>
          <ModeMenu
            mode={mode}
            disabled={!state.controlling || turning || state.busy !== null || !(ARMED_MODES as readonly string[]).includes(mode ?? '')}
            onSelect={(next, input) => void operator.setMode(next, input)}
          />
        </div>
        <div className="controls__secondary">
          <button
            type="button"
            className="button button--estop"
            aria-label="Emergency stop the box"
            aria-keyshortcuts="Space"
            disabled={!link.isOpen}
            onClick={() => operator.estop()}
          >
            <OctagonX strokeWidth={1.75} aria-hidden />
            E-stop
          </button>
          <button
            type="button"
            className="button button--quiet"
            aria-label="Stop the turn"
            disabled={!canStopTurn}
            onClick={() => void operator.stopTurn()}
          >
            <Square strokeWidth={1.75} aria-hidden />
            Stop turn
          </button>
          {armed ? (
            <button
              type="button"
              className="button button--quiet"
              aria-label="Disarm the box"
              disabled={!state.controlling || state.busy !== null}
              onClick={() => void operator.disarm()}
            >
              <PowerOff strokeWidth={1.75} aria-hidden />
              Disarm
            </button>
          ) : (
            <button
              type="button"
              className="button button--quiet"
              aria-label="Arm the box"
              aria-expanded={confirming}
              aria-controls={confirmId}
              disabled={mode !== 'SAFE' || state.busy !== null}
              onClick={() => {
                setPinIn(false);
                setConfirming((value) => !value);
              }}
            >
              <Power strokeWidth={1.75} aria-hidden />
              Arm…
            </button>
          )}
        </div>
      </div>

      <div className="controls__confirm" id={confirmId} role="group" aria-label="Confirm arming" hidden={!confirming}>
        <label className="controls__check">
          <input type="checkbox" checked={pinIn} onChange={(event) => setPinIn(event.target.checked)} />
          The lock pin is in, or someone is holding the box still. Arming spins the wheel up to its bias speed.
        </label>
        <div className="controls__confirm-actions">
          <button
            type="button"
            className="button button--primary"
            disabled={!pinIn || state.busy !== null}
            onClick={() => {
              setConfirming(false);
              void operator.arm();
            }}
          >
            Arm now
          </button>
          <button type="button" className="button button--quiet" onClick={() => setConfirming(false)}>
            Cancel
          </button>
        </div>
      </div>

      {manual ? (
        <div className="controls__manual" role="group" aria-label="Manual setpoint">
          <label className="controls__manual-label mono-label" htmlFor={manualId}>
            {MANUAL_LABEL[manual.input]}
          </label>
          <input
            id={manualId}
            type="range"
            min={manual.min}
            max={manual.max}
            step={manual.input === 'rate' ? 0.5 : 1}
            value={manual.value}
            aria-label={`Manual ${manual.input} setpoint`}
            onChange={(event) => operator.setManual(Number(event.target.value))}
          />
          <output className="controls__manual-value" htmlFor={manualId}>
            {manual.value}
            {MANUAL_UNIT[manual.input]}
          </output>
          {manual.input === 'rate' ? (
            <button type="button" className="button button--quiet" onClick={() => operator.setManual(0)}>
              Zero
            </button>
          ) : null}
        </div>
      ) : null}

      <ol className="controls__sequence mono-label" aria-label="Command sequence">
        {SEQUENCE_STEPS.map((step, index) => (
          <li
            key={step.label}
            className={index === activeStep ? 'is-active' : activeStep > index ? 'is-done' : undefined}
            aria-current={index === activeStep ? 'step' : undefined}
          >
            {step.label}
          </li>
        ))}
      </ol>

      <p className="controls__note mono-label">{guidance(mode, state.controlling)}</p>
      <p
        className={`controls__result mono-label${state.result?.tone === 'error' ? ' controls__result--error' : ''}`}
        role="status"
        aria-live="polite"
      >
        {state.result?.text ?? ''}
      </p>
    </section>
  );
}
