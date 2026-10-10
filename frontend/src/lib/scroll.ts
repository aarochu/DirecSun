import { prefersReducedMotion } from './environment';

const SCROLL_DURATION_MS = 450;
const easeInOutCubic = (t: number) => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2);

let activeFrame: number | null = null;
let removeInterruptListeners: (() => void) | null = null;

function cancelActiveScroll() {
  if (activeFrame !== null) cancelAnimationFrame(activeFrame);
  activeFrame = null;
  removeInterruptListeners?.();
  removeInterruptListeners = null;
}

export function scrollToY(targetY: number, smooth = true): void {
  cancelActiveScroll();
  const maxY = document.documentElement.scrollHeight - window.innerHeight;
  const destination = Math.max(0, Math.min(targetY, maxY));
  if (!smooth || prefersReducedMotion()) {
    window.scrollTo(0, destination);
    return;
  }

  const startY = window.scrollY;
  const distance = destination - startY;
  const startMs = performance.now();
  const interrupt = () => cancelActiveScroll();
  const events = ['wheel', 'touchstart', 'keydown'] as const;
  for (const name of events) window.addEventListener(name, interrupt, { passive: true });
  removeInterruptListeners = () => {
    for (const name of events) window.removeEventListener(name, interrupt);
  };

  const frame = (now: number) => {
    const progress = Math.min(1, (now - startMs) / SCROLL_DURATION_MS);
    window.scrollTo(0, startY + distance * easeInOutCubic(progress));
    if (progress < 1) activeFrame = requestAnimationFrame(frame);
    else cancelActiveScroll();
  };
  activeFrame = requestAnimationFrame(frame);
}

export function scrollToSection(id: string, smooth = true): boolean {
  const element = document.getElementById(id);
  if (!element) return false;
  const header = document.querySelector<HTMLElement>('[data-site-header]');
  const offset = header?.offsetHeight ?? 0;
  const top = element.getBoundingClientRect().top + window.scrollY - offset;
  scrollToY(top, smooth);
  element.focus({ preventScroll: true });
  return true;
}
