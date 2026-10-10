import {
  Box3,
  Color,
  DirectionalLight,
  Group,
  HemisphereLight,
  LineBasicMaterial,
  LineSegments,
  Mesh,
  MeshStandardMaterial,
  PerspectiveCamera,
  Scene,
  Sphere,
  Vector3,
  WebGLRenderer,
} from 'three';
import { MeshoptDecoder } from 'three/examples/jsm/libs/meshopt_decoder.module.js';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { cubesatModel, hardwareStack } from './content';

const DEG = Math.PI / 180;
export const FOV_DEG = 30;
const EDGE_SUFFIX = '__edges';

// How far the stack spreads along the spin axis at explode = 1: each part moves
// away from the stack centre by this fraction of its height above or below it.
const EXPLODE_SPREAD = 0.95;
// Parts that are not stacked on a deck slide outward instead.
const RADIAL_OUT: Record<string, number> = { rod_0: 0.03, rod_1: 0.03, rod_2: 0.03, rod_3: 0.03, lock_pin: 0.05 };
const WHEEL_PARTS = new Set(['washer_flywheel', 'shaft_and_nuts']);

// Tweened by the scroll timeline. Angles in degrees, distances in metres.
export const view = {
  az: 38,
  el: 20,
  dist: 0,
  distScale: 1,
  tx: 0,
  ty: 0,
  tz: 0,
  shiftX: 0,
  shiftY: 0,
  yaw: 0,
  explode: 0,
  wheel: 0,
  idle: 1,
  light: 0,
};

// Per-part look, also tweened by the timeline. o: opacity, hl: highlight.
export interface PartState {
  readonly name: string;
  o: number;
  hl: number;
}

interface PartNode {
  readonly state: PartState;
  readonly group: Group;
  readonly material: MeshStandardMaterial;
  readonly edge: LineBasicMaterial;
  readonly box: Box3;
  readonly offset: Vector3;
}

const SURFACE_BASE = new Color('#9c9c9c');
const SURFACE_HL = new Color('#f4f4f4');
const EDGE_BASE = new Color('#1a1a1a');
const EDGE_HL = new Color('#e7a866');

export interface Framing {
  tx: number;
  ty: number;
  tz: number;
  dist: number;
}

export interface CubeSatScene {
  readonly parts: readonly PartState[];
  readonly ready: Promise<void>;
  frame(names: readonly string[] | 'stack', zoom: number, exploded: boolean): Framing;
  dispose(): void;
}

