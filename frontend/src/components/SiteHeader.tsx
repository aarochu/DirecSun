import { ArrowUpRight, Menu, X } from 'lucide-react';
import { useEffect, useId, useRef, useState } from 'react';
import { Link } from 'react-router-dom';
import { GROUND_STATION_PATH, sectionLinks } from '../data/navigation';
import { useActiveSection, useScrolledPast, useSectionNavigation } from '../lib/navigation';

const SECTION_IDS = sectionLinks.map((link) => link.id);

export function SiteHeader() {
  const solid = useScrolledPast(72);
  const active = useActiveSection(SECTION_IDS);
  const goTo = useSectionNavigation();

  return (
    <header className={`site-header${solid ? ' is-solid' : ''}`} data-site-header>
      <div className="site-header__inner container">
        <a href="/" className="wordmark" onClick={goTo(null)} aria-label="DirecSun — overview">
          DirecSun
        </a>

        <nav className="site-nav" aria-label="Primary">
          {sectionLinks.map((link) => (
            <a
              key={link.id}
              href={`/#${link.id}`}
              onClick={goTo(link.id)}
              className={`site-nav__link${link.tablet ? '' : ' site-nav__link--wide'}`}
              aria-current={active === link.id ? 'location' : undefined}
            >
              {link.label}
            </a>
          ))}
          <Link to={GROUND_STATION_PATH} className="button button--primary header-cta">
            Ground Station
            <ArrowUpRight strokeWidth={1.75} aria-hidden />
          </Link>
        </nav>

        <div className="site-header__compact">
          <Link to={GROUND_STATION_PATH} className="button button--primary header-cta">
            Ground Station
            <ArrowUpRight strokeWidth={1.75} aria-hidden />
          </Link>
          <CompactMenu onNavigate={goTo} />
        </div>
      </div>
    </header>
  );
}

function CompactMenu({ onNavigate }: { onNavigate: ReturnType<typeof useSectionNavigation> }) {
  const [open, setOpen] = useState(false);
  const menuId = useId();
  const buttonRef = useRef<HTMLButtonElement>(null);
  const panelRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    panelRef.current?.querySelector('a')?.focus();

    const onKey = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      setOpen(false);
      buttonRef.current?.focus();
    };
    const onPointer = (event: PointerEvent) => {
      const target = event.target as Node;
      if (!panelRef.current?.contains(target) && !buttonRef.current?.contains(target)) setOpen(false);
    };
    const onResize = () => {
      if (window.innerWidth >= 640) setOpen(false);
    };
    document.addEventListener('keydown', onKey);
    document.addEventListener('pointerdown', onPointer);
    window.addEventListener('resize', onResize);
    return () => {
      document.removeEventListener('keydown', onKey);
      document.removeEventListener('pointerdown', onPointer);
      window.removeEventListener('resize', onResize);
    };
  }, [open]);

  return (
    <div className="compact-menu">
      <button
        ref={buttonRef}
        type="button"
        className="compact-menu__button"
        aria-expanded={open}
        aria-controls={menuId}
        onClick={() => setOpen((value) => !value)}
      >
        {open ? <X strokeWidth={1.75} aria-hidden /> : <Menu strokeWidth={1.75} aria-hidden />}
        <span className="compact-menu__text">Menu</span>
      </button>
      <div ref={panelRef} id={menuId} className="compact-menu__panel" hidden={!open}>
        <nav aria-label="Sections">
          <ul>
            {sectionLinks.map((link) => (
              <li key={link.id}>
                <a
                  href={`/#${link.id}`}
                  onClick={(event) => {
                    setOpen(false);
                    onNavigate(link.id)(event);
                  }}
                >
                  {link.label}
                </a>
              </li>
            ))}
          </ul>
        </nav>
      </div>
    </div>
  );
}
