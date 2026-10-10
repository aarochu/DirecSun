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

const pending = (
  kind: AssetKind,
  src: string,
  partName: string,
  alt: string,
): ImageAsset => ({ kind, src, partName, alt, available: false, provenance: null });

export const imageAssets = {
  assemblyRender: pending(
    'cad-assembly',
    '/assets/direcsun-assembly.webp',
    'DirecSun 2U frame assembly',
    'CAD assembly preview of the DirecSun 2U frame',
  ),
  xiaoEsp32s3: pending(
    'component-photo',
    '/assets/xiao-esp32s3.webp',
    'Seeed Studio XIAO ESP32-S3',
    'Seeed Studio XIAO ESP32-S3 board',
  ),
  mpu6050: pending(
    'component-photo',
    '/assets/mpu6050.webp',
    'Adafruit MPU-6050 #3886',
    'Adafruit MPU-6050 breakout board, product 3886',
  ),
  qmc5883p: pending(
    'component-photo',
    '/assets/qmc5883p.webp',
    'Adafruit QMC5883P #6388',
    'Adafruit QMC5883P magnetometer breakout, product 6388',
  ),
  op550: pending(
    'component-photo',
    '/assets/op550.webp',
    'OP550 phototransistor',
    'OP550 phototransistor',
  ),
  a1104: pending(
    'component-photo',
    '/assets/a1104.webp',
    'A1104 Hall-effect switch',
    'A1104 Hall-effect switch',
  ),
  motor2212: pending(
    'component-photo',
    '/assets/motor-2212.webp',
    '2212 920 KV brushless motor',
    '2212 brushless outrunner motor',
  ),
  esc: pending('component-photo', '/assets/blheli-esc.webp', 'BLHeli_S ESC', 'BLHeli_S electronic speed controller'),
  powerDeck: pending(
    'cad-component',
    '/assets/power-deck.webp',
    'DirecSun power deck',
    'CAD view of the DirecSun battery deck and holder',
  ),
  bmp280: pending(
    'component-photo',
    '/assets/bmp280.webp',
    'Adafruit BMP280 #2651',
    'Adafruit BMP280 breakout board, product 2651',
  ),
} as const satisfies Record<string, ImageAsset>;

export type ImageAssetId = keyof typeof imageAssets;

export function captionFor(asset: ImageAsset): string {
  switch (asset.kind) {
    case 'cad-assembly':
      return 'CAD ASSEMBLY PREVIEW / DESIGN MODEL — NOT BUILT';
    case 'cad-component':
      return 'CAD COMPONENT VIEW / DESIGN MODEL';
    case 'component-photo':
      return `COMPONENT REFERENCE / ${asset.partName.toUpperCase()}`;
  }
}
