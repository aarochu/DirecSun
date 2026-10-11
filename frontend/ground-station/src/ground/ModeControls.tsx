import { useState } from 'react';
import type { CommandableMode, DeviceOperator, ManualInput, OperatorState } from '../hardware/deviceOperator';

// Every mode the box defines (protocol "mode", FD section 13), with the
// command that enters it. Buttons follow the transition table: SAFE to SPINUP
// by arm, READY to the demonstration modes and back, READY to SPINUP by
// respin, anything armed to SAFE by disarm. The box has the final say; a
// refusal shows its reason below the controls.

const DEMO_MODES = ['DETUMBLE', 'HOLD', 'SLEW', 'SUN_POINT', 'MANUAL'] as const;

interface ModeRow {
  mode: string;
  what: string;
}

const MODES: readonly ModeRow[] = [
  { mode: 'SAFE', what: 'Wheel stopped. Reached by disarm or e-stop.' },
  { mode: 'SPINUP', what: 'Wheel spinning up to its start speed. Lock pin in.' },
  { mode: 'READY', what: 'Box rate held at zero. Entry point for every mode below.' },
  { mode: 'DETUMBLE', what: 'Brings the box to a stop.' },
  { mode: 'HOLD', what: 'Holds the current heading against pokes.' },
  { mode: 'SLEW', what: 'Turns to a heading, then returns READY.' },
  { mode: 'SUN_POINT', what: 'Points the +X face at the lamp.' },
  { mode: 'MANUAL', what: 'You drive the wheel directly.' },
];

const label = (mode: string) => mode.replace('_', ' ');

interface Props {
  operator: DeviceOperator;
  state: OperatorState;
  mode: string | null;
  onArm: () => void;
}

export function ModeControls({ operator, state, mode, onArm }: Props) {
  const [slewTarget, setSlewTarget] = useState('90');
  const [manualInput, setManualInput] = useState<ManualInput>('throttle');
  const [respinRpm, setRespinRpm] = useState('');

  const idle = state.busy === null;
  const inControl = state.controlling && idle;
  const inDemo = mode !== null && (DEMO_MODES as readonly string[]).includes(mode);
  const target = Number(slewTarget);
  const targetOk = slewTarget.trim() !== '' && Number.isFinite(target) && target >= -180 && target <= 180;
  const rpm = respinRpm.trim() === '' ? undefined : Number(respinRpm);
  const rpmOk = rpm === undefined || (Number.isFinite(rpm) && rpm > 0);

  const enter = (to: CommandableMode, options?: { targetDeg?: number; input?: ManualInput }) =>
    void operator.setMode(to, options);

  const action = (row: ModeRow) => {
    switch (row.mode) {
      case 'SAFE':
        return (
          <button type="button" className="button button--quiet modes__go" disabled={!inControl || mode === null || mode === 'SAFE'} onClick={() => void operator.disarm()}>
            Disarm
          </button>
        );
      case 'SPINUP':
        return mode === 'READY' ? (
          <span className="modes__with">
            <input
              className="modes__input"
              inputMode="numeric"
              placeholder="bias"
              aria-label="Respin wheel speed in RPM, blank for the bias speed"
              value={respinRpm}
              onChange={(event) => setRespinRpm(event.target.value)}
            />
            <button type="button" className="button button--quiet modes__go" disabled={!inControl || !rpmOk} onClick={() => void operator.respin(rpm)}>
              Respin
            </button>
          </span>
        ) : (
          <button type="button" className="button button--quiet modes__go" disabled={mode !== 'SAFE' || !idle} onClick={onArm}>
            Arm…
          </button>
        );
      case 'READY':
        return (
          <button type="button" className="button button--quiet modes__go" disabled={!inControl || !inDemo} onClick={() => enter('READY')}>
            Enter
          </button>
        );
      case 'SLEW':
        return (
          <span className="modes__with">
            <input
              className="modes__input"
              inputMode="decimal"
              aria-label="SLEW target heading in degrees, clockwise positive"
              value={slewTarget}
              onChange={(event) => setSlewTarget(event.target.value)}
            />
            <span className="modes__unit">°</span>
            <button
              type="button"
              className="button button--quiet modes__go"
              disabled={!inControl || mode !== 'READY' || !targetOk}
              onClick={() => enter('SLEW', { targetDeg: target })}
            >
              Enter
            </button>
          </span>
        );
      case 'MANUAL':
        return (
          <span className="modes__with">
            <select
              className="modes__input"
              aria-label="MANUAL input"
              value={manualInput}
              onChange={(event) => setManualInput(event.target.value as ManualInput)}
            >
              <option value="throttle">throttle</option>
              <option value="wheel">wheel</option>
              <option value="rate">rate</option>
            </select>
            <button
              type="button"
              className="button button--quiet modes__go"
              disabled={!inControl || mode !== 'READY'}
              onClick={() => enter('MANUAL', { input: manualInput })}
            >
              Enter
            </button>
          </span>
        );
      default:
        return (
          <button
            type="button"
            className="button button--quiet modes__go"
            disabled={!inControl || mode !== 'READY'}
            onClick={() => enter(row.mode as CommandableMode)}
          >
            Enter
          </button>
        );
    }
  };

  return (
    <div className="modes" role="group" aria-labelledby="modes-title">
      <h3 className="modes__title mono-label" id="modes-title">
        BOX MODE
      </h3>
      <ul className="modes__list">
        {MODES.map((row) => {
          const current = row.mode === mode;
          return (
            <li key={row.mode} className={`modes__row${current ? ' is-current' : ''}`} aria-current={current ? 'true' : undefined}>
              <div className="modes__text">
                <span className="modes__name mono-label">{label(row.mode)}</span>
                <span className="modes__what">{row.what}</span>
              </div>
              {action(row)}
            </li>
          );
        })}
      </ul>
    </div>
  );
}
