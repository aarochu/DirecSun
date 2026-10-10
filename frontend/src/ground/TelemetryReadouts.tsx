import { formatHeading, formatRate, formatRpm } from '../lib/format';
import type { DisplayTelemetry } from './displaySource';

export function TelemetryReadouts({ telemetry }: { telemetry: DisplayTelemetry }) {
  return (
    <dl className="readouts">
      <div className="readout readout--phase">
        <dt className="mono-label">{telemetry.statusLabel}</dt>
        <dd>
          <span className="readout__phase">{telemetry.statusWord}</span>
          <span className="readout__suffix"> / {telemetry.statusSuffix}</span>
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
