import { ArrowLeft, ArrowRight, OctagonX, Power, PowerOff, Square } from 'lucide-react';
import { useEffect, useId, useRef, useState, useSyncExternalStore } from 'react';
import type { DeviceLink } from '../hardware/deviceLink';
import type { DeviceOperator, TurnPhase } from '../hardware/deviceOperator';
import { ARMED_MODES, armBlock, canEnter, choiceBlock, MANUAL_NUDGE, MODE_CHOICES, MODES, modeSpec, NEAR_LIMIT_GUIDANCE, RESPIN_FIRMWARE, RESPIN_RANGE_RPM, respinBlock } from '../hardware/modes';
import { ModeMenu, type MenuEntry } from './ModeMenu';

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

export function DeviceControls({ link, operator }: { link: DeviceLink; operator: DeviceOperator }) {
  const state = useSyncExternalStore(operator.subscribe, operator.getState);
  const reading = useSyncExternalStore(link.subscribeFrame, () => link.latestReading);
  const telemetry = useSyncExternalStore(link.subscribeTelemetry, link.getTelemetry);
  const mode = reading?.mode ?? null;
  const armed = mode !== null && mode !== 'SAFE';
  const [confirming, setConfirming] = useState(false);
  const [pinIn, setPinIn] = useState(false);
  const confirmId = useId();
  const manualId = useId();
  const slewId = useId();
  const [slewOpen, setSlewOpen] = useState(false);
  const [slewTarget, setSlewTarget] = useState('');
  const [respinOpen, setRespinOpen] = useState(false);
  const [respinRpm, setRespinRpm] = useState('');
  const [respinPinned, setRespinPinned] = useState(false);
  const respinId = useId();
  const arming = armBlock(telemetry);
  const respinBlocked = respinBlock(mode, telemetry);
  const menuEntries: MenuEntry[] = [
    ...MODE_CHOICES.map((choice) => {
      const block = choiceBlock(choice, telemetry);
      return {
        label: choice.label,
        disabled: !canEnter(choice.mode, mode) || block !== null,
        current: choice.mode === mode && choice.input === undefined && !choice.needsTarget,
        tag: block ?? (MODES[choice.mode].firmware === 'pending' ? 'Firmware pending' : undefined),
        onPick: () => {
          if (choice.needsTarget) {
            setRespinOpen(false);
            setSlewTarget('');
            setSlewOpen(true);
          } else {
            void operator.setMode(choice.mode, choice.input);
          }
        },
      };
    }),
    {
      label: 'Respin wheel…',
      disabled: respinBlocked !== null,
      tag: respinBlocked ?? (RESPIN_FIRMWARE === 'pending' ? 'Firmware pending' : undefined),
      separated: true,
      onPick: () => {
        setSlewOpen(false);
        setRespinRpm('');
        setRespinPinned(false);
        setRespinOpen(true);
      },
    },
  ];
  const manual = state.manual;
  const armedRef = useRef(armed);
  armedRef.current = armed;

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.code !== 'Space' || event.repeat || !armedRef.current) return;
      event.preventDefault();
      operator.estop();
    };
    const onArrow = (event: KeyboardEvent) => {
      if (event.defaultPrevented || !operator.getState().manual) return;
      const target = event.target;
      if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement) return;
      // Arrow keys inside the open mode menu navigate it; they must not move the setpoint.
      if (target instanceof Element && target.closest('[role="menu"]')) return;
      const step = event.key === 'ArrowUp' || event.key === 'ArrowRight' ? 1 : event.key === 'ArrowDown' || event.key === 'ArrowLeft' ? -1 : 0;
      if (step === 0) return;
      event.preventDefault();
      operator.nudgeManual(step);
    };
    const onKeyUp = (event: KeyboardEvent) => {
      if (event.code !== 'Space' || !armedRef.current) return;
      event.preventDefault();
    };
    window.addEventListener('keydown', onKey, { capture: true });
    window.addEventListener('keyup', onKeyUp, { capture: true });
    window.addEventListener('keydown', onArrow);
    return () => {
      window.removeEventListener('keydown', onKey, { capture: true });
      window.removeEventListener('keyup', onKeyUp, { capture: true });
      window.removeEventListener('keydown', onArrow);
    };
  }, [operator]);

  useEffect(() => {
    if (mode !== 'SAFE') setConfirming(false);
    if (mode !== 'READY') {
      setSlewOpen(false);
      setRespinOpen(false);
    }
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
          <ModeMenu entries={menuEntries} disabled={!state.controlling || turning || state.busy !== null || !(ARMED_MODES as readonly string[]).includes(mode ?? '')} />
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
        {arming ? <p className="controls__result controls__result--error mono-label">{arming}</p> : null}
        <div className="controls__confirm-actions">
          <button
            type="button"
            className="button button--primary"
            disabled={!pinIn || state.busy !== null || arming !== null}
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

      {slewOpen ? (
        <form
          className="controls__slew"
          aria-label="Slew to a heading"
          onSubmit={(event) => {
            event.preventDefault();
            setSlewOpen(false);
            void operator.slewToHeading(Number(slewTarget));
          }}
        >
          <label className="controls__manual-label mono-label" htmlFor={slewId}>
            TARGET HEADING, DEG CLOCKWISE FROM THE LAST ZERO
          </label>
          <input
            id={slewId}
            className="controls__slew-input"
            type="number"
            min={-180}
            max={180}
            step={0.5}
            required
            autoFocus
            value={slewTarget}
            onChange={(event) => setSlewTarget(event.target.value)}
          />
          <button type="submit" className="button button--primary" disabled={slewTarget.trim() === '' || mode !== 'READY'}>
            Slew
          </button>
          <button type="button" className="button button--quiet" onClick={() => setSlewOpen(false)}>
            Cancel
          </button>
        </form>
      ) : null}

      {respinOpen ? (
        <form
          className="controls__slew"
          aria-label="Respin the wheel"
          onSubmit={(event) => {
            event.preventDefault();
            setRespinOpen(false);
            void operator.respin(respinRpm.trim() === '' ? null : Number(respinRpm));
          }}
        >
          <label className="controls__manual-label mono-label" htmlFor={respinId}>
            WHEEL START SPEED, RPM ({RESPIN_RANGE_RPM[0]} TO {RESPIN_RANGE_RPM[1]}; BLANK FOR THE BIAS SPEED)
          </label>
          <input
            id={respinId}
            className="controls__slew-input"
            type="number"
            min={RESPIN_RANGE_RPM[0]}
            max={RESPIN_RANGE_RPM[1]}
            step={50}
            value={respinRpm}
            onChange={(event) => setRespinRpm(event.target.value)}
          />
          <label className="controls__check">
            <input type="checkbox" checked={respinPinned} onChange={(event) => setRespinPinned(event.target.checked)} />
            The lock pin is in. The wheel changes speed and the reaction goes into the bench.
          </label>
          <button type="submit" className="button button--primary" disabled={!respinPinned || respinBlocked !== null}>
            Respin
          </button>
          <button type="button" className="button button--quiet" onClick={() => setRespinOpen(false)}>
            Cancel
          </button>
        </form>
      ) : null}

      {manual ? (
        <div className="controls__manual" role="group" aria-label="Manual setpoint">
          <label className="controls__manual-label mono-label" htmlFor={manualId}>
            {MANUAL_LABEL[manual.input]} · ARROW KEYS NUDGE
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
          <div className="controls__manual-nudges">
            <button type="button" className="button button--quiet" aria-label={`Lower the ${manual.input} setpoint`} onClick={() => operator.nudgeManual(-1)}>
              −{MANUAL_NUDGE[manual.input]}
            </button>
            <button type="button" className="button button--quiet" aria-label={`Raise the ${manual.input} setpoint`} onClick={() => operator.nudgeManual(1)}>
              +{MANUAL_NUDGE[manual.input]}
            </button>
            {manual.input === 'rate' ? (
              <button type="button" className="button button--quiet" onClick={() => operator.setManual(0)}>
                Zero
              </button>
            ) : null}
          </div>
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

      <p className="controls__note mono-label">
        {telemetry.flags.includes('MOMENTUM_NEAR_LIMIT') && state.controlling ? NEAR_LIMIT_GUIDANCE : guidance(mode, state.controlling)}
      </p>
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
