import { ArrowDown, ArrowUpRight } from 'lucide-react';
import { Component, lazy, Suspense, useState, type ErrorInfo, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { GROUND_STATION_PATH, landingHref } from '../data/navigation';
import { isWebGLAvailable, usePrefersReducedMotion } from '../lib/environment';
import { useSectionNavigation } from '../lib/navigation';

const OrbitBackdrop = lazy(() => import('./OrbitBackdrop'));

export function LandingHero() {
  const reducedMotion = usePrefersReducedMotion();
  const [webgl] = useState(isWebGLAvailable);
  const useOrbit = webgl && !reducedMotion;

  const content = (
    <div className="hero__readability-layer">
      <HeroContent />
    </div>
  );

  return (
    <section className="hero" id="top" aria-labelledby="hero-title" data-background={useOrbit ? 'orbit-space' : 'static'}>
      {useOrbit ? (
        <OrbitErrorBoundary fallback={content}>
          <Suspense fallback={content}>
            <OrbitBackdrop>{content}</OrbitBackdrop>
          </Suspense>
        </OrbitErrorBoundary>
      ) : (
        content
      )}
    </section>
  );
}

function HeroContent() {
  const goTo = useSectionNavigation();
  return (
    <>
      <div className="hero__body container">
        <div className="hero__content">
          <p className="hero__eyebrow mono-label">SINGLE-AXIS ATTITUDE CONTROL / 2U CUBESAT</p>
          <h1 className="hero__title" id="hero-title">
            Control the attitude.
          </h1>
          <p className="hero__lead">
            A desktop CubeSat that estimates its orientation and controls rotation using a reaction
            wheel.
          </p>
          <div className="hero__actions">
            <Link to={GROUND_STATION_PATH} className="button button--primary hero__cta">
              Open Ground Station
              <ArrowUpRight strokeWidth={1.75} aria-hidden />
            </Link>
            <a href={landingHref('system')} onClick={goTo('system')} className="button button--secondary hero__cta">
              Explore the system
              <ArrowDown strokeWidth={1.75} aria-hidden />
            </a>
          </div>
        </div>
      </div>
      <div className="hero__footer container">
        <p className="hero__descriptor mono-label">
          {'ESP32-S3  /  INERTIAL + MAGNETIC + OPTICAL SENSING  /  REACTION WHEEL'}
        </p>
        <p className="hero__scroll mono-label" aria-hidden="true">
          SCROLL TO EXPLORE
        </p>
      </div>
    </>
  );
}

interface BoundaryProps {
  fallback: ReactNode;
  children: ReactNode;
}

class OrbitErrorBoundary extends Component<BoundaryProps, { failed: boolean }> {
  state = { failed: false };

  static getDerivedStateFromError() {
    return { failed: true };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.warn('OrbitSpace background unavailable; using the static hero.', error, info.componentStack);
  }

  render() {
    return this.state.failed ? this.props.fallback : this.props.children;
  }
}
