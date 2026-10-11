import { Link } from 'react-router-dom';
import { GROUND_STATION_PATH, landingHref } from '../data/navigation';
import { useSectionNavigation } from '../lib/navigation';

export function SiteFooter() {
  const goTo = useSectionNavigation();
  return (
    <footer className="site-footer">
      <div className="site-footer__inner container">
        <p className="site-footer__statement">
          DirecSun — Single-axis reaction wheel attitude trainer
        </p>
        <nav className="site-footer__links" aria-label="Footer">
          <a href={landingHref('system')} onClick={goTo('system')}>
            System
          </a>
          <a href={landingHref('hardware')} onClick={goTo('hardware')}>
            Hardware
          </a>
          <Link to={GROUND_STATION_PATH}>Ground Station</Link>
        </nav>
        <p className="site-footer__note mono-label">Desktop demonstration / not flight hardware</p>
      </div>
    </footer>
  );
}
