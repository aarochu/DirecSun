// Visual detail for the electronics, added on the website only. The CAD in
// cad/ models these parts as plain blocks (stand-ins), so this file rebuilds a
// few of them, or adds parts on top, inside each part's own CAD bounding box.
//
// Dimensions come from docs/technical-design.md (Sections 4.8, 5) and the
// boards' published footprints; where the docs do not place a component (the
// layout on the two perfboards), the placement is illustrative. Nothing here
// feeds back into the CAD, the assembly check, or the GLB.

import {
  BoxGeometry,
  BufferGeometry,
  CircleGeometry,
  CylinderGeometry,
  Matrix4,
  Quaternion,
  Vector3,
  type Box3,
} from 'three';

export type Tone =
  | 'pcb'
  | 'chip'
  | 'metal'
  | 'metalDark'
  | 'connector'
  | 'gold'
  | 'hole'
  | 'wireRed'
  | 'wireBlack'
  | 'wireLight'
  | 'cap'
  | 'resistor'
  | 'terminal'
  | 'fuse';

// Base colours, before the highlight lerp in scene.ts. Mostly greys so the
// model keeps its CAD look; a few parts carry their real colour.
export const TONES: Record<Tone, string> = {
  pcb: '#6f7370',
  chip: '#262626',
  metal: '#c9c9c9',
  metalDark: '#7a7a7a',
  connector: '#e8e6e1',
  gold: '#c2a46a',
  hole: '#121212',
  wireRed: '#a8473b',
  wireBlack: '#2a2a2a',
  wireLight: '#d8d8d8',
  cap: '#2f3a48',
  resistor: '#c8b48c',
  terminal: '#4c6b57',
  fuse: '#b5483c',
};

export interface DetailItem {
  geometry: BufferGeometry;
  tone: Tone;
  // One matrix per instance; world space, metres.
  matrices: Matrix4[];
  edges: boolean;
}

export interface Detail {
  // Hide the CAD stand-in and show only the detail.
  replace: boolean;
  items: DetailItem[];
}

const MM = 0.001;

class Builder {
  readonly items: DetailItem[] = [];
  private readonly boxGeo = new BoxGeometry(1, 1, 1);
  private readonly cylGeo = new CylinderGeometry(1, 1, 1, 24);
  private readonly discGeo = new CircleGeometry(1, 12);

  private add(geometry: BufferGeometry, tone: Tone, matrix: Matrix4, edges = true) {
    this.items.push({ geometry, tone, matrices: [matrix], edges });
  }

  // Axis-aligned box from corner to corner, in mm.
  box(x0: number, x1: number, y0: number, y1: number, z0: number, z1: number, tone: Tone) {
    const m = new Matrix4().compose(
      new Vector3(((x0 + x1) / 2) * MM, ((y0 + y1) / 2) * MM, ((z0 + z1) / 2) * MM),
      new Quaternion(),
      new Vector3(Math.abs(x1 - x0) * MM, Math.abs(y1 - y0) * MM, Math.abs(z1 - z0) * MM),
    );
    this.add(this.boxGeo, tone, m);
  }

  // Cylinder along the vertical (spin) axis.
  cyl(cx: number, cz: number, y0: number, y1: number, r: number, tone: Tone) {
    const m = new Matrix4().compose(
      new Vector3(cx * MM, ((y0 + y1) / 2) * MM, cz * MM),
      new Quaternion(),
      new Vector3(r * MM, Math.abs(y1 - y0) * MM, r * MM),
    );
    this.add(this.cylGeo, tone, m);
  }

  // Cylinder lying along x or z (axial resistors, diodes, wires).
  rod(axis: 'x' | 'z', a0: number, a1: number, cy: number, c: number, r: number, tone: Tone) {
    const q = new Quaternion().setFromAxisAngle(new Vector3(axis === 'x' ? 0 : 1, 0, axis === 'x' ? 1 : 0), Math.PI / 2);
    const mid = ((a0 + a1) / 2) * MM;
    const pos = axis === 'x' ? new Vector3(mid, cy * MM, c * MM) : new Vector3(c * MM, cy * MM, mid);
    const m = new Matrix4().compose(pos, q, new Vector3(r * MM, Math.abs(a1 - a0) * MM, r * MM));
    this.add(this.cylGeo, tone, m);
  }

