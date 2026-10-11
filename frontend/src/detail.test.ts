import { describe, expect, it } from 'vitest';
import { Box3, Vector3 } from 'three';
import { hardwareViews } from '@shared/data/hardwareModels';
import { detailFor } from '@shared/geometry/detail';

const sampleBox = new Box3(new Vector3(0, 0, 0), new Vector3(0.05, 0.03, 0.05));

describe('illustrative hardware detail', () => {
  it('covers the highlighted hardware in every electronics close-up', () => {
    for (const id of ['esp32s3', 'mpu6050', 'qmc5883p', 'op550', 'a1104', 'reaction-wheel', 'esc', 'power', 'bmp280']) {
      const subject = hardwareViews[id]!.subject;
      expect(subject.some((name) => detailFor(name, sampleBox)?.items.length), id).toBe(true);
    }
  });
});
