import { useEffect, useRef } from 'react';
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
  Texture,
  Vector3,
  WebGLRenderer,
  type Material,
  type Object3D,
} from 'three';
import { MeshoptDecoder } from 'three/examples/jsm/libs/meshopt_decoder.module.js';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import type { ModelAsset } from '@shared/data/assets';
import { VIEW_CAMERA_BEARING_DEG, VIEW_ELEVATION_DEG } from './CubeSatSchematic';

const DEG = Math.PI / 180;

interface RealCubeSatModelProps {
  asset: ModelAsset;
  headingDeg: number;
  onLoad: () => void;
  onError: (error: unknown) => void;
}

export default function RealCubeSatModel({ asset, headingDeg, onLoad, onError }: RealCubeSatModelProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const headingRef = useRef(headingDeg);
  const applyHeadingRef = useRef<(deg: number) => void>(() => {});
  const callbacksRef = useRef({ onLoad, onError });
  headingRef.current = headingDeg;
  callbacksRef.current = { onLoad, onError };

  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;

    let renderer: WebGLRenderer;
    try {
      renderer = new WebGLRenderer({ antialias: true, alpha: true });
    } catch (error) {
      callbacksRef.current.onError(error);
      return;
    }
    let disposed = false;
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.5));
    renderer.domElement.setAttribute('aria-hidden', 'true');
    host.appendChild(renderer.domElement);

    const scene = new Scene();
    const camera = new PerspectiveCamera(28, 1, 0.001, 100);
    const keyLight = new DirectionalLight(0xffffff, 1.6);
    keyLight.position.set(-1, 2, 1.5);
    scene.add(new HemisphereLight(0xdfe6ee, 0x000000, 1.1), keyLight);

    const fixedGroup = new Group();
    const bodyGroup = new Group();
    scene.add(fixedGroup, bodyGroup);

    const render = () => {
      if (!disposed) renderer.render(scene, camera);
    };
    const resize = () => {
      const { clientWidth: width, clientHeight: height } = host;
      if (!width || !height) return;
      renderer.setSize(width, height, false);
      camera.aspect = width / height;
      camera.updateProjectionMatrix();
      render();
    };
    const observer = new ResizeObserver(resize);
    observer.observe(host);

    applyHeadingRef.current = (deg) => {
      bodyGroup.rotation.y = -(deg + asset.yawOffsetDeg) * DEG;
      render();
    };

    new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).load(
      asset.src,
      (gltf) => {
        if (disposed) {
          disposeObject(gltf.scene);
          return;
        }
        applyCadLook(gltf.scene);
        for (const child of [...gltf.scene.children]) {
          (asset.fixedNodePattern.test(child.name) ? fixedGroup : bodyGroup).add(child);
        }
        frameCamera(camera, scene);
        applyHeadingRef.current(headingRef.current);
        resize();
        callbacksRef.current.onLoad();
      },
      undefined,
      (error) => {
        if (!disposed) callbacksRef.current.onError(error);
      },
    );

    return () => {
      disposed = true;
      applyHeadingRef.current = () => {};
      observer.disconnect();
      disposeObject(scene);
      renderer.dispose();
      renderer.forceContextLoss();
      renderer.domElement.remove();
    };
  }, [asset]);

  useEffect(() => {
    applyHeadingRef.current(headingDeg);
  }, [headingDeg]);

  return <div ref={hostRef} className="cad-model" />;
}

// The landing page's CAD look: flat grey solids with dark outlines. The lock
// pin is only in place during spin-up, so it is hidden.
const SURFACE = new Color('#9c9c9c');
const EDGE = new Color('#1a1a1a');

function applyCadLook(root: Object3D) {
  const surface = new MeshStandardMaterial({ color: SURFACE, roughness: 0.6, metalness: 0 });
  const edge = new LineBasicMaterial({ color: EDGE, transparent: true, opacity: 0.55 });
  root.traverse((object) => {
    if (/^lock_pin/.test(object.name)) object.visible = false;
    if (object instanceof LineSegments) object.material = edge;
    else if (object instanceof Mesh) object.material = surface;
  });
}

function frameCamera(camera: PerspectiveCamera, scene: Scene) {
  const box = new Box3().setFromObject(scene);
  const center = box.getCenter(new Vector3());
  const radius = box.getSize(new Vector3()).length() / 2;
  const distance = (radius / Math.sin((camera.fov * DEG) / 2)) * 1.08;
  const bearing = VIEW_CAMERA_BEARING_DEG * DEG;
  const elevation = VIEW_ELEVATION_DEG * DEG;
  const direction = new Vector3(
    Math.sin(bearing) * Math.cos(elevation),
    Math.sin(elevation),
    -Math.cos(bearing) * Math.cos(elevation),
  );
  camera.position.copy(center).addScaledVector(direction, distance);
  camera.near = distance / 100;
  camera.far = distance * 10;
  camera.updateProjectionMatrix();
  camera.lookAt(center);
}

function disposeObject(root: Object3D) {
  root.traverse((object) => {
    if (!(object instanceof Mesh)) return;
    object.geometry.dispose();
    const materials: Material[] = Array.isArray(object.material) ? object.material : [object.material];
    for (const material of materials) {
      for (const value of Object.values(material)) {
        if (value instanceof Texture) value.dispose();
      }
      material.dispose();
    }
  });
}
