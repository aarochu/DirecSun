export type AssetKind = 'cad-assembly' | 'cad-component' | 'component-photo';

export interface ImageAsset {
  readonly kind: AssetKind;
  readonly src: string;
  readonly available: boolean;
  readonly partName: string;
  readonly alt: string;
  readonly provenance: string | null;
}

export interface ModelAsset {
  readonly src: string;
  readonly available: boolean;
  readonly sourceFile: string | null;
  readonly fixedNodePattern: RegExp;
  readonly yawOffsetDeg: number;
}

export const cubesatModel: ModelAsset = {
  src: '/assets/direcsun.glb',
  available: false,
  sourceFile: null,
  fixedNodePattern: /^(base|fixed|hub|bolt|stand)/i,
  yawOffsetDeg: 0,
};

const CAD_SOURCE = 'cad/assembly.py (named solids of the assembly model)';

const cadView = (kind: AssetKind, src: string, partName: string, alt: string): ImageAsset => ({
  kind,
  src,
  partName,
  alt,
  available: true,
  provenance: CAD_SOURCE,
});

export const hardwareStack = {
  src: '/assets/hardware-stack.glb',
} as const;

export const imageAssets = {
  assemblyRender: cadView(
    'cad-assembly',
    '/assets/hardware/frame.webp',
    'DirecSun 2U frame assembly',
    'CAD view of the full DirecSun stack with the printed 2U frame highlighted',
  ),
  xiaoEsp32s3: cadView(
    'cad-component',
    '/assets/hardware/esp32s3.webp',
    'Seeed Studio XIAO ESP32-S3',
    'CAD view of the XIAO ESP32-S3 carrier board on the avionics deck',
  ),
  mpu6050: cadView(
    'cad-component',
    '/assets/hardware/mpu6050.webp',
    'Adafruit MPU-6050 #3886',
    'CAD view of the MPU-6050 board on its standoffs on the avionics deck',
  ),
  qmc5883p: cadView(
    'cad-component',
    '/assets/hardware/qmc5883p.webp',
    'Adafruit QMC5883P #6388',
    'CAD view of the QMC5883P board hanging below the top deck',
  ),
  op550: cadView(
    'cad-component',
    '/assets/hardware/op550.webp',
    'OP550 phototransistor',
    'CAD view of the avionics deck and its sun-sensor block',
  ),
  a1104: cadView(
    'cad-component',
    '/assets/hardware/a1104.webp',
    'A1104 Hall-effect switch',
    'CAD view of the Hall bracket on the motor deck',
  ),
  motor2212: cadView(
    'cad-component',
    '/assets/hardware/reaction-wheel.webp',
    '2212 920 KV brushless motor',
    'CAD view of the 2212 motor and flywheel stand-in on the motor deck',
  ),
  esc: cadView(
    'cad-component',
    '/assets/hardware/esc.webp',
    'BLHeli_S ESC',
    'CAD view of the ESC stand-in on the power deck',
  ),
  powerDeck: cadView(
    'cad-component',
    '/assets/hardware/power.webp',
    'DirecSun power deck',
    'CAD view of the AA battery holder and power-deck perfboard',
  ),
  bmp280: cadView(
    'cad-component',
    '/assets/hardware/bmp280.webp',
    'Adafruit BMP280 #2651',
    'CAD view of the BMP280 board hanging below the top deck',
  ),
} as const satisfies Record<string, ImageAsset>;

export type ImageAssetId = keyof typeof imageAssets;

export function captionFor(asset: ImageAsset): string {
  switch (asset.kind) {
    case 'cad-assembly':
      return 'CAD ASSEMBLY PREVIEW / DESIGN MODEL';
    case 'cad-component':
      return 'CAD COMPONENT VIEW / DESIGN MODEL';
    case 'component-photo':
      return `COMPONENT REFERENCE / ${asset.partName.toUpperCase()}`;
  }
}
