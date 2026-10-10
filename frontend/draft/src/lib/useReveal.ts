import { useEffect, useRef } from 'react';
import { prefersReducedMotion } from './environment';

export function useReveal<T extends HTMLElement>() {
  const ref = useRef<T>(null);

  useEffect(() => {
    const element = ref.current;
    if (!element || prefersReducedMotion() || !('IntersectionObserver' in window)) return;
    if (element.getBoundingClientRect().top < window.innerHeight * 0.9) return;

    element.dataset.reveal = 'pending';
    const observer = new IntersectionObserver(
      ([entry]) => {
        if (!entry?.isIntersecting) return;
        element.dataset.reveal = 'shown';
        observer.disconnect();
      },
      { rootMargin: '0px 0px -8% 0px' },
    );
    observer.observe(element);
    return () => {
      observer.disconnect();
      delete element.dataset.reveal;
    };
  }, []);

  return ref;
}