  // Flat discs on a horizontal face: board holes and pads. up: face normal +y.
  discs(points: readonly (readonly [number, number])[], y: number, r: number, up: boolean, tone: Tone) {
    const q = new Quaternion().setFromAxisAngle(new Vector3(1, 0, 0), up ? -Math.PI / 2 : Math.PI / 2);
    const matrices = points.map(([x, z]) =>
      new Matrix4().compose(new Vector3(x * MM, y * MM, z * MM), q, new Vector3(r * MM, r * MM, r * MM)),
    );
    this.items.push({ geometry: this.discGeo, tone, matrices, edges: false });
  }
}

interface Mm {
  x0: number;
  x1: number;
  y0: number;
  y1: number;
  z0: number;
  z1: number;
  cx: number;
  cz: number;
}

const toMm = (box: Box3): Mm => ({
  x0: box.min.x / MM,
  x1: box.max.x / MM,
  y0: box.min.y / MM,
  y1: box.max.y / MM,
  z0: box.min.z / MM,
  z1: box.max.z / MM,
  cx: (box.min.x + box.max.x) / 2 / MM,
  cz: (box.min.z + box.max.z) / 2 / MM,
});

// 2.54 mm hole grid over a perfboard's top face, inset by one pitch.
function holeGrid(b: Builder, p: Mm, top: number) {
  const points: [number, number][] = [];
  for (let x = p.x0 + 2.54; x <= p.x1 - 2.0; x += 2.54) {
    for (let z = p.z0 + 2.54; z <= p.z1 - 2.0; z += 2.54) points.push([x, z]);
  }
  b.discs(points, top + 0.02, 0.5, true, 'hole');
}

// Adafruit STEMMA QT breakout, 25.40 x 17.78 mm (TD 4.8): PCB, chip at the
// centre, a STEMMA QT connector on each short end (+-X), four 2.5 mm holes
// 2.54 mm in from the corners, and a row of header pads. flip: mounted chips
// down under the top deck.
function stemmaBoard(p: Mm, chip: { w: number; d: number; tone: Tone }, flip: boolean): Detail {
  const b = new Builder();
  const t = 1.6;
  const s = flip ? -1 : 1;
  // PCB against the mounting face; components grow away from it.
  const pcb0 = flip ? p.y1 - t : p.y0;
  const face = flip ? pcb0 : pcb0 + t;
  const up = (h: number) => (flip ? [face - h, face] : [face, face + h]) as [number, number];
  b.box(p.x0, p.x1, pcb0, pcb0 + t, p.z0, p.z1, 'pcb');

  const [cy0, cy1] = up(0.9);
  b.box(p.cx - chip.w / 2, p.cx + chip.w / 2, cy0, cy1, p.cz - chip.d / 2, p.cz + chip.d / 2, chip.tone);

  // JST SH 4-pin (STEMMA QT): about 4.25 x 6.0 x 2.95 mm.
  const [qy0, qy1] = up(2.95);
  for (const side of [-1, 1]) {
    const x = side > 0 ? p.x1 - 0.4 : p.x0 + 0.4;
    b.box(x - side * 4.25, x, qy0, qy1, p.cz - 3.0, p.cz + 3.0, 'connector');
  }

  // Regulator (SOT-23-5) and a few 0603 passives.
  const [ry0, ry1] = up(1.1);
  b.box(p.cx - 6.2, p.cx - 3.2, ry0, ry1, p.cz + 3.0, p.cz + 4.6, 'chip');
  const [py0, py1] = up(0.5);
  for (const [dx, dz] of [
    [3.4, 3.4],
    [3.4, -3.4],
    [-3.4, -3.6],
    [5.6, 0.2],
  ]) {
    b.box(p.cx + dx - 0.8, p.cx + dx + 0.8, py0, py1, p.cz + dz - 0.4, p.cz + dz + 0.4, 'metalDark');
  }

  // Mounting holes and the header pad row along the -Z long edge.
  const holes: [number, number][] = [];
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) holes.push([p.cx + sx * (12.7 - 2.54), p.cz + sz * (8.89 - 2.54)]);
  const outer = face + s * 0.02;
  b.discs(holes, outer, 1.25, !flip, 'hole');
  const pads: [number, number][] = [];
  for (let i = -2.5; i <= 2.5; i++) pads.push([p.cx + i * 2.54, p.z0 + 1.4]);
  b.discs(pads, outer, 0.85, !flip, 'gold');
  b.discs(pads, outer + s * 0.01, 0.45, !flip, 'hole');
  return { replace: true, items: b.items };
}

