import '@fontsource-variable/inter';
import '@fontsource/ibm-plex-mono/400.css';
import '@fontsource/ibm-plex-mono/500.css';
import './styles/tokens.css';
import './styles.css';
import { animate, createTimeline, onScroll, splitText, stagger, svg, utils, type Timeline } from 'animejs';
import logoUrl from './assets/logo.webp';
import {
  blurbs,
  control,
  footer,
  ground,
  GROUND_STATION_URL,
  hardware,
  hardwareParts,
  hardwareViews,
  hero,
  imageAssets,
  sectionLinks,
  system,
} from './content';
import { flowMarkup, initFlow } from './flow';
import { createScene, view, type CubeSatScene, type PartState } from './scene';
import { createStars, type Stars } from './stars';

// ---------------------------------------------------------------------------
// Story layout. Each chapter is an empty scroll spacer of the given height; the
// copy lives in one fixed overlay and the CubeSat in one fixed canvas, both
// driven by a single timeline synced to the scroll position (the animejs.com
// pattern). Timeline time is in tenths of a viewport height: 1000 = 100vh.
// ---------------------------------------------------------------------------

const PART_STEP = 120;
const HW_INTRO = 140;
const CHAPTERS = [
  { id: 'top', label: 'INTRO', vh: 200 },
  { id: 'system', label: '001 / SYSTEM', vh: 340 },
  { id: 'hardware', label: '002 / HARDWARE', vh: HW_INTRO + hardwareParts.length * PART_STEP },
  { id: 'control', label: '003 / CONTROL', vh: 400 },
  { id: 'ground', label: '004 / GROUND STATION', vh: 320 },
] as const;

const START: Record<string, number> = {};
{
  let at = 0;
  for (const chapter of CHAPTERS) {
    START[chapter.id] = at * 10;
    at += chapter.vh;
  }
}
const TOTAL_VH = CHAPTERS.reduce((sum, chapter) => sum + chapter.vh, 0);
const END = (TOTAL_VH - 100) * 10;
const HW_PARTS_AT = START.hardware + HW_INTRO * 10;
const partAt = (index: number) => HW_PARTS_AT + index * PART_STEP * 10;

const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

const esc = (text: string) =>
  text.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]!);

const arrowUpRight =
  '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M7 7h10v10"/><path d="M7 17 17 7"/></svg>';
const arrowDown =
  '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 5v14"/><path d="m19 12-7 7-7-7"/></svg>';

// ---------------------------------------------------------------------------
// Markup
// ---------------------------------------------------------------------------

