import { ChevronDown } from 'lucide-react';
import { useId, useState, useSyncExternalStore } from 'react';
import type { SensorReadings } from '../hardware/deviceLink';
import { formatHeading, formatRate, formatSigned } from '../lib/format';
import type { DisplaySource } from './displaySource';

// The box's own sensor values, one group per part (protocol/schema-v1.json
// "tlm"). The simulation models motion, not sensors, so without a device the
// panel says so instead of showing numbers.

const STORAGE_KEY = 'direcsun.sensors.open';

function readOpen(): boolean {
  try {
    return window.localStorage.getItem(STORAGE_KEY) !== 'false';
  } catch {
    return true;
  }
}

function writeOpen(open: boolean) {
  try {
    window.localStorage.setItem(STORAGE_KEY, String(open));
  } catch {
    // Storage blocked: the panel still works, it just forgets.
  }
}

const dash = '—';
const num = (value: number | null, format: (v: number) => string) => (value === null ? dash : format(value));
const xyz = (value: readonly [number, number, number]) => value.map((v) => Math.round(v)).join(' / ');

interface Row {
  label: string;
  value: string;
  note?: string;
}

interface Group {
  part: string;
  role: string;
  rows: Row[];
}

function groups(s: SensorReadings): Group[] {
  const optional = (row: Row | null): Row[] => (row ? [row] : []);
  return [
    {
      part: 'MPU-6050',
      role: 'gyro',
      rows: [
        { label: 'Body rate', value: num(s.rateDps, formatRate) },
        ...optional(s.gyroRaw && { label: 'Raw X / Y / Z', value: xyz(s.gyroRaw), note: 'counts' }),
        ...optional(s.accelRmsG !== null ? { label: 'Vibration', value: `${s.accelRmsG.toFixed(3)} g`, note: 'RMS' } : null),
      ],
    },
    {
      part: 'QMC5883P',
      role: 'compass',
      rows: [
        { label: 'Heading', value: num(s.magDeg, formatHeading), note: s.magDeg === null ? 'not reporting' : undefined },
        ...optional(s.magRaw && { label: 'Raw X / Y / Z', value: xyz(s.magRaw), note: 'counts' }),
      ],
    },
    {
      part: 'OP550 ×2',
      role: 'sun sensor',
      rows: [
        {
          label: 'Sun angle',
          value: num(s.sunDeg, (v) => `${formatSigned(v)}°`),
          note: s.sunValid === null ? undefined : s.sunValid ? 'lamp seen' : 'no lamp',
        },
        ...optional(
          s.sunAmV !== null && s.sunBmV !== null
            ? { label: 'A / B', value: `${Math.round(s.sunAmV)} / ${Math.round(s.sunBmV)}`, note: 'mV' }
            : null,
        ),
      ],
    },
    {
      part: 'A1104',
      role: 'Hall switch',
      rows: [{ label: 'Wheel speed', value: num(s.wheelRpm, (v) => `${Math.round(v)} RPM`) }],
    },
    {
      part: '100k / 22k divider',
      role: 'pack voltage',
      rows: [{ label: 'Pack', value: num(s.batteryV, (v) => `${v.toFixed(2)} V`), note: '1 s average' }],
    },
    {
      part: 'BMP280',
      role: 'temp + pressure',
      rows: [
        { label: 'Temperature', value: num(s.tempC, (v) => `${v.toFixed(1)} °C`), note: s.tempC === null ? 'not fitted' : undefined },
        { label: 'Pressure', value: num(s.pressurePa, (v) => `${(v / 100).toFixed(1)} hPa`) },
      ],
    },
    {
      part: 'ESP32-S3',
      role: 'control loop',
      rows: [{ label: 'Longest tick', value: num(s.loopUs, (v) => `${Math.round(v)} µs`), note: 'of 2000' }],
    },
  ];
}

export function SensorPanel({ source }: { source: DisplaySource }) {
  const telemetry = useSyncExternalStore(source.subscribeTelemetry, source.getTelemetry);
  const [open, setOpen] = useState(readOpen);
  const bodyId = useId();
  const sensors = telemetry.sensors;

  const toggle = () => {
    setOpen((value) => {
      writeOpen(!value);
      return !value;
    });
  };

  return (
    <section className={`sensors${open ? ' is-open' : ''}`} aria-labelledby={`${bodyId}-title`}>
      <h2 className="sensors__heading">
        <button type="button" className="sensors__toggle" aria-expanded={open} aria-controls={bodyId} onClick={toggle}>
          <span className="mono-label" id={`${bodyId}-title`}>
            SENSOR READINGS
          </span>
          <span className="sensors__summary mono-label">{sensors ? 'FROM DEVICE' : 'NO DEVICE'}</span>
          <ChevronDown className="sensors__chevron" aria-hidden="true" size={16} />
        </button>
      </h2>
      <div className="sensors__body" id={bodyId} hidden={!open}>
        {sensors ? (
          <div className="sensors__groups">
            {groups(sensors).map((group) => (
              <dl className="sensors__group" key={group.part}>
                <div className="sensors__part">
                  <dt className="mono-label">{group.part}</dt>
                  <dd className="mono-label">{group.role}</dd>
                </div>
                {group.rows.map((row) => (
                  <div className="sensors__row" key={row.label}>
                    <dt>{row.label}</dt>
                    <dd>
                      <span className="sensors__value">{row.value}</span>
                      {row.note && <span className="sensors__note">{row.note}</span>}
                    </dd>
                  </div>
                ))}
              </dl>
            ))}
          </div>
        ) : (
          <p className="sensors__empty">
            Sensor values come straight from the box. Connect a device above to see the gyro, compass, sun sensor, wheel
            speed, pack and BMP280 readings.
          </p>
        )}
      </div>
    </section>
  );
}
