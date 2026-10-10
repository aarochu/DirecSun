import { ArrowUpRight } from 'lucide-react';
import { Link } from 'react-router-dom';
import { SectionHeading } from '../components/SectionHeading';
import { GROUND_STATION_PATH } from '../data/navigation';
import { CubeSatSchematic } from '../ground/CubeSatSchematic';
import { HeadingDial } from '../ground/HeadingDial';
import { HeadingHistoryChart } from '../ground/HeadingHistoryChart';
import { TelemetryReadouts } from '../ground/TelemetryReadouts';
import { simulationTelemetry } from '../ground/displaySource';
import { createPreviewSnapshot } from '../simulation/preview';

const preview = createPreviewSnapshot();
const previewTelemetry = simulationTelemetry(preview);

export function GroundStationBridge() {
  return (
    <section className="section bridge" id="ground" tabIndex={-1} aria-labelledby="ground-title">
      <div className="container bridge__grid">
        <div className="bridge__text">
          <SectionHeading
            index="004 / GROUND STATION"
            title="See the response."
            titleId="ground-title"
            intro="The browser ground station displays estimated heading, angular rate, flywheel speed, and operating state. In demonstration mode, left/right commands can be previewed with a simulated CubeSat."
          />
          <Link to={GROUND_STATION_PATH} className="button button--primary bridge__cta">
            Open Ground Station
            <ArrowUpRight strokeWidth={1.75} aria-hidden />
          </Link>
          <p className="bridge__note mono-label">
            Demo preview uses simulated telemetry until hardware is connected.
          </p>
        </div>

        <figure className="bridge__figure">
          <div className="preview" aria-hidden="true">
            <div className="preview__bar">
              <span>GROUND STATION</span>
              <span className="preview__status">SIMULATED — NO DEVICE CONNECTION</span>
            </div>
            <div className="preview__body">
              <div className="preview__model">
                <CubeSatSchematic headingDeg={preview.state.headingDeg} />
                <div className="preview__dial">
                  <HeadingDial
                    headingDeg={preview.state.headingDeg}
                    targetDeg={preview.state.targetDeg}
                    originDeg={preview.state.originDeg}
                  />
                </div>
                <span className="preview__caption">SCHEMATIC MODEL — CAD NOT LOADED</span>
              </div>
              <div className="preview__rail">
                <TelemetryReadouts telemetry={previewTelemetry} />
                <HeadingHistoryChart telemetry={previewTelemetry} height={150} />
              </div>
            </div>
          </div>
          <figcaption className="mono-label">
            STATIC PREVIEW / SIMULATED: LEFT 90° SEQUENCE, THEN RIGHT 90° HOLDING TARGET
          </figcaption>
        </figure>
      </div>
    </section>
  );
}
