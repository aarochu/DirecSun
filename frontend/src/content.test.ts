import { describe, expect, it } from 'vitest';
import { hardwareParts } from '@shared/data/hardware';
import { componentBullets } from './content';

describe('landing component copy', () => {
  it('gives every hardware component exactly three short, distinct bullets', () => {
    expect(Object.keys(componentBullets).sort()).toEqual(hardwareParts.map((part) => part.id).sort());
    for (const part of hardwareParts) {
      const bullets = componentBullets[part.id];
      expect(bullets, part.id).toHaveLength(3);
      expect(new Set(bullets).size, part.id).toBe(3);
      for (const bullet of bullets) {
        expect(bullet.trim(), part.id).toBe(bullet);
        expect(bullet.length, part.id).toBeLessThanOrEqual(100);
      }
    }
  });
});
