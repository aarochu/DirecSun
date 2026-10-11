import {
  Color,
  EdgesGeometry,
  Group,
  InstancedMesh,
  LineSegments,
  Mesh,
  MeshStandardMaterial,
  type Box3,
  type BufferGeometry,
  type LineBasicMaterial,
} from 'three';
import { detailFor, TONES, type Tone } from './detail';

export interface RenderedDetail {
  readonly group: Group;
  readonly replace: boolean;
  readonly materials: readonly { material: MeshStandardMaterial; base: Color }[];
  readonly geometries: readonly BufferGeometry[];
  readonly count: number;
}

// Detail meshes use the CAD model's world coordinates, so callers can attach
// this group beside the corresponding CAD mesh without changing its transform.
export function renderDetail(name: string, box: Box3, edge: LineBasicMaterial): RenderedDetail | null {
  const detail = detailFor(name, box);
  if (!detail) return null;

  const group = new Group();
  group.name = `${name}__illustrative_detail`;
  group.userData.detail = true;
  const byTone = new Map<Tone, MeshStandardMaterial>();
  const geometries = new Set<BufferGeometry>();

  for (const item of detail.items) {
    geometries.add(item.geometry);
    let material = byTone.get(item.tone);
    if (!material) {
      material = new MeshStandardMaterial({ color: TONES[item.tone], roughness: 0.55, metalness: 0, transparent: true });
      byTone.set(item.tone, material);
    }
    if (item.matrices.length > 1) {
      const mesh = new InstancedMesh(item.geometry, material, item.matrices.length);
      item.matrices.forEach((matrix, index) => mesh.setMatrixAt(index, matrix));
      group.add(mesh);
      continue;
    }
    const mesh = new Mesh(item.geometry, material);
    mesh.matrixAutoUpdate = false;
    mesh.matrix.copy(item.matrices[0]!);
    group.add(mesh);
    if (item.edges) {
      const edges = new EdgesGeometry(item.geometry, 30);
      geometries.add(edges);
      const outline = new LineSegments(edges, edge);
      outline.matrixAutoUpdate = false;
      outline.matrix.copy(item.matrices[0]!);
      group.add(outline);
    }
  }

  return {
    group,
    replace: detail.replace,
    materials: [...byTone.values()].map((material) => ({ material, base: material.color.clone() })),
    geometries: [...geometries],
    count: detail.items.length,
  };
}
