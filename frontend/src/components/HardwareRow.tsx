import { ChevronDown } from 'lucide-react';
import { lazy, Suspense, useEffect, useState } from 'react';
import { captionFor, imageAssets } from '../data/assets';
import type { HardwarePart } from '../data/hardware';
import { hardwareViews } from '../data/hardwareModels';
import { isWebGLAvailable } from '../lib/environment';
import { useInView } from '../lib/useInView';
import { MissingAsset } from './MissingAsset';

const PartModel = lazy(() => import('./PartModel'));

interface HardwareRowProps {
  part: HardwarePart;
  open: boolean;
  onToggle: () => void;
}

export function HardwareRow({ part, open, onToggle }: HardwareRowProps) {
  const headingId = `part-${part.id}-heading`;
  const panelId = `part-${part.id}-panel`;
  return (
    <article className={`hardware-row${open ? ' is-open' : ''}`} aria-labelledby={headingId}>
      <h3 className="hardware-row__heading">
        <button
          type="button"
          className="hardware-row__toggle"
          aria-expanded={open}
          aria-controls={panelId}
          aria-labelledby={headingId}
          onClick={onToggle}
        >
          <span className="hardware-row__index mono-label">{part.index}</span>
          <span className="hardware-row__name" id={headingId}>
            {part.name}
          </span>
          <span className="hardware-row__subsystem mono-label">{part.subsystem}</span>
          <ChevronDown className="hardware-row__chevron" strokeWidth={1.75} aria-hidden />
        </button>
      </h3>

      <div className="hardware-row__panel" id={panelId} hidden={!open}>
        <HardwareMedia part={part} open={open} />
        <div className="hardware-row__content">
          <h4 className="hardware-row__label mono-label">FUNCTION</h4>
          <p className="hardware-row__summary">{part.func}</p>

          <div className="hardware-row__explanation">
            <div>
              <h4 className="hardware-row__label mono-label">CONNECTION</h4>
              <p>{part.connection}</p>
            </div>
            <div>
              <h4 className="hardware-row__label mono-label">CONTROL ROLE</h4>
              <p>{part.controlRole}</p>
            </div>
            <div className="hardware-row__note">
              <h4 className="hardware-row__label mono-label">DESIGN NOTE</h4>
              <p>{part.designNote}</p>
            </div>
          </div>

          <ul className="hardware-row__specs" aria-label={`${part.name} specifications`}>
            {part.specs.map((spec) => (
              <li key={spec}>{spec}</li>
            ))}
          </ul>
        </div>
      </div>
    </article>
  );
}

function HardwareMedia({ part, open }: { part: HardwarePart; open: boolean }) {
  const asset = imageAssets[part.media];
  const view = hardwareViews[part.id];
  const [failed, setFailed] = useState(false);
  const [modelReady, setModelReady] = useState(false);
  const [modelFailed, setModelFailed] = useState(false);
  const [frameRef, inView] = useInView<HTMLDivElement>();
  const isCadAssembly = asset.kind === 'cad-assembly';
  const showImage = asset.available && !failed;
  const live = open && inView && !modelFailed && view !== undefined && isWebGLAvailable();

  useEffect(() => {
    if (!live) setModelReady(false);
  }, [live]);

  return (
    <figure className="hardware-row__media">
      <div ref={frameRef} className={`media-frame${live && modelReady ? ' is-live' : ''}`}>
        {showImage ? (
          <img
            src={asset.src}
            alt={asset.alt}
            loading="lazy"
            decoding="async"
            onError={() => setFailed(true)}
          />
        ) : (
          <MissingAsset
            label={isCadAssembly ? 'CAD RENDER NOT PROVIDED' : 'HARDWARE IMAGE NOT PROVIDED'}
            part={asset.partName}
            icon={isCadAssembly ? 'cad' : 'photo'}
          />
        )}
        {live && (
          <Suspense fallback={null}>
            <PartModel
              key={part.id}
              view={view}
              label={asset.partName}
              onReady={() => setModelReady(true)}
              onError={() => setModelFailed(true)}
            />
          </Suspense>
        )}
      </div>
      {showImage && (
        <figcaption className="mono-label">
          <span>{captionFor(asset)}</span>
          {view && <span className="hardware-row__shows">{view.shows}</span>}
        </figcaption>
      )}
    </figure>
  );
}
