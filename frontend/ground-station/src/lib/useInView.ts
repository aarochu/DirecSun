import { useEffect, useRef, useState } from 'react';

export function useInView<T extends HTMLElement>(rootMargin = '200px') {
  const ref = useRef<T>(null);
  const [inView, setInView] = useState(false);

  useEffect(() => {
    const element = ref.current;
    if (!element || !('IntersectionObserver' in window)) return;
    const observer = new IntersectionObserver(([entry]) => setInView(entry?.isIntersecting ?? false), { rootMargin });
    observer.observe(element);
    return () => observer.disconnect();
  }, [rootMargin]);

  return [ref, inView] as const;
}
