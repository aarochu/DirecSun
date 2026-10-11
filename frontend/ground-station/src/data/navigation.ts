export const GROUND_STATION_PATH = '/';
export const LANDING_PATH = '/overview';

const base = import.meta.env.BASE_URL.replace(/\/$/, '');
export const landingHref = (id?: string) => `${base}${LANDING_PATH}${id ? `#${id}` : ''}`;
export const SITE_URL = import.meta.env.DEV ? 'http://localhost:5173/' : '/';

export interface SectionLinkItem {
  readonly id: 'system' | 'hardware' | 'control';
  readonly label: string;
  readonly tablet: boolean;
}

export const sectionLinks: readonly SectionLinkItem[] = [
  { id: 'system', label: 'System', tablet: true },
  { id: 'hardware', label: 'Hardware', tablet: true },
  { id: 'control', label: 'Control', tablet: false },
];
