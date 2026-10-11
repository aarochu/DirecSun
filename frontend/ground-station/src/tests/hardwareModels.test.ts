import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { hardwareStack, imageAssets } from '@shared/data/assets';
import { hardwareParts } from '@shared/data/hardware';
import { hardwareViews } from '@shared/data/hardwareModels';

// public/ stays in frontend/ and is shared with the landing page.
const publicFile = (src: string) => join(process.cwd(), '..', 'public', src);

function glbNodeNames(): Set<string> {
  const bytes = readFileSync(publicFile(hardwareStack.src));
  expect(bytes.toString('ascii', 0, 4)).toBe('glTF');
  const jsonLength = bytes.readUInt32LE(12);
  const json = JSON.parse(bytes.toString('utf8', 20, 20 + jsonLength)) as { nodes: { name?: string }[] };
  return new Set(json.nodes.flatMap((node) => (node.name ? [node.name] : [])));
}

describe('hardware CAD views', () => {
  it('every hardware row has a view, and every part it highlights exists in the model', () => {
    const nodes = glbNodeNames();
    expect(hardwareViews).toBeDefined();
    for (const part of hardwareParts) {
      const view = hardwareViews[part.id];
      expect(view, `no view for ${part.id}`).toBeDefined();
      expect(view!.subject.length).toBeGreaterThan(0);
      for (const name of [...view!.subject, ...view!.carrier, ...(view!.focus ?? [])]) {
        expect(nodes.has(name), `${part.id}: node ${name} is not in the model`).toBe(true);
      }
      expect((view!.focus ?? []).every((name) => view!.subject.includes(name)), part.id).toBe(true);
      expect(view!.subject.filter((name) => view!.carrier.includes(name))).toEqual([]);
    }
  });

  it('every part has its edge lines baked into the model, so the browser computes nothing', () => {
    const nodes = glbNodeNames();
    for (const name of ['esc', 'mpu6050', 'avionics_deck', 'hall_bracket', 'aa_cells']) {
      expect(nodes.has(`${name}__edges`), name).toBe(true);
    }
  });

  it('every poster the manifest lists exists, and the model stays small', () => {
    for (const asset of Object.values(imageAssets)) {
      expect(asset.available).toBe(true);
      expect(existsSync(publicFile(asset.src)), asset.src).toBe(true);
    }
    expect(readFileSync(publicFile(hardwareStack.src)).length).toBeLessThan(1_500_000);
  });
});
