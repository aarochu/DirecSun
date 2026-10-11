import { CornerLeftUp } from 'lucide-react';

const SENSOR_INPUTS = [
  { name: 'MPU6050', role: 'ANGULAR RATE' },
  { name: 'QMC5883P', role: 'HEADING REFERENCE' },
  { name: 'OP550 ×2', role: 'LIGHT DIRECTION' },
  { name: 'A1104', role: 'WHEEL RPM' },
] as const;

type NodeVariant = 'default' | 'primary' | 'reference' | 'effect';

function Node({
  name,
  role,
  variant = 'default',
  className = '',
}: {
  name: string;
  role?: string;
  variant?: NodeVariant;
  className?: string;
}) {
  return (
    <div className={`node node--${variant} ${className}`}>
      <span className="node__name">{name}</span>
      {role && <span className="node__role">{role}</span>}
    </div>
  );
}

function Wire({
  label,
  kind = 'signal',
  bidirectional = false,
  className = '',
}: {
  label?: string;
  kind?: 'signal' | 'power';
  bidirectional?: boolean;
  className?: string;
}) {
  return (
    <div className={`wire wire--${kind}${bidirectional ? ' wire--both' : ''} ${className}`}>
      <span className="wire__line" aria-hidden="true" />
      {label && <span className="wire__label">{label}</span>}
    </div>
  );
}

export function SystemDiagram() {
  return (
    <figure className="sysdiag" aria-labelledby="sysdiag-caption">
      <figcaption id="sysdiag-caption" className="sr-only">
        Functional architecture. Sensor inputs feed the ESP32-S3, which sends DShot commands to the
        ESC; the ESC drives the 2212 motor and printed flywheel, whose reaction torque yaws the body.
        Gyro, compass and Hall feedback return to the sensor inputs. The BMP280 is an optional
        telemetry branch, not a control input. Power runs from the battery through a 10 A fuse to the
        ESC and a 5 V buck converter.
      </figcaption>

      <div className="sysdiag__legend" aria-hidden="true">
        <span>
          <i className="swatch swatch--signal" />
          SIGNAL
        </span>
        <span>
          <i className="swatch swatch--feedback" />
          FEEDBACK
        </span>
        <span>
          <i className="swatch swatch--power" />
          POWER
        </span>
      </div>

      <div className="sysdiag__band" role="group" aria-labelledby="band-signal">
        <p className="sysdiag__band-label mono-label" id="band-signal">
          SIGNAL / CONTROL
        </p>
        <div className="sysdiag__signal">
          <ul className="sysdiag__inputs" aria-label="Sensor inputs">
            {SENSOR_INPUTS.map((input) => (
              <li key={input.name}>
                <Node name={input.name} role={input.role} />
              </li>
            ))}
          </ul>
          <div className="wire wire--signal bus sysdiag__bus" aria-hidden="true">
            <span className="wire__line" />
            <span className="bus__trunk" />
            <span className="bus__stubs">
              <i />
              <i />
              <i />
              <i />
            </span>
          </div>
          <Node className="sysdiag__esp" variant="primary" name="ESP32-S3" role="STATE ESTIMATION + CONTROL" />
          <Wire className="sysdiag__w2" label="DSHOT" />
          <Node className="sysdiag__esc" name="ESC" role="BLHELI_S" />
          <Wire className="sysdiag__w3" label="3 PHASES" />
          <Node className="sysdiag__motor" name="2212 MOTOR + PRINTED FLYWHEEL" />
          <Wire className="sysdiag__w4" />
          <div className="sysdiag__effect">
            <Node variant="effect" name="REACTION TORQUE → BODY YAW" />
          </div>
          <div className="sysdiag__feedback">
            <span className="fb__start" aria-hidden="true">
              <i />
            </span>
            <span className="fb__span">
              <span className="fb__label">
                <CornerLeftUp className="fb__icon" strokeWidth={1.75} aria-hidden />
                GYRO + COMPASS + HALL FEEDBACK
                <span className="fb__note"> — returns to the sensor inputs</span>
              </span>
            </span>
            <span className="fb__end" aria-hidden="true" />
          </div>
        </div>
      </div>

      <div className="sysdiag__band" role="group" aria-labelledby="band-telemetry">
        <p className="sysdiag__band-label mono-label" id="band-telemetry">
          TELEMETRY / NOT A YAW-CONTROL INPUT
        </p>
        <div className="sysdiag__row sysdiag__telemetry">
          <Node className="t-bmp" name="BMP280" role="OPTIONAL TELEMETRY" />
          <Wire className="t-w1" label="I²C" />
          <Node className="t-esp" variant="reference" name="ESP32-S3" role="TELEMETRY SERVICE" />
          <Wire className="t-w2" label="WI-FI" bidirectional />
          <Node className="t-gs" name="GROUND STATION" role="LAPTOP BROWSER / WEBSOCKET" />
        </div>
      </div>

      <div className="sysdiag__band" role="group" aria-labelledby="band-power">
        <p className="sysdiag__band-label mono-label" id="band-power">
          POWER
        </p>
        <div className="sysdiag__power">
          <Node className="p-batt" name="8 × AA NIMH" role="9.6 V NOMINAL" />
          <Wire className="p-w1" kind="power" />
          <Node className="p-fuse" name="10 A FUSE" />
          <div className="split p-split" aria-hidden="true">
            <i className="split__a" />
            <i className="split__b" />
            <i className="split__c" />
            <i className="split__d" />
          </div>
          <Node className="p-esc" variant="reference" name="ESC" role="MOTOR SUPPLY" />
          <Node className="p-buck" name="5 V BUCK" role="REGULATED SUPPLY" />
          <Wire className="p-w3" kind="power" />
          <Node className="p-esp" variant="reference" name="ESP32-S3" role="5 V IN" />
          <Wire className="p-w4" kind="power" />
          <Node className="p-sensors" name="SENSOR BOARDS" role="3.3 V SENSOR RAIL VIA ESP32" />
        </div>
      </div>
    </figure>
  );
}
