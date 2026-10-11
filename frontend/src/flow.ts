// System flowchart: every electronic part on the box and how they connect.
// Wiring, pins and buses follow docs/technical-design.md (Section 2 diagram,
// Section 5.1 power tree, Section 5.4 pin allocation). Coordinates are
// hand-placed in a 544 × 420 viewBox so each line has its own lane.

type Kind = 'power' | 'sense' | 'control';
type Pt = readonly [number, number];

interface FlowNode {
  id: string;
  name: string;
  sub: string;
  // hardwareParts id, for highlighting the CAD model. null: not on the box.
  hw: string | null;
  desc: string;
  // Extra lines inside the box (the flight computer's jobs).
  lines?: readonly string[];
  x: number;
  y: number;
  w: number;
  h: number;
}

interface FlowEdge {
  from: string;
  to: string;
  kind: Kind;
  label: string;
  at: Pt;
  path: readonly Pt[];
  both?: boolean;
  physical?: boolean;
}

const nodes: readonly FlowNode[] = [
  { id: 'laptop', name: 'Ground station', sub: 'laptop browser', hw: null, x: 200, y: 0, w: 140, h: 40,
    desc: 'Your laptop. It shows the live heading and wheel speed, and sends commands like arm, hold and turn.' },
  { id: 'mpu', name: 'MPU6050', sub: 'gyro', hw: 'mpu6050', x: 0, y: 70, w: 130, h: 40,
    desc: 'Measures how fast the box is turning. The control loop reads it 500 times a second.' },
  { id: 'qmc', name: 'QMC5883P', sub: 'compass', hw: 'qmc5883p', x: 0, y: 135, w: 130, h: 40,
    desc: 'Reads the magnetic field to get a heading. It sits at the top, about 14 cm from the motor’s magnets.' },
  { id: 'bmp', name: 'BMP280', sub: 'temp + pressure', hw: 'bmp280', x: 0, y: 200, w: 130, h: 40,
    desc: 'Extra telemetry for the ground station. The control loop doesn’t use it.' },
  { id: 'sun', name: 'OP550 ×2', sub: 'sun sensor', hw: 'op550', x: 0, y: 265, w: 130, h: 40,
    desc: 'Two light sensors angled apart. Comparing their readings shows which side the lamp is on.' },
  { id: 'xiao', name: 'XIAO ESP32-S3', sub: 'flight computer', hw: 'esp32s3', x: 190, y: 90, w: 160, h: 150,
    lines: ['control loop, 500 Hz', 'heading estimate', 'wheel speed target', 'safety checks', 'telemetry, 50 Hz target'],
    desc: 'Runs the box. It reads every sensor, works out the heading, sets the wheel speed, and streams it all over Wi-Fi.' },
  { id: 'esc', name: 'ESC', sub: 'BLHeli_S', hw: 'esc', x: 400, y: 70, w: 130, h: 40,
    desc: 'Takes a digital throttle from the ESP32 and turns it into three-phase power for the motor.' },
  { id: 'motor', name: '2212 motor', sub: '+ flywheel', hw: 'reaction-wheel', x: 400, y: 150, w: 130, h: 40,
    desc: 'Spins the flywheel. Speeding it up or slowing it down turns the box the other way.' },
  { id: 'hall', name: 'A1104', sub: 'Hall switch', hw: 'a1104', x: 400, y: 230, w: 130, h: 40,
    desc: 'Switches each time one of the two flywheel magnets goes past. The ESP32 times the pulses to get wheel speed.' },
  { id: 'divider', name: 'Divider', sub: '100k / 22k', hw: 'power', x: 190, y: 280, w: 100, h: 36,
    desc: 'Two resistors that scale the battery down so the ESP32 can read it. It won’t arm below 9.6 V.' },
  { id: 'battery', name: 'Battery', sub: '8 × AA, 9.6 V', hw: 'power', x: 0, y: 360, w: 130, h: 44,
    desc: 'Eight rechargeable AAs, 9.6 V nominal. Everything runs off them, so no cable holds the box back.' },
  { id: 'fuse', name: '10 A fuse', sub: 'wiring', hw: 'power', x: 160, y: 364, w: 100, h: 36,
    desc: 'Protects the wiring if something shorts.' },
  { id: 'buck', name: '5 V buck', sub: 'regulator', hw: 'power', x: 290, y: 364, w: 100, h: 36,
    desc: 'Drops the battery to 5 V for the ESP32 and the Hall switch. The ESP32 then feeds 3.3 V to the sensors.' },
];

