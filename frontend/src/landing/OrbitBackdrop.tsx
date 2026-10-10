import { OrbitSpace } from 'orbit-space';
import { useEffect, useRef, type ReactNode } from 'react';
import { releaseWebGLContext } from '../lib/environment';

export default function OrbitBackdrop({ children }: { children: ReactNode }) {
  const hostRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const canvas = hostRef.current?.querySelector('canvas');
    canvas?.setAttribute('aria-hidden', 'true');
    return () => releaseWebGLContext(canvas);
  }, []);

  return (
    <div className="hero__orbit" ref={hostRef}>
      <OrbitSpace>{children}</OrbitSpace>
    </div>
  );
}
