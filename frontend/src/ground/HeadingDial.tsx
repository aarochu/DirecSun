import { memo } from 'react';

interface HeadingDialProps {
  headingDeg: number;
  targetDeg: number;
  originDeg: number | null;
}

const R = 38;
const DEG = Math.PI / 180;

const polar = (deg: number, radius: number) =>
  [radius * Math.sin(deg * DEG), -radius * Math.cos(deg * DEG)] as const;

const TICKS = Array.from({ length: 12 }, (_, i) => i * 30).map((deg) => {
  const [x1, y1] = polar(deg, R - (deg % 90 === 0 ? 7 : 4));
  const [x2, y2] = polar(deg, R);
  return `M${x1.toFixed(2)} ${y1.toFixed(2)} L${x2.toFixed(2)} ${y2.toFixed(2)}`;
}).join(' ');

function marker(deg: number) {
  const tip = polar(deg, R + 2);
  const left = polar(deg - 7, R + 11);
  const right = polar(deg + 7, R + 11);
  return [tip, left, right].map(([x, y]) => `${x.toFixed(2)},${y.toFixed(2)}`).join(' ');
}

export const HeadingDial = memo(function HeadingDial({ headingDeg, targetDeg, originDeg }: HeadingDialProps) {
  const [nx, ny] = polar(headingDeg, R - 10);
  return (
    <svg className="dial" viewBox="-56 -56 112 112" aria-hidden="true">
      <circle className="dial__ring" r={R} />
      <path className="dial__ticks" d={TICKS} />
      <text className="dial__label" x="0" y={-R + 14} textAnchor="middle">
        0°
      </text>
      {originDeg !== null && <polygon className="dial__start" points={marker(originDeg)} />}
      <polygon className="dial__target" points={marker(targetDeg)} />
      <line className="dial__needle" x1="0" y1="0" x2={nx.toFixed(2)} y2={ny.toFixed(2)} />
      <circle className="dial__hub" r="2.5" />
    </svg>
  );
});
