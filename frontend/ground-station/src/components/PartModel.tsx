import { Layers, RotateCcw } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import {
  Box3,
  BufferGeometry,
  DirectionalLight,
  Group,
  HemisphereLight,
  LineBasicMaterial,
  LineSegments,
  Matrix4,
  Mesh,
  MeshStandardMaterial,
  PerspectiveCamera,
  Scene,
  Sphere,
  Vector3,
  WebGLRenderer,
} from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { MeshoptDecoder } from 'three/examples/jsm/libs/meshopt_decoder.module.js';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { hardwareStack } from '@shared/data/assets';
import type { HardwareView } from '@shared/data/hardwareModels';

const DEG = Math.PI / 180;
const FOV_DEG = 30;
const KEY_STEP_RAD = 10 * DEG;
const EDGE_SUFFIX = '__edges';
const SETTLE_MS = 140;

interface StackPart {
  readonly name: string;
  readonly geometry: BufferGeometry;
  readonly matrix: Matrix4;
  readonly edges: BufferGeometry | null;
  readonly edgesMatrix: Matrix4;
}

let stackPromise: Promise<readonly StackPart[]> | null = null;

function loadStack(): Promise<readonly StackPart[]> {
  stackPromise ??= new GLTFLoader()
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
      return [...meshes].map(([name, mesh]) => ({
        name,
        geometry: mesh.geometry,
        matrix: mesh.matrixWorld.clone(),
        edges: lines.get(name)?.geometry ?? null,
        edgesMatrix: lines.get(name)?.matrixWorld.clone() ?? new Matrix4(),
      }));
    });
  stackPromise.catch(() => {
    stackPromise = null;
  });
  return stackPromise;
}

export function preloadStack(): void {
  loadStack().catch(() => {});
}

const place = <T extends Mesh | LineSegments>(object: T, matrix: Matrix4): T => {
  object.matrixAutoUpdate = false;
  object.matrix.copy(matrix);
  object.matrixWorldNeedsUpdate = true;
  return object;
};

let sharedRenderer: WebGLRenderer | null = null;
const lostListeners = new Set<() => void>();

function acquireRenderer(): WebGLRenderer | null {
  if (sharedRenderer) return sharedRenderer;
  let created: WebGLRenderer;
  try {
    created = new WebGLRenderer({ antialias: true, alpha: true, powerPreference: 'high-performance' });
  } catch {
    return null;
  }
  created.setPixelRatio(1);
  created.debug.checkShaderErrors = import.meta.env.DEV;
  created.domElement.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    if (sharedRenderer === created) sharedRenderer = null;
    created.dispose();
    for (const listener of [...lostListeners]) listener();
  });
  sharedRenderer = created;
  return created;
}

type Role = 'subject' | 'carrier' | 'context';

const SURFACE: Record<Role, { color: string; roughness: number; opacity: number }> = {
  subject: { color: '#f4f4f4', roughness: 0.5, opacity: 1 },
  carrier: { color: '#8f8f8f', roughness: 0.65, opacity: 1 },
  context: { color: '#7a7a7a', roughness: 0.8, opacity: 0.14 },
};

const EDGE: Record<Role, { color: string; opacity: number }> = {
  subject: { color: '#e7a866', opacity: 0.9 },
  carrier: { color: '#1a1a1a', opacity: 0.55 },
  context: { color: '#9a9a9a', opacity: 0.14 },
};

interface PartModelProps {
  view: HardwareView;
  label: string;
  onReady: () => void;
  onError: () => void;
}

