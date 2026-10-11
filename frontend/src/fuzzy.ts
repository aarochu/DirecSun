// Fuzzy heading text: the heading is drawn to a canvas and every 1 px row is
// shifted sideways by a random amount each frame, harder while the pointer is
// over the text. Same idea and settings as React Bits' FuzzyText
// (baseIntensity, hoverIntensity, enableHover), written for this page without
// React. The heading keeps its real text for screen readers and search.

export interface FuzzyOptions {
  baseIntensity: number;
  hoverIntensity: number;
  enableHover: boolean;
  // Widest sideways shift, in CSS px, at intensity 1.
  fuzzRange?: number;
  reducedMotion?: boolean;
}

export interface FuzzyText {
  readonly canvas: HTMLCanvasElement;
  // false stops drawing (the hero is scrolled away).
  setActive(active: boolean): void;
  dispose(): void;
}

export function createFuzzyText(el: HTMLElement, options: FuzzyOptions): FuzzyText {
  const { baseIntensity, hoverIntensity, enableHover, fuzzRange = 30, reducedMotion = false } = options;
  const text = el.textContent ?? '';
  el.textContent = '';
  const label = document.createElement('span');
  label.className = 'sr-only';
  label.textContent = text;
  const canvas = document.createElement('canvas');
  canvas.className = 'fuzzy';
  canvas.setAttribute('aria-hidden', 'true');
  el.append(label, canvas);

  const ctx = canvas.getContext('2d')!;
  const source = document.createElement('canvas');
  const sctx = source.getContext('2d')!;

  const pad = fuzzRange / 2 + 2;
  let dpr = 1;
  let width = 0;
  let height = 0;
  let textLeft = 0;
  let textRight = 0;
  let hovering = false;
  let active = true;
  let raf = 0;

  const layout = () => {
    const style = getComputedStyle(el);
    const size = parseFloat(style.fontSize);
    const lineHeight = style.lineHeight === 'normal' ? size * 1.1 : parseFloat(style.lineHeight);
    const font = `${style.fontWeight} ${size}px ${style.fontFamily}`;
    const spacing = style.letterSpacing === 'normal' ? '0px' : style.letterSpacing;
    const maxWidth = el.clientWidth;
    dpr = Math.min(window.devicePixelRatio || 1, 2);

    const setup = (c: CanvasRenderingContext2D) => {
      c.font = font;
      if ('letterSpacing' in c) (c as CanvasRenderingContext2D & { letterSpacing: string }).letterSpacing = spacing;
      c.textBaseline = 'middle';
      c.fillStyle = style.color;
    };

    // Wrap words the way the browser would at this width.
    setup(sctx);
    const lines: string[] = [];
    for (const word of text.split(/\s+/).filter(Boolean)) {
      const last = lines[lines.length - 1];
      if (last !== undefined && sctx.measureText(`${last} ${word}`).width <= maxWidth) lines[lines.length - 1] = `${last} ${word}`;
      else lines.push(word);
    }
    const textWidth = Math.max(0, ...lines.map((line) => sctx.measureText(line).width));

    width = Math.ceil(textWidth + pad * 2);
    height = Math.ceil(lines.length * lineHeight + size * 0.18);
    textLeft = pad;
    textRight = pad + textWidth;

    for (const c of [canvas, source]) {
      c.width = Math.round(width * dpr);
      c.height = Math.round(height * dpr);
    }
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
    canvas.style.marginLeft = `${-pad}px`;

    sctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    setup(sctx);
    sctx.clearRect(0, 0, width, height);
    lines.forEach((line, i) => sctx.fillText(line, pad, i * lineHeight + lineHeight / 2));
    draw(0);
  };

  const draw = (intensity: number) => {
    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    if (intensity <= 0) {
      ctx.drawImage(source, 0, 0);
      return;
    }
    const range = fuzzRange * dpr;
    for (let y = 0; y < h; y++) {
      const dx = Math.floor(intensity * (Math.random() - 0.5) * range);
      ctx.drawImage(source, 0, y, w, 1, dx, y, w, 1);
    }
  };

  const tick = () => {
    raf = requestAnimationFrame(tick);
    if (!active || !width) return;
    draw(hovering ? hoverIntensity : baseIntensity);
  };

  const onMove = (event: PointerEvent) => {
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    hovering = x >= textLeft && x <= textRight && y >= 0 && y <= rect.height;
  };
  const onLeave = () => {
    hovering = false;
  };
  if (enableHover && !reducedMotion) {
    canvas.addEventListener('pointermove', onMove);
    canvas.addEventListener('pointerleave', onLeave);
  }

  let laidOutWidth = -1;
  const observer = new ResizeObserver(() => {
    if (el.clientWidth === laidOutWidth) return;
    laidOutWidth = el.clientWidth;
    layout();
  });
  let disposed = false;
  // Draw once fonts are in, so the canvas uses Inter rather than a fallback.
  void document.fonts.ready.then(() => {
    if (disposed) return;
    laidOutWidth = el.clientWidth;
    layout();
    observer.observe(el);
    if (!reducedMotion) raf = requestAnimationFrame(tick);
  });

  return {
    canvas,
    setActive(value) {
      active = value;
    },
    dispose() {
      disposed = true;
      cancelAnimationFrame(raf);
      observer.disconnect();
      canvas.removeEventListener('pointermove', onMove);
      canvas.removeEventListener('pointerleave', onLeave);
      el.textContent = text;
    },
  };
}