const edges: readonly FlowEdge[] = [
  { from: 'battery', to: 'fuse', kind: 'power', label: '', at: [0, 0], path: [[130, 382], [160, 382]] },
  { from: 'fuse', to: 'buck', kind: 'power', label: '', at: [0, 0], path: [[260, 382], [290, 382]] },
  { from: 'fuse', to: 'esc', kind: 'power', label: 'motor power', at: [400, 409], path: [[230, 400], [230, 414], [538, 414], [538, 90], [530, 90]] },
  { from: 'fuse', to: 'divider', kind: 'power', label: '', at: [0, 0], path: [[210, 364], [210, 316]] },
  { from: 'buck', to: 'xiao', kind: 'power', label: '5 V', at: [348, 300], path: [[340, 364], [340, 240]] },
  { from: 'buck', to: 'hall', kind: 'power', label: '5 V', at: [430, 376], path: [[390, 382], [465, 382], [465, 270]] },
  { from: 'mpu', to: 'xiao', kind: 'sense', label: 'I²C', at: [162, 84], path: [[130, 90], [160, 90], [160, 120], [190, 120]] },
  { from: 'qmc', to: 'mpu', kind: 'sense', label: 'STEMMA QT', at: [72, 126], path: [[65, 135], [65, 110]] },
  { from: 'bmp', to: 'qmc', kind: 'sense', label: 'STEMMA QT', at: [72, 191], path: [[65, 200], [65, 175]] },
  { from: 'sun', to: 'xiao', kind: 'sense', label: 'D0, D1', at: [140, 279], path: [[130, 285], [160, 285], [160, 210], [190, 210]] },
  { from: 'divider', to: 'xiao', kind: 'sense', label: 'D9', at: [246, 264], path: [[240, 280], [240, 240]] },
  { from: 'hall', to: 'xiao', kind: 'sense', label: 'D10', at: [366, 262], path: [[400, 250], [372, 250], [372, 210], [350, 210]] },
  { from: 'xiao', to: 'esc', kind: 'control', label: 'DShot, D8', at: [358, 84], path: [[350, 120], [376, 120], [376, 90], [400, 90]] },
  { from: 'esc', to: 'motor', kind: 'control', label: '3 phases', at: [472, 133], path: [[465, 110], [465, 150]] },
  { from: 'motor', to: 'hall', kind: 'sense', label: 'magnets', at: [472, 213], path: [[465, 190], [465, 230]], physical: true },
  { from: 'xiao', to: 'laptop', kind: 'control', label: 'Wi-Fi', at: [278, 68], path: [[270, 90], [270, 40]], both: true },
];

const flows: Record<Kind, { label: string; desc: string }> = {
  power: { label: 'Power', desc: 'The battery runs through the fuse, then splits: straight to the ESC for the motor, and through the 5 V buck for the electronics.' },
  sense: { label: 'Sensing', desc: 'Gyro, compass, sun and wheel-speed readings all end up at the ESP32. The BMP280 rides along for telemetry.' },
  control: { label: 'Control', desc: 'Every 2 ms the ESP32 sends the ESC a new throttle. The wheel changes speed, the box turns, and your laptop watches over Wi-Fi.' },
};

const IDLE = 'Hover or tap a part to see what it does.';

export const flowData = { nodes, edges };

const byId = new Map(nodes.map((node) => [node.id, node]));
const esc = (text: string) => text.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]!);
const connections = (id: string) =>
  edges
    .filter((edge) => edge.from === id || edge.to === id)
    .map((edge) => {
      const other = byId.get(edge.from === id ? edge.to : edge.from)!;
      const arrow = edge.both ? '↔' : edge.from === id ? '→' : '←';
      return `${arrow} ${other.name}${edge.label ? ` (${edge.label})` : ''}`;
    })
    .join(' · ');

const d = (path: readonly Pt[]) => path.map(([x, y], i) => `${i ? 'L' : 'M'}${x} ${y}`).join(' ');

export function flowMarkup() {
  const edgeMarkup = edges
    .map((edge, i) => {
      const cls = `flow__edge flow__edge--${edge.kind}${edge.physical ? ' flow__edge--physical' : ''}`;
      const markers = `marker-end="url(#flow-arrow)"${edge.both ? ' marker-start="url(#flow-arrow)"' : ''}`;
      return `<g class="${cls}" data-edge="${i}">
        <path class="flow__line" d="${d(edge.path)}" ${markers} />
        <path class="flow__pulse" d="${d(edge.path)}" />
        ${edge.label ? `<text class="flow__label" x="${edge.at[0]}" y="${edge.at[1]}">${esc(edge.label)}</text>` : ''}
      </g>`;
    })
    .join('');

  const nodeMarkup = nodes
    .map(
      (node) => `<g class="flow__node${node.id === 'xiao' ? ' flow__node--core' : ''}" data-node="${node.id}" tabindex="0" role="button" aria-label="${esc(`${node.name}, ${node.sub}`)}">
        <g class="flow__node-body">
          <rect x="${node.x}" y="${node.y}" width="${node.w}" height="${node.h}" rx="3" />
          <text class="flow__name" x="${node.x + 10}" y="${node.y + 17}">${esc(node.name)}</text>
          <text class="flow__sub" x="${node.x + 10}" y="${node.y + 31}">${esc(node.sub)}</text>
          ${(node.lines ?? [])
            .map((line, i) => `<text class="flow__job" x="${node.x + 10}" y="${node.y + 62 + i * 16}">${esc(line)}</text>`)
            .join('')}
        </g>
      </g>`,
    )
    .join('');

  const listMarkup = nodes
    .map(
      (node) => `<li class="flow__item"><button type="button" class="flow__item-button" data-node="${node.id}" aria-pressed="false">
        <span class="flow__item-head"><b>${esc(node.name)}</b><span>${esc(node.sub)}</span></span>
        <span class="flow__item-links">${esc(connections(node.id))}</span>
      </button></li>`,
    )
    .join('');

  const chips = (Object.keys(flows) as Kind[])
    .map((kind) => `<button type="button" class="flow__chip flow__chip--${kind}" data-flow="${kind}" aria-pressed="false"><i></i>${flows[kind].label}</button>`)
    .join('');

  return `
    <figure class="flow" data-flow-root>
      <div class="flow__chips" role="group" aria-label="Show a path">${chips}</div>
      <div class="flow__scroll">
        <svg class="flow__svg" viewBox="0 0 544 420" aria-labelledby="flow-title">
          <title id="flow-title">How the electronics connect</title>
          <defs>
            <marker id="flow-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
              <path d="M0 0.5 L7.5 4 L0 7.5" fill="none" stroke="context-stroke" stroke-width="1.4" />
            </marker>
          </defs>
          ${edgeMarkup}
          ${nodeMarkup}
        </svg>
      </div>
      <ul class="flow__list" aria-label="Parts and how they connect">${listMarkup}</ul>
      <figcaption class="flow__detail" aria-live="polite"><b data-flow-title></b><span data-flow-desc>${IDLE}</span></figcaption>
    </figure>`;
}

