import { useSyncExternalStore } from 'react';
import type { DisplaySource } from './displaySource';
import { HeadingHistoryChart } from './HeadingHistoryChart';
import { TelemetryReadouts } from './TelemetryReadouts';

export function TelemetryRail({ source }: { source: DisplaySource }) {
  const telemetry = useSyncExternalStore(source.subscribeTelemetry, source.getTelemetry);
  const live = telemetry.source === 'hardware';
  return (
    <section className="rail" aria-labelledby="rail-title">
      <div className="rail__header">
        <h2 className="mono-label" id="rail-title">
          TELEMETRY
        </h2>
        <p className={`rail__source mono-label${live ? ' rail__source--device' : ''}`}>
          {live ? 'FROM DEVICE — WEBSOCKET' : 'SIMULATED VALUES — NOT SENSOR DATA'}
        </p>
      </div>
      <TelemetryReadouts telemetry={telemetry} />
      <div className="rail__history">
        <h3 className="mono-label">HEADING HISTORY</h3>
        <HeadingHistoryChart telemetry={telemetry} />
      </div>
    </section>
  );
}
