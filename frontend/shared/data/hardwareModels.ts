export interface HardwareView {
  readonly subject: readonly string[];
  readonly carrier: readonly string[];
  readonly focus?: readonly string[];
  readonly shows: string;
  readonly azimuthDeg: number;
  readonly elevationDeg: number;
  readonly frame: 'subject' | 'stack';
  readonly zoom: number;
}

const screws = (board: string) => [0, 1, 2, 3].map((index) => `${board}_screw${index}`);

const PRINTED_FRAME = [
  'base_hub',
  'bottom_cap',
  'motor_deck',
  'hall_bracket',
  'flywheel_guard',
  'power_deck',
  'battery_deck',
  'battery_holder_body',
  'battery_cap_a',
  'battery_cap_b',
  'avionics_deck',
  'top_deck',
  'lock_pin',
];

export const hardwareViews: Readonly<Record<string, HardwareView>> = {
  frame: {
    subject: PRINTED_FRAME,
    carrier: [],
    shows: 'HIGHLIGHTED: THE PRINTED PARTS OF THE 2U FRAME. THREADED RODS, MOTOR, BOARDS AND CELLS ARE STAND-IN SHAPES',
    azimuthDeg: 38,
    elevationDeg: 20,
    frame: 'stack',
    zoom: 1,
  },
  esp32s3: {
    subject: ['xiao_s3', 'board_xiao_carrier'],
    carrier: ['avionics_deck'],
    focus: ['xiao_s3'],
    shows: 'HIGHLIGHTED: XIAO ESP32-S3 AND CARRIER BOARD ON THE AVIONICS DECK. ELECTRONICS DETAIL IS ILLUSTRATIVE',
    azimuthDeg: 350,
    elevationDeg: 32,
    frame: 'subject',
    zoom: 1.4,
  },
  mpu6050: {
    subject: ['mpu6050', ...screws('mpu6050')],
    carrier: ['avionics_deck'],
    shows: 'HIGHLIGHTED: MPU-6050 BOARD ON ITS STANDOFFS, CENTRED ON THE SPIN AXIS. BOARD DETAIL IS ILLUSTRATIVE',
    azimuthDeg: 12,
    elevationDeg: 40,
    frame: 'subject',
    zoom: 3,
  },
  qmc5883p: {
    subject: ['qmc5883p', ...screws('qmc5883p')],
    carrier: ['top_deck'],
    shows: 'HIGHLIGHTED: QMC5883P BOARD BELOW THE TOP DECK. BOARD DETAIL IS ILLUSTRATIVE',
    azimuthDeg: 30,
    elevationDeg: -28,
    frame: 'subject',
    zoom: 2.8,
  },
  op550: {
    subject: ['avionics_deck'],
    carrier: [],
    shows: 'HIGHLIGHTED: THE AVIONICS DECK AND +X SUN-SENSOR BLOCK. OP550 BODIES ARE ILLUSTRATIVE, NOT PART OF THE CAD',
    azimuthDeg: 90,
    elevationDeg: 24,
    frame: 'subject',
    zoom: 1.15,
  },
  a1104: {
    subject: ['hall_bracket'],
    carrier: ['motor_deck'],
    shows: 'HIGHLIGHTED: THE HALL BRACKET AT 45 DEGREES ON THE MOTOR DECK. THE A1104 BODY IS ILLUSTRATIVE, NOT PART OF THE CAD',
    azimuthDeg: 72,
    elevationDeg: 42,
    frame: 'subject',
    zoom: 2.4,
  },
  'reaction-wheel': {
    subject: ['motor_2212', 'washer_flywheel', 'shaft_and_nuts'],
    carrier: ['motor_deck'],
    shows: 'HIGHLIGHTED: 2212 MOTOR WITH ILLUSTRATIVE DETAIL AND A WASHER-STACK FLYWHEEL STAND-IN. THE PRINTED FLYWHEEL IS NOT MODELLED YET',
    azimuthDeg: 40,
    elevationDeg: 22,
    frame: 'subject',
    zoom: 1.4,
  },
  esc: {
    subject: ['esc'],
    carrier: ['power_deck', 'board_power'],
    shows: 'HIGHLIGHTED: ESC ON THE +X SIDE OF THE POWER DECK. BODY AND LEADS ARE ILLUSTRATIVE',
    azimuthDeg: 70,
    elevationDeg: 34,
    frame: 'subject',
    zoom: 2.4,
  },
  power: {
    subject: [
      'aa_cells',
      'battery_holder_body',
      'battery_cap_a',
      'battery_cap_b',
      'contacts_cap_a',
      'contacts_cap_b',
      'foot_screw0',
      'foot_screw1',
      'board_power',
    ],
    carrier: ['battery_deck', 'power_deck'],
    shows: 'HIGHLIGHTED: EIGHT AA CELLS IN THE PRINTED HOLDER AND THE POWER-DECK PERFBOARD. BOARD DETAIL IS ILLUSTRATIVE',
    azimuthDeg: 48,
    elevationDeg: 24,
    frame: 'subject',
    zoom: 1.3,
  },
  bmp280: {
    subject: ['bmp280', ...screws('bmp280')],
    carrier: ['top_deck'],
    shows: 'HIGHLIGHTED: BMP280 BOARD BELOW THE TOP DECK. BOARD DETAIL IS ILLUSTRATIVE',
    azimuthDeg: 150,
    elevationDeg: -28,
    frame: 'subject',
    zoom: 2.8,
  },
};
