export const GROUND_STATION_PATH = '/ground-station';

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