export function createScene(canvas: HTMLCanvasElement, reducedMotion: boolean): CubeSatScene {
  const renderer = new WebGLRenderer({ canvas, antialias: true, alpha: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));

  const scene = new Scene();
  const camera = new PerspectiveCamera(FOV_DEG, 1, 0.01, 10);
  const key = new DirectionalLight(0xffffff, 1.5);
  key.position.set(-1.2, 2, 1.6);
  const fill = new DirectionalLight(0xbcc8d8, 0.55);
  fill.position.set(1.6, -0.6, -1.2);
  scene.add(new HemisphereLight(0xe4eaf2, 0x555a62, 1.1), key, fill);

  // The box turns on its bearings; the base hub and bolt stay put.
  const body = new Group();
  const fixed = new Group();
  scene.add(body, fixed);

  const nodes: PartNode[] = [];
  const byName = new Map<string, PartNode>();
  const stackBox = new Box3();
  let stackCentreY = 0;

  const ready = new GLTFLoader()
    .setMeshoptDecoder(MeshoptDecoder)
    .loadAsync(hardwareStack.src)
    .then((gltf) => {
      gltf.scene.updateMatrixWorld(true);
      const meshes = new Map<string, Mesh>();
      const lines = new Map<string, LineSegments>();
      gltf.scene.traverse((object) => {
        if (object instanceof LineSegments) lines.set(object.name.slice(0, -EDGE_SUFFIX.length), object);
        else if (object instanceof Mesh) meshes.set(object.name, object);
      });

      for (const [name, mesh] of meshes) {
        const material = new MeshStandardMaterial({ color: SURFACE_BASE, roughness: 0.6, metalness: 0, transparent: true });
        const edge = new LineBasicMaterial({ color: EDGE_BASE, transparent: true, opacity: 0.55 });
        const group = new Group();
        const solid = new Mesh(mesh.geometry, material);
        solid.matrixAutoUpdate = false;
        solid.matrix.copy(mesh.matrixWorld);
        group.add(solid);
        const line = lines.get(name);
        if (line) {
          const outline = new LineSegments(line.geometry, edge);
          outline.matrixAutoUpdate = false;
          outline.matrix.copy(line.matrixWorld);
          group.add(outline);
        }
        if (!mesh.geometry.boundingBox) mesh.geometry.computeBoundingBox();
        const box = mesh.geometry.boundingBox!.clone().applyMatrix4(mesh.matrixWorld);
        stackBox.union(box);
        (cubesatModel.fixedNodePattern.test(name) ? fixed : body).add(group);
        const node: PartNode = { state: { name, o: 1, hl: 0 }, group, material, edge, box, offset: new Vector3() };
        nodes.push(node);
        byName.set(name, node);
        parts.push(node.state);
      }

      stackCentreY = stackBox.getCenter(new Vector3()).y;
      for (const node of nodes) {
        const centre = node.box.getCenter(new Vector3());
        const radial = RADIAL_OUT[node.state.name];
        if (radial !== undefined) {
          node.offset.set(centre.x, 0, centre.z).normalize().multiplyScalar(radial);
        } else {
          node.offset.set(0, (centre.y - stackCentreY) * EXPLODE_SPREAD, 0);
        }
      }
      resize();
    });

  const parts: PartState[] = [];

  // Camera framing, as in src/components/PartModel.tsx: fit the bounding sphere
  // of the subject (or the whole stack) in the 30 degree field of view.
  const frame = (names: readonly string[] | 'stack', zoom: number, exploded: boolean): Framing => {
    const box = new Box3();
    const list = names === 'stack' ? nodes : names.map((name) => byName.get(name)).filter((n): n is PartNode => !!n);
    for (const node of list) {
      const shifted = node.box.clone();
      if (exploded) shifted.translate(node.offset);
      box.union(shifted);
    }
    if (box.isEmpty()) box.copy(stackBox);
    const sphere = box.getBoundingSphere(new Sphere());
    const dist = (sphere.radius / Math.sin((FOV_DEG * DEG) / 2)) * zoom;
    return { tx: sphere.center.x, ty: sphere.center.y, tz: sphere.center.z, dist };
  };

  let width = 0;
  let height = 0;
  const resize = () => {
    width = canvas.clientWidth;
    height = canvas.clientHeight;
    if (!width || !height) return;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  };
  const observer = new ResizeObserver(resize);
  observer.observe(canvas);

  const surface = new Color();
  const edgeColor = new Color();
  const target = new Vector3();
  let raf = 0;
  let last = performance.now();
  let wheelSpin = 0;

  const tick = (now: number) => {
    raf = requestAnimationFrame(tick);
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    if (!nodes.length || !width) return;

    // A slow sway while the hero is up; it fades to zero so scroll alone
    // decides the heading everywhere else.
    const sway = reducedMotion ? 0 : Math.sin(now / 1600) * 14 * view.idle;
    if (!reducedMotion) wheelSpin += dt * 9 * view.wheel;

    body.rotation.y = (view.yaw + sway) * DEG;
    for (const node of nodes) {
      const { o, hl } = node.state;
      node.group.position.copy(node.offset).multiplyScalar(view.explode);
      if (WHEEL_PARTS.has(node.state.name)) node.group.rotation.y = -wheelSpin;
      node.group.visible = o > 0.01;
      surface.copy(SURFACE_BASE).lerp(SURFACE_HL, hl);
      node.material.color.copy(surface);
      node.material.opacity = o;
      node.material.depthWrite = o > 0.98;
      edgeColor.copy(EDGE_BASE).lerp(EDGE_HL, hl);
      node.edge.color.copy(edgeColor);
      node.edge.opacity = (0.55 + 0.35 * hl) * o;
    }

    const az = view.az * DEG;
    const el = view.el * DEG;
    const dist = view.dist * view.distScale;
    target.set(view.tx, view.ty, view.tz);
    camera.position.set(
      target.x + dist * Math.sin(az) * Math.cos(el),
      target.y + dist * Math.sin(el),
      target.z + dist * Math.cos(az) * Math.cos(el),
    );
    camera.lookAt(target);
    camera.near = dist / 40;
    camera.far = dist * 8;
    // Slide the model sideways on screen so it sits beside the copy.
    camera.setViewOffset(width, height, -view.shiftX * width, -view.shiftY * height, width, height);
    camera.updateProjectionMatrix();
    renderer.render(scene, camera);
  };
  raf = requestAnimationFrame(tick);

  return {
    parts,
    ready,
    frame,
    dispose() {
      cancelAnimationFrame(raf);
      observer.disconnect();
      for (const node of nodes) {
        node.material.dispose();
        node.edge.dispose();
      }
      renderer.dispose();
    },
  };
}