function render(root: HTMLElement) {
  const navLinks = sectionLinks
    .map((link) => `<a class="site-nav__link" href="#${link.id}" data-nav="${link.id}">${esc(link.label)}</a>`)
    .join('');

  const parts = hardwareParts
    .map((part, index) => {
      const asset = imageAssets[part.media];
      return `
      <article class="panel panel--part" data-panel="part-${index}" aria-labelledby="part-${part.id}">
        <p class="part__index mono-label"><span class="accent">${part.index}</span> / ${String(hardwareParts.length).padStart(2, '0')}</p>
        <h3 class="part__name" id="part-${part.id}">${esc(part.name)}</h3>
        <figure class="part__media">
          <img src="${asset.src}" alt="${esc(asset.alt)}" loading="lazy" decoding="async" />
        </figure>
        <p class="part__func">${esc(blurbs[part.id] ?? part.func)}</p>
      </article>`;
    })
    .join('');

  root.innerHTML = `
  <a class="skip-link" href="#main">Skip to content</a>
  <div class="loader" aria-hidden="true"><img class="loader__logo" src="${logoUrl}" alt="" /></div>

  <header class="site-header" data-site-header>
    <div class="site-header__inner container">
      <a href="#top" class="wordmark" data-nav="top" aria-label="DirecSun home">
        <img class="wordmark__logo" src="${logoUrl}" alt="" />DirecSun
      </a>
      <nav class="site-nav" aria-label="Primary">
        ${navLinks}
        <a href="${GROUND_STATION_URL}" class="button button--primary header-cta">Ground Station ${arrowUpRight}</a>
      </nav>
    </div>
  </header>

  <canvas class="stars-canvas" aria-hidden="true"></canvas>
  <canvas class="stage-canvas" aria-hidden="true"></canvas>

  <div class="overlay" id="main">
    <section class="panel panel--hero" data-panel="hero" aria-labelledby="hero-title">
      <h1 class="hero__title" id="hero-title">${esc(hero.title)}</h1>
      <p class="hero__lead">${esc(hero.lead)}</p>
      <div class="hero__actions">
        <a href="${GROUND_STATION_URL}" class="button button--primary">${esc(hero.primary)} ${arrowUpRight}</a>
        <a href="#system" data-nav="system" class="button button--secondary">${esc(hero.secondary)} ${arrowDown}</a>
      </div>
    </section>
    <div class="panel panel--hero-foot" data-panel="hero-foot">
      <p class="mono-label hero__scroll" aria-hidden="true">${esc(hero.scroll)}</p>
    </div>

    <section class="panel panel--system" data-panel="system" aria-labelledby="system-title">
      <p class="section-heading__index mono-label">${esc(system.index)}</p>
      <h2 class="section-heading__title" id="system-title">${esc(system.title)}</h2>
      <p class="section-heading__intro">${esc(system.intro)}</p>
      ${flowMarkup()}
    </section>

    <section class="panel panel--hw-head" data-panel="hw-head" aria-labelledby="hardware-title">
      <p class="section-heading__index mono-label">${esc(hardware.index)}</p>
      <h2 class="section-heading__title" id="hardware-title">${esc(hardware.title)}</h2>
      <p class="section-heading__intro">${esc(hardware.intro)}</p>
    </section>
    ${parts}
    <nav class="panel panel--rail" data-panel="rail" aria-label="Components">
      <p class="mono-label">${hardwareParts.length} COMPONENTS</p>
      <ol>${hardwareParts
        .map((part, index) => `<li><a href="#hardware" data-part="${index}"><span>${part.index}</span>${esc(part.name)}</a></li>`)
        .join('')}</ol>
    </nav>

    <section class="panel panel--control" data-panel="control" aria-labelledby="control-title">
      <p class="section-heading__index mono-label">${esc(control.index)}</p>
      <h2 class="section-heading__title" id="control-title">${esc(control.title)}</h2>
      <ol class="control-steps">
        ${control.steps
          .map(
            (step, index) => `
          <li class="control-step" data-step="${index}">
            <h3 class="control-step__title">${esc(step.title)}</h3>
            <p class="control-step__body">${esc(step.body)}</p>
          </li>`,
          )
          .join('')}
      </ol>
    </section>

    <section class="panel panel--ground" data-panel="ground" aria-labelledby="ground-title">
      <p class="section-heading__index mono-label">${esc(ground.index)}</p>
      <h2 class="section-heading__title" id="ground-title">${esc(ground.title)}</h2>
      <p class="section-heading__intro">${esc(ground.intro)}</p>
      <a href="${GROUND_STATION_URL}" class="button button--primary ground__cta">${esc(ground.cta)} ${arrowUpRight}</a>
    </section>
    <div class="panel panel--readout" data-panel="readout" aria-hidden="true">
      <p class="mono-label">${esc(ground.sequence)}</p>
      <p class="readout__value"><span data-readout>0°</span><small data-readout-dir></small></p>
    </div>
  </div>

  <div class="ticker" aria-hidden="true">
    <span class="ticker__label mono-label" data-ticker-label>${CHAPTERS[0].label}</span>
    <span class="ticker__track">${'<i></i>'.repeat(40)}<b class="ticker__head"></b></span>
  </div>

  <main class="story" data-story>
    ${CHAPTERS.map((chapter) => `<div class="chapter" id="${chapter.id}" style="height:${chapter.vh}vh"></div>`).join('')}
  </main>

  <footer class="site-footer">
    <div class="site-footer__inner container">
      <img class="site-footer__logo" src="${logoUrl}" alt="DirecSun logo" />
      <p class="site-footer__statement">${esc(footer.statement)}</p>
      <nav class="site-footer__links" aria-label="Footer">
        <a href="#system" data-nav="system">System</a>
        <a href="#hardware" data-nav="hardware">Hardware</a>
        <a href="${GROUND_STATION_URL}">Ground Station</a>
      </nav>
    </div>
  </footer>`;
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

const $ = <T extends Element = HTMLElement>(selector: string) => document.querySelector<T>(selector)!;
const $$ = <T extends Element = HTMLElement>(selector: string) => [...document.querySelectorAll<T>(selector)];
const panel = (name: string) => $(`[data-panel="${name}"]`);

const IN = { duration: 400, ease: 'out(3)' } as const;
const OUT = { duration: 400, ease: 'in(2)' } as const;

function fadeIn(tl: Timeline, el: Element | Element[], at: number, duration = 400) {
  tl.add(el, { opacity: [0, 1], y: [32, 0], ...IN, duration }, at);
}
function fadeOut(tl: Timeline, el: Element | Element[], at: number, duration = 400) {
  tl.add(el, { opacity: [1, 0], y: [0, -32], ...OUT, duration }, at);
}

interface Layout {
  narrow: boolean;
  shiftX: number;
  shiftY: number;
  distScale: number;
}

const layoutFor = (): Layout => {
  const narrow = window.innerWidth < 900;
  return narrow
    ? { narrow, shiftX: 0, shiftY: -0.2, distScale: 1.3 }
    : { narrow, shiftX: 0.22, shiftY: 0, distScale: 1 };
};

function buildTimeline(scene: CubeSatScene | null, layout: Layout): Timeline {
  const parts = scene?.parts ?? [];
  const setParts = (tl: Timeline, at: number, look: (part: PartState) => { o: number; hl: number }, duration = 400) => {
    if (!parts.length) return;
    for (const part of parts) tl.add(part, { ...look(part), duration, ease: 'inOut(2)' }, at);
  };
  const neutral = (hidePin: boolean) => (part: PartState) => ({ o: hidePin && part.name === 'lock_pin' ? 0 : 1, hl: 0 });
  const highlight = (names: readonly string[], carriers: readonly string[] = []) => {
    const subject = new Set(names);
    const carrier = new Set(carriers);
    return (part: PartState) =>
      subject.has(part.name) ? { o: 1, hl: 1 } : carrier.has(part.name) ? { o: 1, hl: 0 } : { o: 0.14, hl: 0 };
  };
  const cam = (tl: Timeline, at: number, params: Partial<typeof view>, duration = 500) =>
    tl.add(view, { ...params, duration, ease: 'inOut(2)' }, at);

  const stack = scene?.frame('stack', 1.4, false) ?? { tx: 0, ty: 0.1, tz: 0, dist: 0.6 };
  const stackExploded = scene?.frame('stack', 1.25, true) ?? stack;

  // Initial state.
  utils.set(view, { ...stack, az: 38, el: 20, yaw: 0, explode: 0, wheel: 0, idle: 1, light: 0, ...layout });
  for (const part of parts) {
    part.o = part.name === 'lock_pin' ? 0 : 1;
    part.hl = 0;
  }
  utils.set($$('[data-panel]'), { opacity: 0 });
  utils.set([panel('hero'), panel('hero-foot')], { opacity: 1 });

  const tl = createTimeline({ autoplay: false, defaults: { ease: 'inOut(2)' } });
  // A zero-length anchor makes the timeline exactly END long, so scroll
  // progress maps linearly onto chapter times.
  tl.add({ t: 0 }, { t: 1, duration: END, ease: 'linear' }, 0);

  // INTRO ------------------------------------------------------------------
  fadeOut(tl, panel('hero-foot'), 0, 300);
  fadeOut(tl, panel('hero'), 500, 700);
  cam(tl, 0, { idle: 0, el: 14, az: 20, distScale: layout.distScale * 0.92 }, 1600);

  // 001 SYSTEM -------------------------------------------------------------
  const sys = START.system;
  fadeIn(tl, panel('system'), sys - 600, 500);
  // The chart builds itself: parts appear, then the wires draw in.
  const flowNodes = $$('.flow__node');
  const flowLines = svg.createDrawable('.flow__line');
  utils.set(flowNodes, { opacity: 0 });
  utils.set(flowLines, { draw: '0 0' });
  tl.add(flowNodes, { opacity: [0, 1], duration: 250, delay: stagger(25), ease: 'out(2)' }, sys - 200);
  tl.add(flowLines, { draw: ['0 0', '0 1'], duration: 450, delay: stagger(30), ease: 'inOut(2)' }, sys + 200);
  // One axis: the box turns a full revolution on its bearings.
  tl.add(view, { yaw: 360, distScale: layout.distScale, duration: 3600, ease: 'inOut(1.5)' }, sys - 400);
  cam(tl, sys + 400, { el: 24, az: 38 }, 1400);
  fadeOut(tl, panel('system'), sys + 2900, 500);

  // 002 HARDWARE -----------------------------------------------------------
  const hw = START.hardware;
  fadeIn(tl, panel('hw-head'), hw - 300, 500);
  fadeOut(tl, panel('hw-head'), HW_PARTS_AT - 500, 400);
  cam(tl, hw, { explode: 1, ...stackExploded, az: 38, el: 18, shiftX: layout.narrow ? 0 : 0.08 }, 1100);
  setParts(tl, hw + 300, neutral(false), 400);
  fadeIn(tl, panel('rail'), HW_PARTS_AT - 300, 400);

  hardwareParts.forEach((part, index) => {
    const at = partAt(index);
    const v = hardwareViews[part.id];
    const el = panel(`part-${index}`);
    fadeIn(tl, el, at - 200, 400);
    if (index < hardwareParts.length - 1) fadeOut(tl, el, at + PART_STEP * 10 - 250, 300);
    if (!v) return;
    const framing = scene?.frame(v.frame === 'stack' ? 'stack' : v.subject, v.frame === 'stack' ? 1.25 : v.zoom, true);
    cam(tl, at - 300, { ...(framing ?? {}), az: v.azimuthDeg, el: v.elevationDeg }, 700);
    setParts(tl, at - 250, highlight(v.subject, v.carrier), 500);
  });
  const last = panel(`part-${hardwareParts.length - 1}`);

  // 003 CONTROL ------------------------------------------------------------
  const ctl = START.control;
  fadeOut(tl, [last, panel('rail')], ctl - 300, 300);
  cam(tl, ctl - 200, { explode: 0, ...stack, az: 30, el: 16, shiftX: layout.shiftX }, 900);
  setParts(tl, ctl - 200, neutral(false), 500);
  tl.add(view, { light: 1, duration: 500, ease: 'inOut(2)' }, ctl - 100);
  fadeIn(tl, panel('control'), ctl + 100, 500);

  const steps = $$('.control-step');
  utils.set(steps, { opacity: 0.32 });
  control.steps.forEach((step, index) => {
    const at = ctl + 700 + index * 800;
    tl.add(steps[index]!, { opacity: [0.32, 1], duration: 250, ease: 'out(2)' }, at);
    if (index > 0) tl.add(steps[index - 1]!, { opacity: [1, 0.32], duration: 250 }, at);
    setParts(tl, at, highlight(step.parts), 400);
    if (step.title === 'Turn') tl.add(view, { wheel: 1, duration: 400 }, at);
  });
  const loopAt = ctl + 700 + control.steps.length * 800;
  tl.add(steps, { opacity: 1, duration: 300 }, loopAt);
  setParts(tl, loopAt, neutral(false), 400);
  fadeOut(tl, panel('control'), START.ground - 500, 400);
  tl.add(view, { light: 0, duration: 400, ease: 'inOut(2)' }, START.ground - 400);

  // 004 GROUND STATION -----------------------------------------------------
  // The box leaves its lock pin and runs the judge sequence: left 90, hold,
  // return; right 90, hold.
  const gs = START.ground;
  setParts(tl, gs - 300, neutral(true), 300);
  fadeIn(tl, panel('ground'), gs - 200, 500);
  fadeIn(tl, panel('readout'), gs + 200, 400);
  cam(tl, gs - 300, { el: 30, az: 24 }, 700);
  // Ends 400 before END so the right turn has its hold.
  tl.add(view, { yaw: 360 + 90, duration: 500, ease: 'inOut(3)' }, gs + 300)
    .add(view, { yaw: 360, duration: 500, ease: 'inOut(3)' }, gs + 1000)
    .add(view, { yaw: 360 - 90, duration: 500, ease: 'inOut(3)' }, gs + 1500);

  return tl;
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

function webglAvailable() {
  try {
    const canvas = document.createElement('canvas');
    return !!(canvas.getContext('webgl2') ?? canvas.getContext('webgl'));
  } catch {
    return false;
  }
}

function boot() {
  const root = $('#app');
  render(root);

  const storyEl = $('[data-story]');
  const header = $('[data-site-header]');
  const tickerLabel = $('[data-ticker-label]');
  const tickerHead = $('.ticker__head');
  const readout = $('[data-readout]');
  const readoutDir = $('[data-readout-dir]');
  const railLinks = $$<HTMLAnchorElement>('[data-part]');
  const allPanels = $$('[data-panel]');
  const canvas = $<HTMLCanvasElement>('.stage-canvas');

  const timeToY = (time: number) => storyEl.offsetTop + (time / 1000) * window.innerHeight;
  const go = (time: number) => window.scrollTo({ top: timeToY(time), behavior: reducedMotion ? 'auto' : 'smooth' });
  const navTime: Record<string, number> = {
    top: 0,
    system: START.system + 300,
    hardware: START.hardware + 400,
    control: START.control + 1200,
  };

  document.addEventListener('click', (event) => {
    const link = (event.target as Element).closest<HTMLAnchorElement>('[data-nav], [data-part]');
    if (!link) return;
    event.preventDefault();
    if (link.dataset.part !== undefined) go(partAt(Number(link.dataset.part)) + 150);
    else go(navTime[link.dataset.nav!] ?? 0);
  });

  // Keyboard users: focusing a link in a hidden panel scrolls its chapter in.
  const panelTime = (name: string) => {
    if (name.startsWith('part-')) return partAt(Number(name.slice(5))) + 150;
    if (name === 'rail') return HW_PARTS_AT + 150;
    if (name.startsWith('hero')) return 0;
    if (name === 'system') return START.system + 300;
    if (name === 'hw-head') return START.hardware + 400;
    if (name === 'control') return START.control + 2000;
    return START.ground + 400;
  };
  document.addEventListener('focusin', (event) => {
    const owner = (event.target as Element).closest<HTMLElement>('[data-panel]');
    if (owner && Number(owner.style.opacity || 1) < 0.5) go(panelTime(owner.dataset.panel!));
  });

  let scene: CubeSatScene | null = null;
  const hasWebGL = webglAvailable();
  document.documentElement.dataset.webgl = String(hasWebGL);
  if (hasWebGL) {
    try {
      scene = createScene(canvas, reducedMotion);
    } catch {
      scene = null;
      document.documentElement.dataset.webgl = 'false';
    }
  }

  // The previous site's star field, behind the hero only.
  let stars: Stars | null = null;
  if (hasWebGL && !reducedMotion) {
    try {
      stars = createStars($<HTMLCanvasElement>('.stars-canvas'));
    } catch {
      stars = null;
    }
  }

  let tl: Timeline | null = null;
  let layout = layoutFor();

  // Flowchart focus lights the same parts on the model (system chapter only;
  // the timeline owns part looks everywhere else).
  const restingLook = (part: PartState) => ({ o: part.name === 'lock_pin' ? 0 : 1, hl: 0 });
  const flow = initFlow($('[data-flow-root]'), (hw) => {
    if (!scene) return;
    const subject = new Set(hw?.flatMap((id) => hardwareViews[id]?.subject ?? []));
    const carrier = new Set(hw?.flatMap((id) => hardwareViews[id]?.carrier ?? []));
    for (const part of scene.parts) {
      const look = !hw
        ? restingLook(part)
        : subject.has(part.name)
          ? { o: 1, hl: 1 }
          : carrier.has(part.name)
            ? { o: 1, hl: 0 }
            : { o: 0.14, hl: 0 };
      // composition 'none' leaves the scroll timeline's own tweens alone.
      animate(part, { ...look, duration: reducedMotion ? 0 : 250, ease: 'out(2)', composition: 'none' });
    }
  });

  const onRender = () => {
    // Hidden panels must not take clicks.
    for (const el of allPanels) el.style.pointerEvents = Number(el.style.opacity) > 0.5 ? 'auto' : 'none';
    document.documentElement.style.setProperty('--light', view.light.toFixed(3));
    document.documentElement.classList.toggle('is-light', view.light > 0.5);
    const yaw = Math.round(view.yaw - 360);
    if (tl && tl.currentTime >= START.ground - 600) {
      readout.textContent = `${Math.abs(Math.max(-90, Math.min(90, yaw)))}°`;
      readoutDir.textContent = yaw > 1 ? 'LEFT' : yaw < -1 ? 'RIGHT' : 'ORIGIN';
    }
  };

  const onProgress = (progress: number) => {
    const time = progress * END;
    let current: (typeof CHAPTERS)[number] = CHAPTERS[0];
    for (const chapter of CHAPTERS) if (time >= START[chapter.id]! - 300) current = chapter;
    tickerLabel.textContent = current.label;
    stars?.setVisibility(Math.max(0, Math.min(1, 1 - time / 1400)));
    if (current.id !== 'system' && flow.active) flow.clear();
    tickerHead.style.left = `${(progress * 100).toFixed(2)}%`;
    header.classList.toggle('is-solid', window.scrollY > 72);
    const active = Math.floor((time - HW_PARTS_AT + 300) / (PART_STEP * 10));
    railLinks.forEach((link, index) => link.toggleAttribute('aria-current', index === active));
  };

  let observer: ReturnType<typeof onScroll> | null = null;

  const build = () => {
    observer?.revert();
    tl?.revert();
    tl = buildTimeline(scene, layout);
    tl.onRender = onRender;
    observer = onScroll({
      target: storyEl,
      enter: 'top top',
      leave: 'bottom bottom',
      sync: reducedMotion ? true : 0.18,
      onUpdate: (self) => onProgress(self.progress),
    });
    observer.link(tl);
    (window as unknown as { __tl: Timeline }).__tl = tl;
    onProgress(observer.progress);
  };

  const intro = () => {
    const loader = $('.loader');
    if (reducedMotion) {
      loader.remove();
      return;
    }
    const { chars } = splitText('.hero__title', { chars: true });
    animate(loader, { opacity: 0, duration: 600, delay: 150, ease: 'out(2)', onComplete: () => loader.remove() });
    animate(chars, { y: ['110%', '0%'], opacity: [0, 1], delay: stagger(22, { start: 250 }), duration: 750, ease: 'out(4)' });
    animate('.hero__lead, .hero__actions, .panel--hero-foot > *', {
      opacity: [0, 1],
      y: [16, 0],
      delay: stagger(90, { start: 500 }),
      duration: 600,
      ease: 'out(3)',
    });
  };

  const start = () => {
    build();
    intro();
  };
  if (scene) {
    scene.ready.then(start, () => {
      scene?.dispose();
      scene = null;
      document.documentElement.dataset.webgl = 'false';
      start();
    });
  } else {
    start();
  }

  let resizeTimer = 0;
  window.addEventListener('resize', () => {
    window.clearTimeout(resizeTimer);
    resizeTimer = window.setTimeout(() => {
      const next = layoutFor();
      if (next.narrow === layout.narrow) return;
      layout = next;
      build();
    }, 150);
  });
}

boot();