// Seeed XIAO ESP32-S3, 21 x 17.8 mm (Seeed wiki): PCB on two header strips
// over the carrier, RF shield can, USB-C on the end at the carrier's open edge
// (+Z), BOOT and RESET buttons beside it, seven castellated pads per side.
function xiao(p: Mm): Detail {
  const b = new Builder();
  const pcb0 = p.y0;
  const pcb1 = pcb0 + 1.0;
  b.box(p.x0, p.x1, pcb0, pcb1, p.z0, p.z1, 'pcb');

  // Header strips down to the carrier (the 2.5 mm gap under the box).
  const len = 7 * 2.54;
  const zc = (p.z0 + p.z1) / 2 - 1.0;
  for (const side of [-1, 1]) {
    const x = p.cx + side * (8.9 - 1.27);
    b.box(x - 1.27, x + 1.27, pcb0 - 2.5, pcb0, zc - len / 2, zc + len / 2, 'chip');
    for (let i = -3; i <= 3; i++) {
      const z = zc + i * 2.54;
      const edge = side > 0 ? p.x1 : p.x0;
      b.box(edge - side * 1.0, edge + side * 0.05, pcb0 - 0.05, pcb1 + 0.05, z - 0.6, z + 0.6, 'gold');
    }
  }

  // Shield can over the ESP32-S3 and flash.
  b.box(p.cx - 7.0, p.cx + 7.0, pcb1, pcb1 + 2.2, p.z0 + 1.2, p.z1 - 7.4, 'metal');
  // USB-C receptacle, 8.94 x 3.2 mm, 0.6 mm proud of the edge.
  b.box(p.cx - 4.47, p.cx + 4.47, pcb1, pcb1 + 3.2, p.z1 - 7.0, p.z1 + 0.6, 'metal');
  b.box(p.cx - 3.4, p.cx + 3.4, pcb1 + 0.6, pcb1 + 2.6, p.z1 + 0.6, p.z1 + 0.65, 'hole');
  // BOOT and RESET.
  for (const side of [-1, 1]) {
    const x = p.cx + side * 6.6;
    b.box(x - 1.3, x + 1.3, pcb1, pcb1 + 0.8, p.z1 - 4.2, p.z1 - 1.6, 'chip');
    b.box(x - 0.6, x + 0.6, pcb1 + 0.8, pcb1 + 1.2, p.z1 - 3.5, p.z1 - 2.3, 'connector');
  }
  return { replace: true, items: b.items };
}

// 2212 outrunner (TD 4.3): cross mount, stator, bell with grooves, vented
// top, shaft stub. The bell is drawn static, as the CAD stand-in is.
function motor(p: Mm): Detail {
  const b = new Builder();
  const r = (p.x1 - p.x0) / 2;
  const { cx, cz, y0, y1 } = p;
  b.box(cx - r, cx + r, y0, y0 + 2, cz - 3, cz + 3, 'metalDark');
  b.box(cx - 3, cx + 3, y0, y0 + 2, cz - r, cz + r, 'metalDark');
  b.cyl(cx, cz, y0, y0 + 3.5, 6, 'metalDark');
  b.cyl(cx, cz, y0 + 3.5, y0 + 5, r - 1.2, 'chip');
  b.cyl(cx, cz, y0 + 5, y1 - 2, r, 'metal');
  for (const f of [0.3, 0.5, 0.7]) {
    const y = y0 + 5 + (y1 - 2 - y0 - 5) * f;
    b.cyl(cx, cz, y - 0.3, y + 0.3, r + 0.08, 'metalDark');
  }
  b.cyl(cx, cz, y1 - 2, y1 - 1, r - 1, 'metal');
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2;
    const rx = Math.cos(a) * 8;
    const rz = Math.sin(a) * 8;
    b.box(cx + rx - 1.4, cx + rx + 1.4, y1 - 1.02, y1 - 0.98, cz + rz - 1.4, cz + rz + 1.4, 'hole');
  }
  b.cyl(cx, cz, y1 - 1, y1, 2.5, 'metalDark');
  return { replace: true, items: b.items };
}

// BLHeli_S ESC: keep the CAD block (the heat-shrunk body) and add its leads:
// three motor phases on one end; battery and signal on the other.
function esc(p: Mm): Detail {
  const b = new Builder();
  const yc = (p.y0 + p.y1) / 2;
  const lead = 7;
  [-4.5, 0, 4.5].forEach((dx) => b.rod('z', p.z1, p.z1 + lead, yc, p.cx + dx, 0.9, 'wireBlack'));
  b.rod('z', p.z0 - lead, p.z0, yc, p.cx - 4, 1.1, 'wireRed');
  b.rod('z', p.z0 - lead, p.z0, yc, p.cx + 4, 1.1, 'wireBlack');
  b.rod('z', p.z0 - lead, p.z0, yc + 1.6, p.cx, 0.5, 'wireLight');
  return { replace: false, items: b.items };
}

