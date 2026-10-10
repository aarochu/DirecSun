import { act, fireEvent, render, screen, within } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { hardwareParts } from '../data/hardware';

function renderAt(path: string) {
  return render(
    <MemoryRouter initialEntries={[path]}>
      <App />
    </MemoryRouter>,
  );
}

const advance = (ms: number) =>
  act(() => {
    vi.advanceTimersByTime(ms);
  });

const button = (name: RegExp) => screen.getByRole('button', { name });
function readout(label: string) {
  const rail = screen.getByRole('region', { name: 'TELEMETRY' });
  const term = [...rail.querySelectorAll('dt')].find((dt) => dt.textContent === label);
  if (!term) throw new Error(`no readout labeled ${label}`);
  return term.nextElementSibling!.textContent;
}

describe('landing page', () => {
  it('LANDING_NAV: every nav anchor targets an existing, correctly labeled section', () => {
    renderAt('/');
    const nav = screen.getByRole('navigation', { name: 'Primary' });
    const expected = {
      System: ['system', 'One axis. One reaction wheel. A complete feedback loop.'],
      Hardware: ['hardware', 'Every component has a job.'],
      Control: ['control', 'Sense. Calculate. Correct.'],
    } as const;
    for (const [label, [id, heading]] of Object.entries(expected)) {
      const link = within(nav).getByRole('link', { name: label });
      expect(link.getAttribute('href')).toBe(`/#${id}`);
      const section = document.getElementById(id)!;
      expect(within(section).getByRole('heading', { level: 2 }).textContent).toBe(heading);
    }
    for (const link of screen.getAllByRole('link', { name: /Ground Station/ })) {
      expect(link.getAttribute('href')).toBe('/ground-station');
    }
    expect(screen.getByRole('link', { name: /Explore the system/ }).getAttribute('href')).toBe('/#system');
  });

  it('renders the hero copy and section order', () => {
    renderAt('/');
    expect(screen.getByRole('heading', { level: 1 }).textContent).toBe('Control the attitude.');
    expect(screen.getByText('SINGLE-AXIS ATTITUDE CONTROL / 2U CUBESAT')).toBeTruthy();
    const ids = [...document.querySelectorAll('main > section')].map((s) => s.id);
    expect(ids).toEqual(['top', 'system', 'hardware', 'control', 'ground']);
  });

  it('HARDWARE_ORDER and HARDWARE_DEEP_COPY: ten rows, fixed order, all sublabels and 2–3 specs', () => {
    renderAt('/');
    const rows = within(document.getElementById('hardware')!).getAllByRole('article');
    expect(rows.map((row) => row.querySelector('.hardware-row__name')!.textContent)).toEqual(
      hardwareParts.map((part) => part.name),
    );
    for (const row of rows) {
      for (const label of ['FUNCTION', 'CONNECTION', 'CONTROL ROLE', 'DESIGN NOTE']) {
        expect(within(row).getByText(label)).toBeTruthy();
      }
      const specs = row.querySelectorAll('.hardware-row__specs li');
      expect(specs.length).toBeGreaterThanOrEqual(2);
      expect(specs.length).toBeLessThanOrEqual(3);
    }
  });

  it('hardware rows start collapsed and expand individually or all at once', () => {
    renderAt('/');
    const toggles = within(document.getElementById('hardware')!).getAllByRole('button', { expanded: false });
    expect(toggles).toHaveLength(hardwareParts.length);
    fireEvent.click(toggles[2]!);
    expect(toggles[2]!.getAttribute('aria-expanded')).toBe('true');
    expect(document.getElementById(toggles[2]!.getAttribute('aria-controls')!)!.hidden).toBe(false);
    fireEvent.click(screen.getByRole('button', { name: 'Expand all' }));
    expect(toggles.every((t) => t.getAttribute('aria-expanded') === 'true')).toBe(true);
    fireEvent.click(screen.getByRole('button', { name: 'Collapse all' }));
    expect(toggles.every((t) => t.getAttribute('aria-expanded') === 'false')).toBe(true);
  });

  it('MISSING_CAD: truthful placeholders, no photos or renders', () => {
    renderAt('/');
    expect(document.querySelectorAll('img')).toHaveLength(0);
    expect(screen.getAllByText('CAD RENDER NOT PROVIDED')).toHaveLength(1);
    expect(screen.getAllByText('HARDWARE IMAGE NOT PROVIDED')).toHaveLength(9);
  });

  it('keeps the BMP280 off the control path in the diagram', () => {
    renderAt('/');
    const telemetry = screen.getByRole('group', { name: /TELEMETRY \/ NOT A YAW-CONTROL INPUT/ });
    expect(within(telemetry).getByText('BMP280')).toBeTruthy();
    const signal = screen.getByRole('group', { name: 'SIGNAL / CONTROL' });
    expect(within(signal).queryByText('BMP280')).toBeNull();
  });
});

