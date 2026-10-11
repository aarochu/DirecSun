import { useEffect, useState, type MouseEvent } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { LANDING_PATH } from '../data/navigation';

export interface ScrollIntent {
  smooth?: boolean;
}

const isPlainLeftClick = (event: MouseEvent) =>
  event.button === 0 && !event.metaKey && !event.ctrlKey && !event.shiftKey && !event.altKey;

export function useSectionNavigation() {
  const navigate = useNavigate();
  const location = useLocation();

  return (sectionId: string | null) => (event: MouseEvent<HTMLAnchorElement>) => {
    if (event.defaultPrevented || !isPlainLeftClick(event)) return;
    event.preventDefault();
    const hash = sectionId ? `#${sectionId}` : '';
    const onLanding = location.pathname === LANDING_PATH;
    const state: ScrollIntent = { smooth: onLanding };
    navigate({ pathname: LANDING_PATH, hash }, { state, replace: onLanding && location.hash === hash });
  };
}

export function useActiveSection(ids: readonly string[]): string | null {
  const [active, setActive] = useState<string | null>(null);

  useEffect(() => {
    if (!('IntersectionObserver' in window)) return;
    const visible = new Map<string, boolean>();
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) visible.set(entry.target.id, entry.isIntersecting);
        setActive(ids.find((id) => visible.get(id)) ?? null);
      },
      { rootMargin: '-40% 0px -55% 0px' },
    );
    for (const id of ids) {
      const element = document.getElementById(id);
      if (element) observer.observe(element);
    }
    return () => observer.disconnect();
  }, [ids]);

  return active;
}

export function useScrolledPast(threshold: number): boolean {
  const [past, setPast] = useState(false);

  useEffect(() => {
    let frame: number | null = null;
    const update = () => {
      frame = null;
      setPast(window.scrollY >= threshold);
    };
    const onScroll = () => {
      if (frame === null) frame = requestAnimationFrame(update);
    };
    update();
    window.addEventListener('scroll', onScroll, { passive: true });
    return () => {
      window.removeEventListener('scroll', onScroll);
      if (frame !== null) cancelAnimationFrame(frame);
    };
  }, [threshold]);

  return past;
}
