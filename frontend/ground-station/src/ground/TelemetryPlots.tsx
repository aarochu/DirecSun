import { ARM_MIN_BATTERY_V } from '../hardware/modes';
import { WHEEL_MAX_RPM, WHEEL_MIN_RPM } from '../simulation/constants';
import { useSyncExternalStore } from 'react';
import type { DisplaySource, DisplayTelemetry } from './displaySource';
import { SeriesChart, type SeriesPoint } from './SeriesChart';

export const MOMENTUM_WARN_RPM = 300;
export const BATTERY_CUTOFF_V = 8.8;
const WHEEL_DOMAIN: readonly [number, number] = [0, WHEEL_MAX_RPM + 500];

function series(telemetry: DisplayTelemetry, pick: (s: DisplayTelemetry['history'][number]) => number | null | undefined, ref?: (s: DisplayTelemetry['history'][number]) => number | null | undefined) {
  const points: SeriesPoint[] = [];
  for (const sample of telemetry.history) {
    const value = pick(sample);
    if (typeof value === 'number' && Number.isFinite(value)) points.push({ t: sample.t, value, ref: ref?.(sample) ?? null });
  }
  return points;
}

export function rateDomain(points: readonly SeriesPoint[]): readonly [number, number] {
  const peak = points.reduce((max, p) => Math.max(max, Math.abs(p.value)), 0);
  const span = Math.max(30, Math.ceil(peak / 30) * 30);
  return [-span, span];
}

export function batteryDomain(points: readonly SeriesPoint[]): readonly [number, number] {
  let lo = 8.4;
  let hi = 10.8;
  for (const p of points) {
    lo = Math.min(lo, Math.floor(p.value * 2) / 2);
    hi = Math.max(hi, Math.ceil(p.value * 2) / 2);
  }
  return [lo, hi];
}

export function TelemetryPlotsPanel({ source }: { source: DisplaySource }) {
  const telemetry = useSyncExternalStore(source.subscribeTelemetry, source.getTelemetry);
  const live = telemetry.source === 'hardware';
  return (
    <section className="plots-panel" aria-labelledby="plots-title">
      <div className="rail__header">
        <h2 className="mono-label" id="plots-title">
          TELEMETRY PLOTS
        </h2>
        <p className={`rail__source mono-label${live ? ' rail__source--device' : ''}`}>{live ? 'FROM DEVICE' : 'SIMULATED'}</p>
      </div>
      <TelemetryPlots telemetry={telemetry} />
    </section>
  );
}

export function TelemetryPlots({ telemetry }: { telemetry: DisplayTelemetry }) {
  const source = telemetry.source === 'simulation' ? 'Simulated' : 'Device';
  const rate = series(telemetry, (s) => s.rate);
  const wheel = series(telemetry, (s) => s.wheel, (s) => s.wheelRef);
  const battery = series(telemetry, (s) => s.vbat);
  const rateRange = rateDomain(rate);

  return (
    <div className="plots">
      <SeriesChart
        title="BOX RATE"
        unit="DEG/S"
        points={rate}
        nowS={telemetry.nowS}
        domain={rateRange}
        ticks={[rateRange[0], 0, rateRange[1]]}
        describe={`${source} box rate over the last 15 seconds.`}
      />
      <SeriesChart
        title="WHEEL SPEED"
        unit="RPM"
        refLabel="REF"
        points={wheel}
        nowS={telemetry.nowS}
        domain={WHEEL_DOMAIN}
        ticks={[0, WHEEL_MIN_RPM, 2400, WHEEL_MAX_RPM]}
        bands={[
          { from: WHEEL_DOMAIN[0], to: WHEEL_MIN_RPM, label: 'BELOW LOW LIMIT (DEFAULT)' },
          { from: WHEEL_MAX_RPM, to: WHEEL_DOMAIN[1], label: 'ABOVE CAP (DEFAULT)' },
        ]}
        markers={[
          { value: WHEEL_MIN_RPM + MOMENTUM_WARN_RPM, label: 'NEAR LOW LIMIT (DEFAULT)' },
          { value: WHEEL_MAX_RPM - MOMENTUM_WARN_RPM, label: 'NEAR CAP (DEFAULT)' },
        ]}
        describe={`${source} wheel speed over the last 15 seconds, with the default ${WHEEL_MIN_RPM} rpm low limit and ${WHEEL_MAX_RPM} rpm cap shaded. The box's own limits can differ: they are parameters.`}
      />
      {battery.length > 0 ? (
        <SeriesChart
          title="BATTERY"
          unit="V"
          points={battery}
          nowS={telemetry.nowS}
          domain={batteryDomain(battery)}
          ticks={[9, 10]}
          markers={[
            { value: ARM_MIN_BATTERY_V, label: `ARM ABOVE ${ARM_MIN_BATTERY_V} V` },
            { value: BATTERY_CUTOFF_V, label: `CUTOFF ${BATTERY_CUTOFF_V} V` },
          ]}
          describe="Device battery voltage over the last 15 seconds, with the arming threshold and the cutoff marked."
        />
      ) : null}
    </div>
  );
}
