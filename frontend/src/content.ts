// Copy for the landing page, kept short and plain. Part names and media come
// from ./data. Keep the claims within what docs/ says.

export { hardwareParts } from '@shared/data/hardware';
export { hardwareViews } from '@shared/data/hardwareModels';
export { cubesatModel, hardwareStack, imageAssets } from '@shared/data/assets';

export const hero = {
  title: 'Control the attitude.',
  lead: 'A desk-sized CubeSat that turns 90° on command, pauses, and comes back to where it started, steered by a reaction wheel inside.',
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

// Three quick takeaways per landing-page component, keyed by hardwareParts id.
export const componentBullets: Record<string, readonly [string, string, string]> = {
  frame: [
    'The printed stack carries the boards, battery, and motor.',
    'Two bearings let the body turn around a bolt that stays put.',
    'That free yaw axis is where the wheel’s reaction torque shows up.',
  ],
  esp32s3: [
    'It brings sensor readings together and decides what the wheel should do.',
    'D8 talks to the ESC; D10 listens for flywheel speed pulses.',
    'It also sends readings to your browser and enforces the safety stops.',
  ],
  mpu6050: [
    'Its gyro tells us how quickly the body is turning.',
    'It shares the I²C bus with the compass and pressure sensor.',
    'That fast rate reading is the controller’s first clue during a turn.',
  ],
  qmc5883p: [
    'This compass gives the box a longer-term heading reference.',
    'It rides the same I²C bus, away from the motor.',
    'After calibration, it can correct the gyro’s slow drift.',
  ],
  op550: [
    'Two angled light sensors compare how bright each side is.',
    'Their voltages reach the XIAO on D0 and D1.',
    'After calibration, that difference can point the box toward a lamp.',
  ],
  a1104: [
    'It spots the magnets passing on the spinning flywheel.',
    'Each pulse reaches D10, giving the controller wheel-speed feedback.',
    'The same reading helps catch a wheel that runs too fast.',
  ],
  'reaction-wheel': [
    'The motor speeds the washer-filled flywheel up or down.',
    'Three phase wires bring motor power from the ESC.',
    'Change the wheel’s speed and the body turns the other way.',
  ],
  esc: [
    'It turns digital DShot requests into three-phase motor drive.',
    'Battery power arrives through the fuse; D8 carries the signal.',
    'The Hall switch supplies the RPM reading that throttle alone cannot.',
  ],
  power: [
    'Eight rechargeable AAs are the planned untethered supply.',
    'After a fuse, the pack feeds the ESC and a 5 V regulator.',
    'A voltage divider lets the box refuse to arm a low pack.',
  ],
  bmp280: [
    'It measures room temperature and air pressure.',
    'It shares the I²C bus with the gyro and compass.',
    'Those readings are optional telemetry, not steering inputs.',
  ],
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