export interface Flow {
  clear(): void;
  readonly active: boolean;
}

// onFocus receives the hardwareParts ids to light up on the model, or null.
export function initFlow(root: HTMLElement, onFocus: (hw: string[] | null) => void): Flow {
  const svg = root.querySelector<SVGSVGElement>('.flow__svg')!;
  const title = root.querySelector<HTMLElement>('[data-flow-title]')!;
  const desc = root.querySelector<HTMLElement>('[data-flow-desc]')!;
  const list = root.querySelector<HTMLElement>('.flow__list')!;
  const nodeEls = [...root.querySelectorAll<HTMLElement>('[data-node]')];
  const edgeEls = [...root.querySelectorAll<SVGGElement>('[data-edge]')];
  const chipEls = [...root.querySelectorAll<HTMLButtonElement>('[data-flow]')];

  // What a click pinned, and what the pointer is over right now.
  let pinned: string | null = null;
  let hovered: string | null = null;

  const show = (key: string | null) => {
    const kind = key?.startsWith('flow:') ? (key.slice(5) as Kind) : null;
    const node = key && !kind ? byId.get(key)! : null;

    const onEdges = new Set<number>();
    const onNodes = new Set<string>();
    edges.forEach((edge, i) => {
      if ((kind && edge.kind === kind) || (node && (edge.from === node.id || edge.to === node.id))) {
        onEdges.add(i);
        onNodes.add(edge.from);
        onNodes.add(edge.to);
      }
    });
    if (node) onNodes.add(node.id);

    svg.classList.toggle('has-focus', !!key);
    list.classList.toggle('has-focus', !!key);
    edgeEls.forEach((el, i) => el.classList.toggle('is-on', onEdges.has(i)));
    nodeEls.forEach((el) => {
      const id = el.dataset.node!;
      el.classList.toggle('is-on', onNodes.has(id));
      el.classList.toggle('is-current', id === node?.id);
      if (el instanceof HTMLButtonElement) el.setAttribute('aria-pressed', String(id === node?.id));
    });
    chipEls.forEach((el) => el.setAttribute('aria-pressed', String(el.dataset.flow === kind)));

    if (node) {
      title.textContent = `${node.name} `;
      desc.textContent = node.desc;
    } else if (kind) {
      title.textContent = `${flows[kind].label} `;
      desc.textContent = flows[kind].desc;
    } else {
      title.textContent = '';
      desc.textContent = IDLE;
    }

    const ids = node ? [node.id] : [...onNodes];
    const hw = [...new Set(ids.map((id) => byId.get(id)?.hw).filter((id): id is string => !!id))];
    onFocus(key ? hw : null);
  };

  const refresh = () => show(hovered ?? pinned);
  const pin = (key: string) => {
    pinned = pinned === key ? null : key;
    hovered = null;
    refresh();
  };

  for (const el of nodeEls) {
    const id = el.dataset.node!;
    el.addEventListener('pointerenter', (event) => {
      if (event.pointerType !== 'mouse') return;
      hovered = id;
      refresh();
    });
    el.addEventListener('pointerleave', () => {
      if (hovered !== id) return;
      hovered = null;
      refresh();
    });
    el.addEventListener('click', () => pin(id));
    el.addEventListener('keydown', (event) => {
      if (el instanceof HTMLButtonElement) return;
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        pin(id);
      }
    });
  }
  for (const el of chipEls) el.addEventListener('click', () => pin(`flow:${el.dataset.flow}`));
  root.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && pinned) pin(pinned);
  });

  return {
    clear() {
      if (!pinned && !hovered) return;
      pinned = hovered = null;
      refresh();
    },
    get active() {
      return !!(pinned || hovered);
    },
  };
}