// Power-deck perfboard (TD 5.1, README "power_deck"): 5 V buck module, the
// 10 A fuse in its holder, the 100k / 22k divider, the 470 uF bulk capacitor
// on the side toward the ESC, and the battery terminal block.
function powerBoard(p: Mm): Detail {
  const b = new Builder();
  const top = p.y1;
  holeGrid(b, p, top);
  // Buck module: its own small board, inductor and controller.
  b.box(p.x0 + 3, p.x0 + 25, top + 2, top + 3.6, p.z0 + 4, p.z0 + 21, 'pcb');
  b.box(p.x0 + 4, p.x0 + 6, top, top + 2, p.z0 + 5, p.z0 + 20, 'chip');
  b.box(p.x0 + 22, p.x0 + 24, top, top + 2, p.z0 + 5, p.z0 + 20, 'chip');
  b.box(p.x0 + 7, p.x0 + 14, top + 3.6, top + 7.6, p.z0 + 8, p.z0 + 15, 'chip');
  b.box(p.x0 + 16, p.x0 + 19, top + 3.6, top + 4.6, p.z0 + 9, p.z0 + 13, 'chip');
  // Fuse holder with the blade fuse.
  b.box(p.x0 + 28, p.x0 + 44, top, top + 8, p.z0 + 6, p.z0 + 14, 'chip');
  b.box(p.x0 + 33, p.x0 + 39, top + 8, top + 11, p.z0 + 8.2, p.z0 + 11.8, 'fuse');
  // Divider: two axial resistors.
  for (const dz of [24, 28]) b.rod('x', p.x0 + 6, p.x0 + 12.5, top + 1.2, p.cz + dz - 26, 1.15, 'resistor');
  // 470 uF 35 V electrolytic, standing, with its vent disc.
  b.cyl(p.x1 - 7, p.z1 - 12, top, top + 16, 5, 'cap');
  b.cyl(p.x1 - 7, p.z1 - 12, top + 16, top + 16.1, 4.4, 'metal');
  // Battery terminal block.
  b.box(p.x0 + 3, p.x0 + 10.5, top, top + 10, p.z1 - 14, p.z1 - 4, 'terminal');
  for (const dz of [-11.5, -6.5]) b.cyl(p.x0 + 6.75, p.z1 + dz, top + 10, top + 10.2, 1.5, 'metal');
  return { replace: false, items: b.items };
}

// XIAO carrier perfboard (cad/README.md): the OP550 load resistors, the Hall
// pull-up and the 1N5817 on the 5 V feed, clear of the XIAO footprint.
function carrier(p: Mm): Detail {
  const b = new Builder();
  const top = p.y1;
  holeGrid(b, p, top);
  const zFree0 = p.z0 + 3;
  for (const [i, x] of [p.x1 - 6, p.x1 - 10, p.x1 - 14].entries()) {
    b.rod('z', zFree0 + i * 0.5, zFree0 + 6.5 + i * 0.5, top + 1.2, x, 1.15, 'resistor');
  }
  b.rod('z', zFree0, zFree0 + 5.2, top + 1.4, p.x0 + 8, 1.35, 'chip');
  b.rod('z', zFree0 + 4.0, zFree0 + 4.7, top + 1.4, p.x0 + 8, 1.4, 'metalDark');
  return { replace: false, items: b.items };
}

export function detailFor(name: string, box: Box3): Detail | null {
  const p = toMm(box);
  switch (name) {
    case 'xiao_s3':
      return xiao(p);
    case 'mpu6050':
      return stemmaBoard(p, { w: 4, d: 4, tone: 'chip' }, false);
    case 'qmc5883p':
      return stemmaBoard(p, { w: 3, d: 3, tone: 'chip' }, true);
    case 'bmp280':
      return stemmaBoard(p, { w: 2, d: 2.5, tone: 'metal' }, true);
    case 'motor_2212':
      return motor(p);
    case 'esc':
      return esc(p);
    case 'board_power':
      return powerBoard(p);
    case 'board_xiao_carrier':
      return carrier(p);
    default:
      return null;
  }
}
