import { memo } from 'react';

const DEG = Math.PI / 180;

export const VIEW_CAMERA_BEARING_DEG = 205;
export const VIEW_ELEVATION_DEG = 22;

const CAMERA_BEARING = VIEW_CAMERA_BEARING_DEG * DEG;
const ELEVATION = VIEW_ELEVATION_DEG * DEG;
const COS_EL = Math.cos(ELEVATION);
const SIN_EL = Math.sin(ELEVATION);
const RIGHT_E = Math.sin(CAMERA_BEARING - Math.PI / 2);
const RIGHT_N = Math.cos(CAMERA_BEARING - Math.PI / 2);
const CAM_E = Math.sin(CAMERA_BEARING);
const CAM_N = Math.cos(CAMERA_BEARING);
const LIGHT_E = Math.sin(250 * DEG);
const LIGHT_N = Math.cos(250 * DEG);

const HALF = 50;
const BODY_HEIGHT = 227;
const BODY_Z0 = 14;
const BODY_Z1 = BODY_Z0 + BODY_HEIGHT;
const ONE_U_Z = BODY_Z0 + BODY_HEIGHT / 2;
const FLOOR_Z = -24;
const HUB_RADIUS = 30;
const RING_RADIUS = 104;
const GRID_RADIUS = 260;
const GRID_STEP = 40;

type Point2 = readonly [number, number];

function project(e: number, n: number, z: number): Point2 {
  const x = e * RIGHT_E + n * RIGHT_N;
  const away = -(e * CAM_E + n * CAM_N);
  return [x, -(z * COS_EL + away * SIN_EL)];
}

function bodyToWorld(x: number, y: number, z: number, psi: number): [number, number, number] {
  const s = Math.sin(psi);
  const c = Math.cos(psi);
  return [x * s + y * c, x * c - y * s, z];
}

const fmt = (value: number) => value.toFixed(2);
const pointList = (points: readonly Point2[]) => points.map(([x, y]) => `${fmt(x)},${fmt(y)}`).join(' ');
const pathOf = (points: readonly Point2[], close = false) =>
  points.map(([x, y], i) => `${i === 0 ? 'M' : 'L'}${fmt(x)} ${fmt(y)}`).join(' ') + (close ? ' Z' : '');

const GRID_PATH = (() => {
  const segments: string[] = [];
  for (let offset = -GRID_RADIUS; offset <= GRID_RADIUS; offset += GRID_STEP) {
    const span = Math.sqrt(Math.max(0, GRID_RADIUS ** 2 - offset ** 2));
    segments.push(pathOf([project(offset, -span, FLOOR_Z), project(offset, span, FLOOR_Z)]));
    segments.push(pathOf([project(-span, offset, FLOOR_Z), project(span, offset, FLOOR_Z)]));
  }
  return segments.join(' ');
})();

const FLOOR_CENTER = project(0, 0, FLOOR_Z);

const RING_TICKS = (() => {
  const segments: string[] = [];
  for (let deg = 0; deg < 360; deg += 15) {
    const major = deg % 90 === 0;
    const minor = deg % 30 !== 0;
    const inner = RING_RADIUS + (minor ? 3 : 0);
    const outer = RING_RADIUS + (major ? 16 : minor ? 7 : 10);
    const b = deg * DEG;
    segments.push(
      pathOf([
        project(inner * Math.sin(b), inner * Math.cos(b), FLOOR_Z),
        project(outer * Math.sin(b), outer * Math.cos(b), FLOOR_Z),
      ]),
    );
  }
  return segments.join(' ');
})();

const HUB = (() => {
  const top = project(0, 0, 0);
  const bottom = project(0, 0, FLOOR_Z);
  const ry = HUB_RADIUS * SIN_EL;
  const side =
    `M${fmt(-HUB_RADIUS)} ${fmt(top[1])} L${fmt(-HUB_RADIUS)} ${fmt(bottom[1])} ` +
    `A${HUB_RADIUS} ${fmt(ry)} 0 0 0 ${fmt(HUB_RADIUS)} ${fmt(bottom[1])} ` +
    `L${fmt(HUB_RADIUS)} ${fmt(top[1])} Z`;
  return { side, topY: top[1], ry };
})();

const AXIS_LOWER = [project(0, 0, FLOOR_Z - 44), project(0, 0, 0)] as const;
const AXIS_UPPER = [project(0, 0, BODY_Z1), project(0, 0, BODY_Z1 + 52)] as const;

const VIEWBOX = (() => {
  const points: Point2[] = [];
  for (let deg = 0; deg < 360; deg += 10) {
    const b = deg * DEG;
    for (const [r, z] of [
      [RING_RADIUS + 16, FLOOR_Z],
      [96, BODY_Z1],
      [72, BODY_Z0],
    ] as const) {
      points.push(project(r * Math.sin(b), r * Math.cos(b), z));
    }
  }
  points.push(AXIS_LOWER[0], AXIS_UPPER[1]);
  const pad = 18;
  const xs = points.map((p) => p[0]);
  const ys = points.map((p) => p[1]);
  const minX = Math.min(...xs) - pad;
  const minY = Math.min(...ys) - pad;
  return `${fmt(minX)} ${fmt(minY)} ${fmt(Math.max(...xs) - minX + pad)} ${fmt(Math.max(...ys) - minY + pad)}`;
})();

interface SideFace {
  normal: readonly [number, number];
  corners: readonly (readonly [number, number])[];
}

