// Copy for the landing page, kept short and plain. Part names and media come
// from ./data. Keep the claims within what docs/ says.

export { hardwareParts } from '@shared/data/hardware';
export { hardwareViews } from '@shared/data/hardwareModels';
export { cubesatModel, hardwareStack, imageAssets } from '@shared/data/assets';

export const hero = {
  title: 'Control the attitude.',
  lead: 'A desk-sized CubeSat that turns on command, holds its heading, and returns to where it started using an internal reaction wheel.',
  primary: 'Open Ground Station',
  secondary: 'See how it works',
  scroll: 'SCROLL',
} as const;

export const system = {
  index: '001',
  title: 'One axis, one wheel.',
  intro: 'Speed the wheel up one way and the body turns the other. That’s how it steers.',
} as const;

export const hardware = {
  index: '002',
  title: 'What’s inside.',
  intro: 'Here’s what each part does.',
} as const;

// One line per part, keyed by hardwareParts id.
export const blurbs: Record<string, string> = {
  frame: 'The 3D-printed body. It spins on two bearings around a fixed bolt.',
  esp32s3: 'The brain. It reads the sensors, works out how to turn, and sends data to your browser.',
  mpu6050: 'Measures how fast the body is spinning.',
  qmc5883p: 'A compass. It gives the heading.',
  op550: 'Two light sensors that show which side the lamp is on.',
  a1104: 'Counts magnets on the flywheel to measure its speed.',
  'reaction-wheel': 'The motor and flywheel. Spin it up and the body turns the other way.',
  esc: 'Turns the brain’s commands into motor power.',
  power: 'Eight AA batteries power everything.',
  bmp280: 'Optional extra: temperature and pressure.',
};

// `parts` names the CAD solids each step highlights (the avionics deck carries
// the OP550 block, per src/data/hardwareModels.ts).
export const control = {
  index: '003',
  title: 'Sense. Think. Turn.',
  steps: [
    { title: 'Sense', body: 'Check how it’s spinning and where it’s pointing.', parts: ['mpu6050', 'qmc5883p', 'avionics_deck', 'hall_bracket'] },
    { title: 'Think', body: 'Work out how far off it is.', parts: ['xiao_s3', 'board_xiao_carrier'] },
    { title: 'Turn', body: 'Nudge the wheel. Then do it all again.', parts: ['esc', 'motor_2212', 'washer_flywheel', 'shaft_and_nuts'] },
  ],
} as const;

export const ground = {
  index: '004',
  title: 'Try it yourself.',
  intro: 'Open the ground station to see where it’s pointing and send it left or right. If the hardware isn’t connected, it runs a simulated one.',
  cta: 'Open Ground Station',
  // The model runs the judge sequence from WORKSTREAMS.md; an illustration, not telemetry.
  sequence: 'ILLUSTRATION',
} as const;

export const footer = {
  statement: 'DirecSun, a single-axis reaction wheel trainer',
} as const;

// src/data/navigation.ts
export const sectionLinks = [
  { id: 'system', label: 'System' },
  { id: 'hardware', label: 'Hardware' },
  { id: 'control', label: 'Control' },
] as const;

// The ground station is the React app in frontend/ground-station. In production
// it is served at /ground-station/; in development it runs on port 5174.
export const GROUND_STATION_URL = import.meta.env.DEV ? 'http://localhost:5174/' : '/ground-station/';
