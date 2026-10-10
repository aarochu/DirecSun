import { useEffect, useRef, useState } from 'react';

export interface ElementSize {
  width: number;
  height: number;
}

export function useElementSize<T extends HTMLElement>(fallback: ElementSize = { width: 320, height: 176 }) {
  const ref = useRef<T>(null);
  const [size, setSize] = useState(fallback);
  const { width: fallbackWidth, height: fallbackHeight } = fallback;

  useEffect(() => {
    const element = ref.current;
    if (!element) return;
    const measure = () => {
      const width = Math.round(element.clientWidth) || fallbackWidth;
      const height = Math.round(element.clientHeight) || fallbackHeight;
      setSize((current) =>
        current.width === width && current.height === height ? current : { width, height },
      );
    };
    measure();
    if (!('ResizeObserver' in window)) return;
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    return () => observer.disconnect();
  }, [fallbackWidth, fallbackHeight]);

  return [ref, size] as const;
}
