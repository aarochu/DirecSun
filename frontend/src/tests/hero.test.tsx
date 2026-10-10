import { render, screen } from '@testing-library/react';
import type { ReactNode } from 'react';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';

const env = vi.hoisted(() => ({ reducedMotion: false }));

vi.mock('orbit-space', () => ({
  OrbitSpace: ({ children }: { children?: ReactNode }) => (
    <>
      <div data-testid="orbit-space">{children}</div>
      <div style={{ position: 'fixed', inset: 0, zIndex: -1 }} />
    </>
  ),
}));

vi.mock('../lib/environment', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../lib/environment')>();
  return {
    ...actual,
    isWebGLAvailable: () => true,
    prefersReducedMotion: () => env.reducedMotion,
    usePrefersReducedMotion: () => env.reducedMotion,
  };
});

const { LandingHero } = await import('../landing/LandingHero');

function renderHero() {
  return render(
    <MemoryRouter>
      <LandingHero />
    </MemoryRouter>,
  );
}

afterEach(() => {
  env.reducedMotion = false;
});

describe('hero background', () => {
  it('mounts OrbitSpace inside the hero only, with the content as its children', async () => {
    renderHero();
    const orbit = await screen.findByTestId('orbit-space');
    const hero = document.getElementById('top')!;
    expect(hero.dataset.background).toBe('orbit-space');
    expect(hero.contains(orbit)).toBe(true);
    expect(orbit.querySelector('h1')?.textContent).toBe('Control the attitude.');
  });

  it('REDUCED_MOTION: static background, same content, no OrbitSpace', () => {
    env.reducedMotion = true;
    renderHero();
    expect(document.getElementById('top')!.dataset.background).toBe('static');
    expect(screen.queryByTestId('orbit-space')).toBeNull();
    expect(screen.getByRole('link', { name: /Open Ground Station/ })).toBeTruthy();
  });
});