describe('ground station', () => {
  beforeEach(() => {
    vi.useFakeTimers({
      toFake: ['setTimeout', 'clearTimeout', 'requestAnimationFrame', 'cancelAnimationFrame', 'performance'],
    });
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it('opens directly with the simulated state and the default values', () => {
    renderAt('/ground-station');
    expect(screen.getByText('SIMULATED — NO DEVICE CONNECTION')).toBeTruthy();
    expect(readout('SIMULATION PHASE')).toBe('READY / SIMULATED');
    expect(readout('HEADING')).toBe('0.0°');
    expect(readout('TARGET')).toBe('0.0°');
    expect(readout('BODY RATE')).toBe('0.0°/s');
    expect(readout('REACTION WHEEL')).toMatch(/^2400 RPM/);
    expect(screen.getAllByText('SCHEMATIC MODEL — CAD NOT LOADED').length).toBeGreaterThan(0);
    expect(document.querySelector('canvas')).toBeNull();
    expect(screen.queryByText(/battery|volt/i)).toBeNull();
    expect(document.body.textContent).not.toMatch(/\b(CONNECTED|ONLINE|LIVE|ARMED|NOMINAL|FLIGHT READY)\b/);
    expect(button(/Stop the simulation/).hasAttribute('disabled')).toBe(true);
    expect(button(/Reset the simulation/).hasAttribute('disabled')).toBe(false);
  });

  it('judge flow: right 90°, hold, automatic return, READY at 0°', () => {
    renderAt('/ground-station');
    fireEvent.click(button(/Rotate right ninety degrees/));

    expect(button(/Rotate right ninety degrees/).hasAttribute('disabled')).toBe(true);
    expect(button(/Rotate left ninety degrees/).hasAttribute('disabled')).toBe(true);
    expect(button(/Stop the simulation/).hasAttribute('disabled')).toBe(false);
    expect(readout('SIMULATION PHASE')).toBe('ROTATING / SIMULATED');
    expect(readout('TARGET')).toBe('+90.0°');

    advance(2200);
    expect(readout('SIMULATION PHASE')).toBe('HOLDING TARGET / SIMULATED');
    expect(readout('HEADING')).toBe('+90.0°');
    expect(readout('BODY RATE')).toBe('0.0°/s');

    advance(1000);
    expect(readout('SIMULATION PHASE')).toBe('RETURNING / SIMULATED');
    expect(readout('TARGET')).toBe('0.0°');

    advance(3000);
    expect(readout('SIMULATION PHASE')).toBe('READY / SIMULATED');
    expect(readout('HEADING')).toBe('0.0°');
    expect(button(/Rotate left ninety degrees/).hasAttribute('disabled')).toBe(false);
  });

  it('MOCK_ONLY: every control acts locally; no network or device requests', () => {
    const fetchSpy = vi.fn();
    const socketSpy = vi.fn();
    const beaconSpy = vi.fn();
    vi.stubGlobal('fetch', fetchSpy);
    vi.stubGlobal('WebSocket', socketSpy);
    vi.stubGlobal('EventSource', socketSpy);
    Object.defineProperty(navigator, 'sendBeacon', { value: beaconSpy, configurable: true });
    const xhrOpen = vi.spyOn(XMLHttpRequest.prototype, 'open');

    renderAt('/ground-station');
    fireEvent.click(button(/Rotate right ninety degrees/));
    advance(800);
    fireEvent.click(button(/Stop the simulation/));
    fireEvent.click(button(/Reset the simulation/));
    fireEvent.click(button(/Rotate left ninety degrees/));
    advance(7000);
    fireEvent.click(button(/Reset the simulation/));

    expect(fetchSpy).not.toHaveBeenCalled();
    expect(socketSpy).not.toHaveBeenCalled();
    expect(beaconSpy).not.toHaveBeenCalled();
    expect(xhrOpen).not.toHaveBeenCalled();
    delete (navigator as { sendBeacon?: unknown }).sendBeacon;
  });

  it('UNMOUNT_CANCEL: leaving the route mid-turn disposes the frame loop', () => {
    const errors = vi.spyOn(console, 'error');
    renderAt('/ground-station');
    fireEvent.click(button(/Rotate left ninety degrees/));
    advance(500);
    expect(vi.getTimerCount()).toBeGreaterThan(0);

    fireEvent.click(screen.getByRole('link', { name: /Overview/ }));
    expect(screen.getByRole('heading', { level: 1 }).textContent).toBe('Control the attitude.');
    advance(100);
    expect(vi.getTimerCount()).toBe(0);
    advance(5000);
    expect(errors).not.toHaveBeenCalled();
  });

  it('announces phase changes politely, not every number', () => {
    renderAt('/ground-station');
    const live = document.querySelector('[aria-live="polite"]')!;
    fireEvent.click(button(/Rotate right ninety degrees/));
    expect(live.textContent).toBe('Simulation rotating right 90 degrees.');
    advance(400);
    expect(live.textContent).toBe('Simulation rotating right 90 degrees.');
    advance(1800);
    expect(live.textContent).toBe('Holding the target heading.');
  });
});
