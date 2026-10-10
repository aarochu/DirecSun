import { useEffect } from 'react';
import { useLocation } from 'react-router-dom';
import { SiteFooter } from '../components/SiteFooter';
import { SiteHeader } from '../components/SiteHeader';
import { ControlExplainer } from '../landing/ControlExplainer';
import { GroundStationBridge } from '../landing/GroundStationBridge';
import { HardwareBreakdown } from '../landing/HardwareBreakdown';
import { LandingHero } from '../landing/LandingHero';
import { SystemOverview } from '../landing/SystemOverview';
import type { ScrollIntent } from '../lib/navigation';
import { scrollToSection, scrollToY } from '../lib/scroll';
import '../styles/landing.css';

export default function LandingPage() {
  const location = useLocation();

  useEffect(() => {
    const smooth = (location.state as ScrollIntent | null)?.smooth === true;
    const id = decodeURIComponent(location.hash.slice(1));
    if (!id && !smooth) return;
    const frame = requestAnimationFrame(() => {
      if (id) scrollToSection(id, smooth);
      else if (smooth) scrollToY(0, true);
    });
    return () => cancelAnimationFrame(frame);
  }, [location.key, location.hash, location.state]);

  return (
    <>
      <SiteHeader />
      <main id="main">
        <LandingHero />
        <SystemOverview />
        <HardwareBreakdown />
        <ControlExplainer />
        <GroundStationBridge />
      </main>
      <SiteFooter />
    </>
  );
}
