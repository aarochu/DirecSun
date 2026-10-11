import { memo, useId } from 'react';
import { formatSigned, spokenHeading } from '../lib/format';
import { useElementSize } from '../lib/useElementSize';
import { HISTORY_WINDOW_S } from '../simulation/constants';
import type { HistorySample } from '../simulation/types';
import type { DisplayTelemetry } from './displaySource';

interface HeadingHistoryChartProps {
  telemetry: DisplayTelemetry;
  height?: number;
}

const PAD = { left: 40, right: 10, top: 10, bottom: 34 };
const X_TICKS = [-15, -10, -5, 0];

function domainFor(samples: readonly HistorySample[]): [number, number] {
  let lo = -120;
  let hi = 120;
  for (const sample of samples) {
    lo = Math.min(lo, sample.heading, sample.target);
    hi = Math.max(hi, sample.heading, sample.target);
  }
  return [Math.floor((lo - 10) / 45) * 45, Math.ceil((hi + 10) / 45) * 45];
}

export const HeadingHistoryChart = memo(function HeadingHistoryChart({
  telemetry,
  height: fixedHeight,
}: HeadingHistoryChartProps) {
  const [ref, size] = useElementSize<HTMLDivElement>();
  const width = size.width;
  const height = fixedHeight ?? size.height;
  const clipId = useId();
  const { history, nowS: now, headingDeg, targetDeg } = telemetry;
  const start = now - HISTORY_WINDOW_S;
  const samples: HistorySample[] = history.filter((sample) => sample.t >= start - 0.2 && sample.t < now);
  samples.push({ t: now, heading: headingDeg, target: targetDeg });

  const [lo, hi] = domainFor(samples);
  const plotW = Math.max(40, width - PAD.left - PAD.right);
  const plotH = Math.max(40, height - PAD.top - PAD.bottom);
  const x = (t: number) => PAD.left + ((t - start) / HISTORY_WINDOW_S) * plotW;
  const y = (deg: number) => PAD.top + ((hi - deg) / (hi - lo)) * plotH;
  const yStep = hi - lo > 360 ? 180 : 90;
  const yTicks: number[] = [];
  for (let v = Math.ceil(lo / yStep) * yStep; v <= hi; v += yStep) yTicks.push(v);

  const headingPath = samples
    .map((s, i) => `${i === 0 ? 'M' : 'L'}${x(s.t).toFixed(1)} ${y(s.heading).toFixed(1)}`)
    .join(' ');
  const targetPath = samples
    .map((s, i) =>
      i === 0
        ? `M${x(s.t).toFixed(1)} ${y(s.target).toFixed(1)}`
        : `H${x(s.t).toFixed(1)} V${y(s.target).toFixed(1)}`,
    )
    .join(' ');
  const last = samples[samples.length - 1]!;

  return (
    <figure className="history">
      <div className="history__legend" aria-hidden="true">
        <span>
          <i className="history__swatch history__swatch--heading" />
          HEADING
        </span>
        <span>
          <i className="history__swatch history__swatch--target" />
          TARGET
        </span>
      </div>
      <div className="history__plot" ref={ref} style={fixedHeight ? { height: fixedHeight } : undefined}>
        <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} aria-hidden="true">
          <defs>
            <clipPath id={clipId}>
              <rect x={PAD.left} y={PAD.top - 2} width={plotW} height={plotH + 4} />
            </clipPath>
          </defs>
          {yTicks.map((v) => (
            <g key={v}>
              <line
                className={v === 0 ? 'history__zero' : 'history__grid'}
                x1={PAD.left}
                x2={PAD.left + plotW}
                y1={y(v)}
                y2={y(v)}
              />
              <text className="history__tick" x={PAD.left - 8} y={y(v)} dy="0.32em" textAnchor="end">
                {formatSigned(v, 0)}
              </text>
            </g>
          ))}
          {X_TICKS.map((t) => (
            <g key={t}>
              <line
                className="history__grid"
                x1={x(now + t)}
                x2={x(now + t)}
                y1={PAD.top}
                y2={PAD.top + plotH}
              />
              <text className="history__tick" x={x(now + t)} y={PAD.top + plotH + 14} textAnchor="middle">
                {formatSigned(t, 0)}
              </text>
            </g>
          ))}
          <text className="history__axis" x={PAD.left + plotW} y={height - 2} textAnchor="end">
            TIME / S
          </text>
          <text className="history__axis" x={PAD.left} y={height - 2}>
            HEADING / DEG
          </text>
          <g clipPath={`url(#${clipId})`}>
            <path className="history__target" d={targetPath} />
            <path className="history__heading" d={headingPath} />
            <circle className="history__dot" cx={x(last.t)} cy={y(last.heading)} r="2.5" />
          </g>
        </svg>
      </div>
      <figcaption className="sr-only">
        {telemetry.source === 'simulation' ? 'Simulated' : 'Device'} heading {spokenHeading(headingDeg)},
        target {spokenHeading(targetDeg)}. Plot covers the last {HISTORY_WINDOW_S} seconds
        {telemetry.source === 'simulation' ? ' of simulation time' : ''}.
      </figcaption>
    </figure>
  );
});
