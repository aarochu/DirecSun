import { act, fireEvent, render, screen } from '@testing-library/react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { App } from '../App';
import { batteryDomain, rateDomain } from '../ground/TelemetryPlots';
import { FakeSocket, tlm } from './fakeSocket';

afterEach(() => {
  FakeSocket.instances = [];
  vi.unstubAllGlobals();
});

describe('telemetry plot domains', () => {
  it('keeps box rate symmetric, at least 30 deg/s, in 30 deg/s steps', () => {
    expect(rateDomain([])).toEqual([-30, 30]);
    expect(rateDomain([{ t: 0, value: 12 }])).toEqual([-30, 30]);
    expect(rateDomain([{ t: 0, value: -71 }])).toEqual([-90, 90]);
  });

  it('shows 8.4 to 10.8 V and widens to fit the pack', () => {
    expect(batteryDomain([{ t: 0, value: 10.1 }])).toEqual([8.4, 10.8]);
    expect(batteryDomain([{ t: 0, value: 11.3 }, { t: 1, value: 8.1 }])).toEqual([8, 11.5]);
  });
});

describe('telemetry plots in the rail', () => {
  function connectedApp() {
    vi.stubGlobal('WebSocket', FakeSocket);
    render(
      <MemoryRouter initialEntries={['/']}>
        <App />
      </MemoryRouter>,
    );
    return () => {
      fireEvent.change(screen.getByRole('textbox', { name: 'DEVICE LINK / WEBSOCKET' }), { target: { value: 'ws://box.test/ws' } });
      fireEvent.click(screen.getByRole('button', { name: 'Connect' }));
      const socket = FakeSocket.instances[0]!;
      act(() => {
        socket.open();
        for (let i = 0; i < 5; i += 1) {
          socket.message(tlm({ t_ms: i * 20, psi_deg: 0, rate_dps: -4 + i, wheel_rpm: 2400 + i, wheel_ref_rpm: 2410, vbat: 10.12, mode: 'READY' }));
        }
      });
    };
  }

  it('draws box rate and wheel speed in simulation, and no battery plot because the simulation has no pack', () => {
    connectedApp();
    expect(screen.getByText(/Simulated box rate over the last 15 seconds/)).toBeTruthy();
    expect(screen.getByText(/Simulated wheel speed over the last 15 seconds, with the default 800 rpm low limit and 4000 rpm cap shaded/)).toBeTruthy();
    expect(screen.queryByText(/battery voltage over the last 15 seconds/)).toBeNull();
    expect(screen.getByText('BELOW LOW LIMIT (DEFAULT)')).toBeTruthy();
    expect(screen.getByText('ABOVE CAP (DEFAULT)')).toBeTruthy();
  });

  it('plots device rate, wheel speed with its reference, and battery with the arm and cutoff lines', () => {
    connectedApp()();
    expect(screen.getByText(/Device box rate/)).toBeTruthy();
    const valueOf = (caption: RegExp) => screen.getByText(caption).closest('figure')!.querySelector('.series__value')!.textContent;
    expect(valueOf(/Device wheel speed/)).toMatch(/^240\d RPM$/);
    expect(valueOf(/Device box rate/)).toMatch(/^-?\d(\.\d)? DEG\/S$/);
    expect(screen.getByText('REF')).toBeTruthy();
    expect(screen.getByText(/Device battery voltage/)).toBeTruthy();
    expect(valueOf(/Device battery voltage/)).toBe('10.1 V');
    expect(screen.getByText('ARM ABOVE 9.6 V')).toBeTruthy();
    expect(screen.getByText('CUTOFF 8.8 V')).toBeTruthy();
  });
});
