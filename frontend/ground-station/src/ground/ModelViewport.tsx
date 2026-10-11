import { lazy, Suspense, useState, useSyncExternalStore } from 'react';
import { groundStationModel } from '@shared/data/assets';
import { formatHeading } from '../lib/format';
import { isWebGLAvailable } from '../lib/environment';
import type { DisplaySource } from './displaySource';
import { CubeSatSchematic } from './CubeSatSchematic';
import { HeadingDial } from './HeadingDial';

const RealCubeSatModel = lazy(() => import('./RealCubeSatModel'));

type ModelStatus = 'schematic' | 'loading' | 'cad' | 'failed';

const MODEL_LABEL: Record<ModelStatus, string> = {
  schematic: 'SCHEMATIC MODEL — CAD NOT LOADED',
  loading: 'LOADING CAD MODEL',
  cad: 'CAD STACK — CAD/ASSEMBLY.PY',
  failed: 'SCHEMATIC MODEL — CAD NOT LOADED',
};

export function ModelViewport({ source }: { source: DisplaySource }) {
  const state = useSyncExternalStore(source.subscribeFrame, source.getFrame);
  const [status, setStatus] = useState<ModelStatus>(() =>
    groundStationModel.available && isWebGLAvailable() ? 'loading' : 'schematic',
  );
  const tryCad = status === 'loading' || status === 'cad';

  return (
    <section className="viewport" aria-label="CubeSat orientation model">
      <div className="viewport__stage">
        {tryCad ? (
          <Suspense fallback={null}>
            <RealCubeSatModel
              asset={groundStationModel}
              headingDeg={state.headingDeg}
              onLoad={() => setStatus('cad')}
              onError={() => setStatus('failed')}
            />
          </Suspense>
        ) : (
          <CubeSatSchematic headingDeg={state.headingDeg} />
        )}
      </div>

      <div className="viewport__legend mono-label" aria-hidden="true">
        <span>
          <svg className="legend-marker legend-marker--target" viewBox="0 0 10 9">
            <polygon points="5,8 0.5,1 9.5,1" />
          </svg>
          TARGET
        </span>
        <span>
          <svg className="legend-marker legend-marker--start" viewBox="0 0 10 9">
            <polygon points="5,8 0.5,1 9.5,1" />
          </svg>
          START
        </span>
      </div>
      <div className="viewport__dial">
        <HeadingDial headingDeg={state.headingDeg} targetDeg={state.targetDeg} originDeg={state.originDeg} />
      </div>

      <ViewportReadout source={source} />

      <p className="viewport__corner viewport__corner--left mono-label">+Z / YAW AXIS</p>
      <p className="viewport__corner viewport__corner--right mono-label">
        {MODEL_LABEL[status]}
        {status === 'failed' && <span className="viewport__error"> / CAD FILE FAILED TO LOAD</span>}
      </p>
    </section>
  );
}

function ViewportReadout({ source }: { source: DisplaySource }) {
  const telemetry = useSyncExternalStore(source.subscribeTelemetry, source.getTelemetry);
  return (
    <p className="viewport__readout mono-label" aria-hidden="true">
      <span>HDG {formatHeading(telemetry.headingDeg)}</span>
      <span className="viewport__readout-target">TGT {formatHeading(telemetry.targetDeg)}</span>
      <span>{telemetry.statusWord}</span>
    </p>
  );
}
