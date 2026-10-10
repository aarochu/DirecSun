import type { ImageAssetId } from './assets';

export interface HardwarePart {
  readonly id: string;
  readonly index: string;
  readonly subsystem: string;
  readonly name: string;
  readonly func: string;
  readonly connection: string;
  readonly controlRole: string;
  readonly designNote: string;
  readonly specs: readonly string[];
  readonly media: ImageAssetId;
}

export const hardwareParts: readonly HardwarePart[] = [
  {
    id: 'frame',
    index: '01',
    subsystem: 'STRUCTURE',
    name: '3D-printed 2U frame',
    func: 'A stacked 100 × 100 × 227 mm structure carries the battery, controller, sensors, and motor. Two 608 bearings let the body turn around a fixed vertical M8 bolt.',
    connection:
      'The base hub and bolt remain stationary; the printed decks and their electronics turn together. Four #8-32 threaded rods clamp the deck stack.',
    controlRole:
      "Provides the single yaw axis. Because the structure can turn freely on bearings, reaction torque from the accelerating flywheel changes the body's heading.",
    designNote:
      'The bearing fit and friction must be measured before closed-loop tuning; this is a CAD design, not an as-built claim.',
    specs: ['2U / 100 × 100 × 227 mm', '2 × 608 BEARINGS', '1 YAW AXIS'],
    media: 'assemblyRender',
  },
  {
    id: 'esp32s3',
    index: '02',
    subsystem: 'FLIGHT COMPUTER',
    name: 'Seeed XIAO ESP32-S3',
    func: 'Reads sensors, estimates body motion and heading, runs feedback control, enforces safety interlocks, and publishes telemetry for the ground station.',
    connection:
      'D4/D5 carry the shared I²C bus. D0/D1 read the two light sensors, D10 receives Hall pulses, D8 sends DShot to the ESC, and D9 measures divided battery voltage. The ESP32 receives regulated power through the regulated 5 V path.',
    controlRole:
      'At the planned 500 Hz control rate, it converts estimated attitude and body rate into a wheel-speed reference and motor throttle command. Wi-Fi telemetry is handled separately at 50 Hz.',
    designNote: 'These task rates are design targets and are not benchmarked on assembled hardware.',
    specs: ['DUAL-CORE ESP32-S3', '500 HZ CONTROL TARGET', 'WI-FI / WEBSOCKET'],
    media: 'xiaoEsp32s3',
  },
  {
    id: 'mpu6050',
    index: '03',
    subsystem: 'INERTIAL SENSING',
    name: 'MPU6050 IMU',
    func: 'The three-axis gyroscope reports angular velocity; for this single-axis trainer, the Z-axis rate is the primary rotation measurement. Its accelerometer supports tilt checks and vibration testing.',
    connection:
      "Adafruit MPU-6050 #3886 joins the ESP32's shared 3.3 V STEMMA QT I²C bus, at expected address 0x68.",
    controlRole:
      'The controller uses gyro Z for fast body-rate feedback and integrates that rate into a short-term heading estimate. The estimator corrects long-term drift using the magnetometer.',
    designNote:
      'Verify board orientation and gyro sign during bring-up; wheel vibration can contaminate readings.',
    specs: ['GYRO + ACCEL', 'I²C 0x68', 'RATE FEEDBACK'],
    media: 'mpu6050',
  },
  {
    id: 'qmc5883p',
    index: '04',
    subsystem: 'MAGNETIC SENSING',
    name: 'QMC5883P magnetometer',
    func: 'Measures magnetic-field components. With calibration and a level frame, these measurements provide a heading reference for yaw.',
    connection:
      'Adafruit QMC5883P #6388 connects on the same 3.3 V STEMMA QT I²C bus; expected address 0x2C.',
    controlRole:
      'A complementary filter slowly pulls the gyro-integrated heading toward the magnetic heading, limiting drift without relying on a noisy compass for fast rotation.',
    designNote:
      'Motor magnets, currents, and nearby steel can bias readings. Validation is required at several wheel speeds.',
    specs: ['MAGNETIC FIELD', 'I²C 0x2C', 'HEADING CORRECTION'],
    media: 'qmc5883p',
  },
  {
    id: 'op550',
    index: '05',
    subsystem: 'OPTICAL SENSING',
    name: 'OP550 × 2 phototransistors',
    func: 'Two angled light-sensitive components measure relative illumination. Their normalized difference can indicate which direction the CubeSat should turn to face a lamp.',
    connection:
      'Each light sensor drives an ESP32 ADC1 input through an emitter-follower and load-resistor circuit: D0 and D1.',
    controlRole:
      'In Sun Point mode, the calibrated difference becomes an optical pointing error. It is a relative direction cue, not an absolute position measurement or camera image.',
    designNote:
      'The resistor choice and angular response must be calibrated with a real lamp before claiming pointing accuracy.',
    specs: ['2 ANALOG CHANNELS', 'NORMALIZED DIFFERENCE', 'SUN-POINT REFERENCE'],
    media: 'op550',
  },
  {
    id: 'a1104',
    index: '06',
    subsystem: 'SPEED FEEDBACK',
    name: 'A1104 Hall switch',
    func: 'Detects small magnets on the spinning motor bell. Timing the digital transitions estimates reaction-wheel rotational speed.',
    connection:
      'The Hall switch uses the 5 V rail, with its open-drain output pulled up to 3.3 V and connected to ESP32 D10.',
    controlRole:
      "Provides measured flywheel RPM for the inner wheel-speed feedback loop and the speed-limit checks. It measures wheel motion, not the CubeSat's compass heading.",
    designNote: 'The sensor-to-magnet gap and pulse count must be verified on the real hardware.',
    specs: ['DIGITAL PULSES', '5 V SUPPLY', 'WHEEL RPM'],
    media: 'a1104',
  },
  {
    id: 'reaction-wheel',
    index: '07',
    subsystem: 'ACTUATION',
    name: '2212 brushless motor + steel washer flywheel',
    func: 'The motor spins a clamped stack of steel fender washers. Increasing or decreasing their angular speed applies an opposing reaction torque to the CubeSat body.',
    connection:
      'Three motor phase wires connect to the BLHeli_S ESC; the washers mount mechanically to the motor bell/shaft hardware. Nothing 3D-printed is intended to spin with the wheel.',
    controlRole:
      'The wheel is the only actuator in the single-axis controller. The planned operating bias is 2400 RPM and the baseline cap is 4000 RPM, pending hardware characterization.',
    designNote:
      'Centring, balance, protective guard clearance, and nut retention are safety-critical; do not describe these speeds as demonstrated achievements.',
    specs: ['2212 / 920 KV', '2400 RPM BIAS — TARGET', '4000 RPM CAP — DESIGN'],
    media: 'motor2212',
  },
  {
    id: 'esc',
    index: '08',
    subsystem: 'MOTOR CONTROL',
    name: 'BLHeli_S ESC',
    func: 'Takes digital throttle commands from the flight computer and electronically commutates the three-phase brushless motor.',
    connection:
      'Battery power enters the ESC through the fused power branch; its three outputs drive the motor. ESP32 D8 supplies the DShot control signal and ground reference.',
    controlRole:
      "Translates the controller's wheel-speed demand into motor power. The ESP32 must use Hall feedback because stock BLHeli_S throttle is not a direct measured-torque or mechanical-RPM command.",
    designNote:
      'The DShot150/300 choice depends on the actual ESC MCU; brake-on-stop and signal-loss behavior require bench verification before judge interaction.',
    specs: ['BLHELI_S', 'DSHOT150 / 300 PENDING MCU', '3-PHASE MOTOR DRIVE'],
    media: 'esc',
  },
  {
    id: 'power',
    index: '09',
    subsystem: 'POWER',
    name: '8 × AA NiMH + power distribution',
    func: 'Eight rechargeable AA cells in series supply a nominal 9.6 V pack. The onboard power distribution splits motor power from the regulated electronics supply.',
    connection:
      'The pack feeds a 10 A fuse, then the ESC directly and a 5 V buck converter. The prescribed Schottky-protected path powers the XIAO, and its 3.3 V rail feeds sensor boards.',
    controlRole:
      'Untethered power allows the frame to turn without cable drag. A separate battery voltage divider gives firmware an undervoltage measurement for interlocks.',
    designNote:
      'The printed holder uses screw contacts whose resistance and retention must be tested under load.',
    specs: ['8 × AA NIMH', '9.6 V NOMINAL', '10 A FUSE'],
    media: 'powerDeck',
  },
  {
    id: 'bmp280',
    index: '10',
    subsystem: 'ENVIRONMENTAL TELEMETRY',
    name: 'BMP280',
    func: 'Measures ambient pressure and temperature as optional environmental data.',
    connection:
      'Adafruit BMP280 #2651 shares the STEMMA QT I²C bus; expected board address 0x77, to be verified at startup.',
    controlRole:
      'It is available for telemetry but is not an input to the single-axis heading controller or a source of CubeSat location.',
    designNote:
      'Do not display invented altitude estimates or simulated pressure in the default ground station.',
    specs: ['PRESSURE + TEMPERATURE', 'I²C 0x77 EXPECTED', 'OPTIONAL TELEMETRY'],
    media: 'bmp280',
  },
];
