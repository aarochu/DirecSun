import { memo, useId } from 'react';
import { useElementSize } from '../lib/useElementSize';
import { HISTORY_WINDOW_S } from '../simulation/constants';

export interface SeriesPoint {
  readonly t: number;
  readonly value: number;
  readonly ref?: number | null;
}

export interface ChartBand {
  readonly from: number;
  readonly to: number;
  readonly label: string;
}

export interface ChartMarker {
  readonly value: number;
  readonly label: string;
}

interface SeriesChartProps {
  readonly title: string;
  readonly unit: string;
  readonly points: readonly SeriesPoint[];
  readonly nowS: number;
  readonly domain: readonly [number, number];
  readonly ticks: readonly number[];
  readonly bands?: readonly ChartBand[];
  readonly markers?: readonly ChartMarker[];
  readonly refLabel?: string;
  readonly height?: number;
  readonly describe: string;
}

const PAD = { left: 44, right: 10, top: 8, bottom: 20 };

export const SeriesChart = memo(function SeriesChart({
  title,
  unit,
  points,
  nowS,
  domain,
  ticks,
  bands = [],
  markers = [],
  refLabel,
  height = 120,
  describe,
}: SeriesChartProps) {
  const [ref, size] = useElementSize<HTMLDivElement>();
  const width = size.width;
  const clipId = useId();
  const start = nowS - HISTORY_WINDOW_S;
  const [lo, hi] = domain;
  const plotW = Math.max(40, width - PAD.left - PAD.right);
  const plotH = Math.max(30, height - PAD.top - PAD.bottom);
  const x = (t: number) => PAD.left + ((t - start) / HISTORY_WINDOW_S) * plotW;
  const y = (v: number) => PAD.top + ((hi - Math.min(hi, Math.max(lo, v))) / (hi - lo)) * plotH;
  const visible = points.filter((point) => point.t >= start - 0.2 && point.t <= nowS);
  const line = visible.map((p, i) => `${i === 0 ? 'M' : 'L'}${x(p.t).toFixed(1)} ${y(p.value).toFixed(1)}`).join(' ');
  const refPoints = visible.filter((p) => p.ref !== null && p.ref !== undefined);
  const refLine = refPoints.map((p, i) => `${i === 0 ? 'M' : 'L'}${x(p.t).toFixed(1)} ${y(p.ref!).toFixed(1)}`).join(' ');
  const last = visible[visible.length - 1];

  return (
    <figure className="history series">
      <div className="history__legend" aria-hidden="true">
        <span>
          <i className="history__swatch history__swatch--heading" />
          {title}
        </span>
        {refLabel && refPoints.length > 0 ? (
          <span>
            <i className="history__swatch history__swatch--target" />
            {refLabel}
          </span>
        ) : null}
        <span className="series__value">{last ? `${formatValue(last.value)} ${unit}` : '—'}</span>
      </div>
      <div className="history__plot" ref={ref} style={{ height }}>
        <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} aria-hidden="true">
          <defs>
            <clipPath id={clipId}>
              <rect x={PAD.left} y={PAD.top - 2} width={plotW} height={plotH + 4} />
            </clipPath>
          </defs>
          {bands.map((band) => (
            <g key={band.label}>
              <rect
                className="series__band"
                x={PAD.left}
                width={plotW}
                y={y(Math.max(band.from, band.to))}
                height={Math.max(0, y(Math.min(band.from, band.to)) - y(Math.max(band.from, band.to)))}
              />
              <text className="series__band-label" x={PAD.left + plotW - 4} y={y(Math.max(band.from, band.to)) + 10} textAnchor="end">
                {band.label}
              </text>
            </g>
          ))}
          {ticks.map((v) => (
            <g key={v}>
              <line className={v === 0 ? 'history__zero' : 'history__grid'} x1={PAD.left} x2={PAD.left + plotW} y1={y(v)} y2={y(v)} />
              <text className="history__tick" x={PAD.left - 8} y={y(v)} dy="0.32em" textAnchor="end">
                {formatValue(v)}
              </text>
            </g>
          ))}
          {markers.map((marker) => (
            <g key={marker.label}>
              <line className="series__marker" x1={PAD.left} x2={PAD.left + plotW} y1={y(marker.value)} y2={y(marker.value)} />
              <text className="series__band-label" x={PAD.left + 4} y={y(marker.value) - 3}>
                {marker.label}
              </text>
            </g>
          ))}
          <text className="history__axis" x={PAD.left + plotW} y={height - 2} textAnchor="end">
            LAST {HISTORY_WINDOW_S} S
          </text>
          <text className="history__axis" x={PAD.left} y={height - 2}>
            {unit}
          </text>
          <g clipPath={`url(#${clipId})`}>
            {refLine ? <path className="history__target" d={refLine} /> : null}
            {line ? <path className="history__heading" d={line} /> : null}
            {last ? <circle className="history__dot" cx={x(last.t)} cy={y(last.value)} r="2.5" /> : null}
          </g>
        </svg>
      </div>
      <figcaption className="sr-only">{describe}</figcaption>
    </figure>
  );
});

function formatValue(v: number): string {
  if (Math.abs(v) >= 100) return String(Math.round(v));
  return Number.isInteger(v) ? String(v) : v.toFixed(1);
}
