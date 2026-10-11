import { describe, expect, it } from 'vitest';
import { hardwareParts } from '@shared/data/hardware';
import { hardwareViews } from '@shared/data/hardwareModels';
import { flowData, flowMarkup } from './flow';

const { nodes, edges } = flowData;
const byId = new Map(nodes.map((node) => [node.id, node]));

const onBoundary = (node: (typeof nodes)[number], [x, y]: readonly [number, number]) => {
  const inside = x >= node.x && x <= node.x + node.w && y >= node.y && y <= node.y + node.h;
  const onEdge = x === node.x || x === node.x + node.w || y === node.y || y === node.y + node.h;
  return inside && onEdge;
};

describe('flowchart data', () => {
  it('has unique node ids and every edge connects two existing nodes', () => {
    expect(new Set(nodes.map((node) => node.id)).size).toBe(nodes.length);
    for (const edge of edges) {
      expect(byId.has(edge.from), `${edge.from} -> ${edge.to}`).toBe(true);
      expect(byId.has(edge.to), `${edge.from} -> ${edge.to}`).toBe(true);
      expect(edge.from).not.toBe(edge.to);
    }
  });

  it('draws every edge from the border of its source to the border of its target', () => {
    for (const edge of edges) {
      const first = edge.path[0]!;
      const last = edge.path[edge.path.length - 1]!;
      expect(onBoundary(byId.get(edge.from)!, first), `${edge.from} -> ${edge.to} starts off its box`).toBe(true);
      expect(onBoundary(byId.get(edge.to)!, last), `${edge.from} -> ${edge.to} ends off its box`).toBe(true);
    }
  });

  it('uses axis-aligned wires only, so they read as a schematic', () => {
    for (const edge of edges) {
      edge.path.slice(1).forEach(([x, y], index) => {
        const [px, py] = edge.path[index]!;
        expect(x === px || y === py, `${edge.from} -> ${edge.to}`).toBe(true);
      });
    }
  });

  it('keeps every node inside the viewBox and no two boxes overlapping', () => {
    for (const node of nodes) {
      expect(node.x).toBeGreaterThanOrEqual(0);
      expect(node.y).toBeGreaterThanOrEqual(0);
      expect(node.x + node.w).toBeLessThanOrEqual(544);
      expect(node.y + node.h).toBeLessThanOrEqual(420);
    }
    for (const [i, a] of nodes.entries()) {
      for (const b of nodes.slice(i + 1)) {
        const apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
        expect(apart, `${a.id} overlaps ${b.id}`).toBe(true);
      }
    }
  });

  it('points every part that is on the box at a real hardware row with a 3D view', () => {
    const rows = new Set(hardwareParts.map((part) => part.id));
    for (const node of nodes) {
      if (node.hw === null) continue;
      expect(rows.has(node.hw), `${node.id} -> ${node.hw}`).toBe(true);
      expect(hardwareViews[node.hw], `${node.id} -> ${node.hw}`).toBeDefined();
    }
  });

  it('writes the same connections into the list version that the chart draws', () => {
    const markup = flowMarkup();
    expect(markup.match(/class="flow__item"/g)).toHaveLength(nodes.length);
    expect(markup).toContain('→ XIAO ESP32-S3 (I²C)');
    expect(markup).toContain('↔ Ground station (Wi-Fi)');
  });
});