export default function PartModel({ view, label, onReady, onError }: PartModelProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const resetRef = useRef<() => void>(() => {});
  const contextRef = useRef<Group | null>(null);
  const renderRef = useRef<() => void>(() => {});
  const callbacksRef = useRef({ onReady, onError });
  callbacksRef.current = { onReady, onError };
  const rootRef = useRef<HTMLDivElement>(null);
  const [state, setState] = useState<'loading' | 'ready'>('loading');
  const [partOnly, setPartOnly] = useState(false);

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;
    const canvas = document.createElement('canvas');
    const surface = canvas.getContext('2d');
    if (!surface || !acquireRenderer()) {
      callbacksRef.current.onError();
      return;
    }
    let disposed = false;
    const sharpRatio = Math.min(window.devicePixelRatio, 2);
    let ratio = sharpRatio;
    let frame = 0;
    let settleTimer = 0;
    canvas.setAttribute('aria-hidden', 'true');
    host.appendChild(canvas);
    const onLost = () => callbacksRef.current.onError();
    lostListeners.add(onLost);

    const scene = new Scene();
    const camera = new PerspectiveCamera(FOV_DEG, 1, 0.001, 10);
    const key = new DirectionalLight(0xffffff, 1.5);
    key.position.set(-1.2, 2, 1.6);
    const fill = new DirectionalLight(0xbcc8d8, 0.55);
    fill.position.set(1.6, -0.6, -1.2);
    scene.add(new HemisphereLight(0xe4eaf2, 0x555a62, 1.1), key, fill);

    const controls = new OrbitControls(camera, canvas);
    controls.enablePan = false;
    controls.enableDamping = false;
    controls.rotateSpeed = 0.9;
    canvas.style.touchAction = 'pan-y';

    const materials: (MeshStandardMaterial | LineBasicMaterial)[] = [];
    const home = { position: new Vector3(), target: new Vector3() };

    const draw = () => {
      frame = 0;
      const gl = sharedRenderer;
      if (disposed || !gl) return;
      const width = Math.max(1, Math.round(host.clientWidth * ratio));
      const height = Math.max(1, Math.round(host.clientHeight * ratio));
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      if (gl.domElement.width !== width || gl.domElement.height !== height) gl.setSize(width, height, false);
      gl.render(scene, camera);
      surface.clearRect(0, 0, width, height);
      surface.drawImage(gl.domElement, 0, 0);
    };
    const render = () => {
      if (!frame && !disposed) frame = requestAnimationFrame(draw);
    };
    renderRef.current = render;

    const placeCamera = (azimuthRad: number, elevationRad: number, distance: number, target: Vector3) => {
      camera.position.set(
        target.x + distance * Math.sin(azimuthRad) * Math.cos(elevationRad),
        target.y + distance * Math.sin(elevationRad),
        target.z + distance * Math.cos(azimuthRad) * Math.cos(elevationRad),
      );
      controls.target.copy(target);
      camera.lookAt(target);
      controls.update();
    };

    const resize = () => {
      const { clientWidth: width, clientHeight: height } = host;
      if (!width || !height) return;
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      render();
    };
    const observer = new ResizeObserver(resize);
    observer.observe(host);

    const offset = new Vector3();
    const reportAzimuth = () => {
      offset.copy(camera.position).sub(controls.target);
      rootRef.current?.setAttribute('data-azimuth', String(Math.round((Math.atan2(offset.x, offset.z) / DEG + 360) % 360)));
    };
    controls.addEventListener('change', () => {
      render();
      reportAzimuth();
    });
    controls.addEventListener('start', () => {
      window.clearTimeout(settleTimer);
      ratio = 1;
    });
    controls.addEventListener('end', () => {
      window.clearTimeout(settleTimer);
      settleTimer = window.setTimeout(() => {
        if (disposed) return;
        ratio = sharpRatio;
        render();
      }, SETTLE_MS);
    });

    const onWheel = (event: WheelEvent) => {
      if (!event.ctrlKey && !event.metaKey) event.stopPropagation();
    };
    host.addEventListener('wheel', onWheel, { capture: true });

    const onKey = (event: KeyboardEvent) => {
      const offset = camera.position.clone().sub(controls.target);
      const distance = offset.length();
      let azimuthRad = Math.atan2(offset.x, offset.z);
      let elevationRad = Math.asin(offset.y / distance);
      let zoom = 1;
      switch (event.key) {
        case 'ArrowLeft':
          azimuthRad -= KEY_STEP_RAD;
          break;
        case 'ArrowRight':
          azimuthRad += KEY_STEP_RAD;
          break;
        case 'ArrowUp':
          elevationRad = Math.min(1.45, elevationRad + KEY_STEP_RAD);
          break;
        case 'ArrowDown':
          elevationRad = Math.max(-1.45, elevationRad - KEY_STEP_RAD);
          break;
        case '+':
        case '=':
          zoom = 0.85;
          break;
        case '-':
          zoom = 1.18;
          break;
        default:
          return;
      }
      event.preventDefault();
      const next = Math.min(controls.maxDistance, Math.max(controls.minDistance, distance * zoom));
      placeCamera(azimuthRad, elevationRad, next, controls.target.clone());
      render();
      reportAzimuth();
    };
    host.addEventListener('keydown', onKey);

    loadStack().then(
      (parts) => {
        if (disposed) return;
        const subject = new Set(view.subject);
        const carrier = new Set(view.carrier);
        const solid = new Group();
        const context = new Group();
        const focusBox = new Box3();
        const stackBox = new Box3();
        for (const part of parts) {
          const role: Role = subject.has(part.name) ? 'subject' : carrier.has(part.name) ? 'carrier' : 'context';
          const surface = SURFACE[role];
          const material = new MeshStandardMaterial({
            color: surface.color,
            roughness: surface.roughness,
            metalness: 0,
            transparent: surface.opacity < 1,
            opacity: surface.opacity,
            depthWrite: surface.opacity === 1,
          });
          const edge = role === 'subject' && view.frame === 'stack' ? EDGE.carrier : EDGE[role];
          const lineMaterial = new LineBasicMaterial({ color: edge.color, transparent: true, opacity: edge.opacity });
          materials.push(material, lineMaterial);
          const target = role === 'context' ? context : solid;
          target.add(place(new Mesh(part.geometry, material), part.matrix));
          if (part.edges) target.add(place(new LineSegments(part.edges, lineMaterial), part.edgesMatrix));
          if (!part.geometry.boundingBox) part.geometry.computeBoundingBox();
          const bounds = part.geometry.boundingBox!.clone().applyMatrix4(part.matrix);
          stackBox.union(bounds);
          if (role === 'subject') focusBox.union(bounds);
        }
        if (focusBox.isEmpty()) focusBox.copy(stackBox);
        scene.add(solid, context);
        contextRef.current = context;

        const focusSphere = focusBox.getBoundingSphere(new Sphere());
        const stackSphere = stackBox.getBoundingSphere(new Sphere());
        const base = view.frame === 'stack' ? stackSphere : focusSphere;
        const distance = (base.radius / Math.sin((FOV_DEG * DEG) / 2)) * (view.frame === 'stack' ? 1.05 : view.zoom);
        home.target.copy(base.center);
        controls.minDistance = Math.min(distance, focusSphere.radius * 1.4);
        controls.maxDistance = stackSphere.radius * 6;
        camera.near = controls.minDistance / 20;
        camera.far = controls.maxDistance * 4;
        camera.updateProjectionMatrix();
        placeCamera(view.azimuthDeg * DEG, view.elevationDeg * DEG, distance, home.target);
        home.position.copy(camera.position);
        resetRef.current = () => {
          camera.position.copy(home.position);
          controls.target.copy(home.target);
          camera.lookAt(home.target);
          controls.update();
          render();
          reportAzimuth();
        };
        resize();
        setState('ready');
        callbacksRef.current.onReady();
      },
      () => {
        if (!disposed) callbacksRef.current.onError();
      },
    );

    return () => {
      disposed = true;
      cancelAnimationFrame(frame);
      window.clearTimeout(settleTimer);
      renderRef.current = () => {};
      resetRef.current = () => {};
      contextRef.current = null;
      lostListeners.delete(onLost);
      host.removeEventListener('wheel', onWheel, { capture: true });
      host.removeEventListener('keydown', onKey);
      observer.disconnect();
      controls.dispose();
      for (const material of materials) material.dispose();
      canvas.remove();
    };
  }, [view]);

  useEffect(() => {
    if (contextRef.current) contextRef.current.visible = !partOnly;
    renderRef.current();
  }, [partOnly, state]);

  return (
    <div className="part-view" ref={rootRef} data-view-state={state} data-azimuth={Math.round(view.azimuthDeg)}>
      <div
        className="part-view__stage"
        ref={hostRef}
        tabIndex={0}
        role="group"
        aria-label={`3D view of ${label}. Arrow keys rotate, plus and minus zoom.`}
      />
      {state === 'ready' && (
        <>
          <div className="part-view__actions">
            <button type="button" className="part-view__button" aria-label="Reset the 3D view" onClick={() => resetRef.current()}>
              <RotateCcw strokeWidth={1.75} aria-hidden />
            </button>
            <button
              type="button"
              className="part-view__button"
              aria-label="Show only this part and its carrier"
              aria-pressed={partOnly}
              onClick={() => setPartOnly((value) => !value)}
            >
              <Layers strokeWidth={1.75} aria-hidden />
            </button>
          </div>
          <p className="part-view__hint mono-label" aria-hidden="true">
            <span className="part-view__hint-fine">DRAG TO ROTATE / CTRL + SCROLL TO ZOOM</span>
            <span className="part-view__hint-coarse">DRAG TO ROTATE / PINCH TO ZOOM</span>
          </p>
        </>
      )}
    </div>
  );
}