const SIDE_FACES: readonly SideFace[] = [
  { normal: [1, 0], corners: [[HALF, -HALF], [HALF, HALF]] },
  { normal: [-1, 0], corners: [[-HALF, HALF], [-HALF, -HALF]] },
  { normal: [0, 1], corners: [[HALF, HALF], [-HALF, HALF]] },
  { normal: [0, -1], corners: [[-HALF, -HALF], [HALF, -HALF]] },
];

const SHADE_DARK = [16, 16, 16] as const;
const SHADE_LIGHT = [44, 44, 44] as const;
const shade = (t: number) => {
  const mix = (i: 0 | 1 | 2) => Math.round(SHADE_DARK[i] + (SHADE_LIGHT[i] - SHADE_DARK[i]) * t);
  return `rgb(${mix(0)} ${mix(1)} ${mix(2)})`;
};

function bodyGeometry(headingDeg: number) {
  const psi = headingDeg * DEG;
  const faces = SIDE_FACES.flatMap((face) => {
    const [ne, nn] = bodyToWorld(face.normal[0], face.normal[1], 0, psi);
    if (ne * CAM_E + nn * CAM_N <= 1e-6) return [];
    const [[x0, y0], [x1, y1]] = face.corners;
    const bottomA = bodyToWorld(x0, y0, BODY_Z0, psi);
    const bottomB = bodyToWorld(x1, y1, BODY_Z0, psi);
    const outline = [
      project(bottomA[0], bottomA[1], BODY_Z0),
      project(bottomB[0], bottomB[1], BODY_Z0),
      project(bottomB[0], bottomB[1], BODY_Z1),
      project(bottomA[0], bottomA[1], BODY_Z1),
    ];
    const deck = [project(bottomA[0], bottomA[1], ONE_U_Z), project(bottomB[0], bottomB[1], ONE_U_Z)];
    const light = Math.max(0, ne * LIGHT_E + nn * LIGHT_N);
    return [{ key: `${face.normal}`, outline, deck, fill: shade(0.15 + 0.6 * light) }];
  });

  const top = (
    [
      [HALF, -HALF],
      [HALF, HALF],
      [-HALF, HALF],
      [-HALF, -HALF],
    ] as const
  ).map(([x, y]) => {
    const [e, n] = bodyToWorld(x, y, 0, psi);
    return project(e, n, BODY_Z1);
  });

  const onTop = (x: number, y: number) => {
    const [e, n] = bodyToWorld(x, y, 0, psi);
    return project(e, n, BODY_Z1);
  };
  const arrow = {
    shaft: [onTop(0, 0), onTop(66, 0)] as const,
    head: [onTop(84, 0), onTop(64, -10), onTop(64, 10)] as const,
    label: onTop(100, 0),
  };

  return { faces, top, arrow };
}

export const CubeSatSchematic = memo(function CubeSatSchematic({ headingDeg }: { headingDeg: number }) {
  const { faces, top, arrow } = bodyGeometry(headingDeg);

  return (
    <svg className="schematic" viewBox={VIEWBOX} preserveAspectRatio="xMidYMid meet" aria-hidden="true">
      <defs>
        <radialGradient
          id="schematic-grid-fade"
          gradientUnits="userSpaceOnUse"
          cx={FLOOR_CENTER[0]}
          cy={FLOOR_CENTER[1]}
          r={GRID_RADIUS}
          gradientTransform={`translate(0 ${fmt(FLOOR_CENTER[1])}) scale(1 ${fmt(SIN_EL)}) translate(0 ${fmt(-FLOOR_CENTER[1])})`}
        >
          <stop offset="0" stopColor="#fff" stopOpacity="1" />
          <stop offset="0.55" stopColor="#fff" stopOpacity="0.55" />
          <stop offset="1" stopColor="#fff" stopOpacity="0" />
        </radialGradient>
        <mask id="schematic-grid-mask" maskUnits="userSpaceOnUse" x="-2000" y="-2000" width="4000" height="4000">
          <rect x="-2000" y="-2000" width="4000" height="4000" fill="url(#schematic-grid-fade)" />
        </mask>
      </defs>

      <g className="schematic__fixed">
        <path className="schematic__grid" d={GRID_PATH} mask="url(#schematic-grid-mask)" />
        <ellipse
          className="schematic__ring"
          cx={FLOOR_CENTER[0]}
          cy={FLOOR_CENTER[1]}
          rx={RING_RADIUS}
          ry={RING_RADIUS * SIN_EL}
        />
        <path className="schematic__ticks" d={RING_TICKS} />
        <path className="schematic__axis" d={pathOf(AXIS_LOWER)} />
        <path className="schematic__hub" d={HUB.side} />
        <ellipse className="schematic__hub" cx={0} cy={HUB.topY} rx={HUB_RADIUS} ry={HUB.ry} />
      </g>

      <g className="schematic__body">
        {faces.map((face) => (
          <g key={face.key}>
            <polygon className="schematic__face" points={pointList(face.outline)} fill={face.fill} />
            <path className="schematic__deck" d={pathOf(face.deck)} />
          </g>
        ))}
        <polygon className="schematic__face schematic__face--top" points={pointList(top)} />
        <path className="schematic__arrow" d={pathOf(arrow.shaft)} />
        <polygon className="schematic__arrow-head" points={pointList(arrow.head)} />
        <text className="schematic__arrow-label" x={arrow.label[0]} y={arrow.label[1]} dy="0.35em">
          +X
        </text>
      </g>

      <path className="schematic__axis" d={pathOf(AXIS_UPPER)} />
    </svg>
  );
});
