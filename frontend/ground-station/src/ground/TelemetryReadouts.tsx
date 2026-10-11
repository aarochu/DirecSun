import { flagSeverity } from '../hardware/deviceLink';
import { formatHeading, formatRate, formatRpm } from '../lib/format';
import type { DisplayTelemetry } from './displaySource';

const flagLabel = (flag: string) => flag.replace(/_/g, ' ');

export function TelemetryReadouts({ telemetry }: { telemetry: DisplayTelemetry }) {
  return (
    <dl className="readouts">
      <div className="readout readout--phase">
        <dt className="mono-label">{telemetry.statusLabel}</dt>
        <dd>
          <span className="readout__phase">{telemetry.statusWord}</span>
          <span className="readout__suffix"> / {telemetry.statusSuffix}</span>
          {telemetry.flags.length > 0 && (
            <ul className="flags" aria-label="Device flags">
              {telemetry.flags.map((flag) => {
                const severity = flagSeverity(flag);
                return (
                  <li key={flag} className={`flag flag--${severity}`}>
                    {severity === 'fault' ? `LATCHED ${flagLabel(flag)}` : flagLabel(flag)}
                  </li>
                );
              })}
            </ul>
          )}
        </dd>
      </div>
      <div className="readout">
        <dt className="mono-label">HEADING</dt>
        <dd className="readout__value">{formatHeading(telemetry.headingDeg)}</dd>
      </div>
      <div className="readout readout--target">
        <dt className="mono-label">TARGET</dt>
        <dd className="readout__value">{formatHeading(telemetry.targetDeg)}</dd>
      </div>
      <div className="readout">
        <dt className="mono-label">BODY RATE</dt>
        <dd className="readout__value">{telemetry.rateDegS === null ? '—' : formatRate(telemetry.rateDegS)}</dd>
      </div>
      <div className="readout">
        <dt className="mono-label">REACTION WHEEL</dt>
        <dd className="readout__value">
          {telemetry.wheelRpm === null ? '—' : formatRpm(telemetry.wheelRpm)}
          <span className="readout__note">{telemetry.wheelNote}</span>
        </dd>
      </div>
      {telemetry.throttle !== null && (
        <div className="readout">
          <dt className="mono-label">MOTOR COMMAND</dt>
          <dd className="readout__value">
            {Math.round(telemetry.throttle)}
            <span className="readout__note">
              DSHOT{telemetry.torqueMNm === null ? '' : ` · TORQUE ${telemetry.torqueMNm.toFixed(1)} mN·m`}
            </span>
          </dd>
        </div>
      )}
      {telemetry.throttleBand !== null && (
        <div className="readout">
          <dt className="mono-label">THROTTLE BAND</dt>
          <dd className="readout__value">
            {Math.round(telemetry.throttleBand[0])}–{Math.round(telemetry.throttleBand[1])}
            <span className="readout__note">MANUAL CLAMP / DSHOT</span>
          </dd>
        </div>
      )}
      {telemetry.batteryV !== null && (
        <div className="readout">
          <dt className="mono-label">BATTERY</dt>
          <dd className="readout__value">
            {telemetry.batteryV.toFixed(2)} V<span className="readout__note">DEVICE REPORTED</span>
          </dd>
        </div>
      )}
    </dl>
  );
}
