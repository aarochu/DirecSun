// The hero's star field, ported from orbit-space 1.1.0 (MIT, Roman Vinnick),
// the background of the previous landing page (ground-station/src/landing/OrbitBackdrop.tsx).
// Same texture, count, spread, size and camera behaviour, minus React and gsap:
// 260 points in a 10-unit cube, a 3 s dolly in from z 20 to z 5, and the camera
// easing toward the mouse.

import {
  AdditiveBlending,
  BufferAttribute,
  BufferGeometry,
  Group,
  PerspectiveCamera,
  Points,
  PointsMaterial,
  Scene,
  SRGBColorSpace,
  TextureLoader,
  WebGLRenderer,
} from 'three';
import particleUrl from './assets/particle.png';

const COUNT = 260;
const MAX_MOUSE = 0.1;

export interface Stars {
  // 0 hides the field and stops rendering it.
  setVisibility(value: number): void;
  dispose(): void;
}

export function createStars(canvas: HTMLCanvasElement): Stars {
  const renderer = new WebGLRenderer({ canvas, antialias: false, alpha: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  const scene = new Scene();
  const camera = new PerspectiveCamera(75, 1, 0.1, 1000);
  camera.position.z = 20;

  const positions = new Float32Array(COUNT * 3);
  for (let i = 0; i < positions.length; i++) positions[i] = (Math.random() - 0.5) * 10;
  const geometry = new BufferGeometry();
  geometry.setAttribute('position', new BufferAttribute(positions, 3));
  const texture = new TextureLoader().load(particleUrl);
  texture.colorSpace = SRGBColorSpace;
  const material = new PointsMaterial({
    color: 0xffffff,
    size: 0.055,
    map: texture,
    transparent: true,
    blending: AdditiveBlending,
    depthWrite: false,
  });
  const group = new Group();
  group.add(new Points(geometry, material));
  group.rotation.set(-0.5, -0.5, 0);
  scene.add(group);

  let mouseX = 0;
  let mouseY = 0;
  const onMove = (event: PointerEvent) => {
    const clamp = (v: number) => Math.min(MAX_MOUSE, Math.max(-MAX_MOUSE, v));
    mouseX = clamp((event.clientX - window.innerWidth / 2) / (window.innerWidth / 2));
    mouseY = clamp((event.clientY - window.innerHeight / 2) / (window.innerHeight / 2));
  };

  const resize = () => {
    const width = window.innerWidth;
    const height = window.innerHeight;
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    renderer.setSize(width, height, false);
  };
  resize();

  let visible = 1;
  let raf = 0;
  const start = performance.now();
  // The original's gsap tween: power1.inOut over 3 s.
  const dolly = (t: number) => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2);

  const tick = (now: number) => {
    raf = requestAnimationFrame(tick);
    if (visible <= 0) return;
    const t = Math.min(1, (now - start) / 3000);
    camera.position.z = 20 + (5 - 20) * dolly(t);
    camera.position.x += (mouseX * 5 - camera.position.x) * 0.05;
    camera.position.y += (-mouseY * 5 - camera.position.y) * 0.05;
    camera.lookAt(scene.position);
    renderer.render(scene, camera);
  };
  raf = requestAnimationFrame(tick);

  window.addEventListener('pointermove', onMove);
  window.addEventListener('resize', resize);

  return {
    setVisibility(value) {
      visible = value;
      canvas.style.opacity = String(value);
    },
    dispose() {
      cancelAnimationFrame(raf);
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('resize', resize);
      geometry.dispose();
      material.dispose();
      texture.dispose();
      renderer.dispose();
    },
  };
}
