import { useEffect, useState } from 'react';
import { HardwareRow } from '../components/HardwareRow';
import { SectionHeading } from '../components/SectionHeading';
import { hardwareParts } from '@shared/data/hardware';
import { isWebGLAvailable } from '../lib/environment';
import { useInView } from '../lib/useInView';

export function HardwareBreakdown() {
  const [openIds, setOpenIds] = useState<ReadonlySet<string>>(() => new Set());
  const allOpen = openIds.size === hardwareParts.length;
  const [sectionRef, near] = useInView<HTMLElement>('1500px');

  useEffect(() => {
    if (!near || !isWebGLAvailable()) return;
    const start = () => void import('../components/PartModel').then((module) => module.preloadStack());
    if (window.requestIdleCallback) {
      const handle = window.requestIdleCallback(start);
      return () => window.cancelIdleCallback(handle);
    }
    const timer = window.setTimeout(start, 200);
    return () => window.clearTimeout(timer);
  }, [near]);

  const toggle = (id: string) =>
    setOpenIds((current) => {
      const next = new Set(current);
      if (!next.delete(id)) next.add(id);
      return next;
    });

  return (
    <section className="section" id="hardware" ref={sectionRef} tabIndex={-1} aria-labelledby="hardware-title">
      <div className="container">
        <SectionHeading
          index="002 / HARDWARE"
          title="Every component has a job."
          titleId="hardware-title"
          intro="DirecSun combines onboard sensing, a flight computer, and a reaction wheel inside a 3D-printed 2U frame. The sections below show what each component measures, how it connects, and how it contributes to single-axis attitude control."
        />
        <div className="hardware-list__bar">
          <p className="mono-label">{hardwareParts.length} COMPONENTS</p>
          <button
            type="button"
            className="hardware-list__toggle-all"
            onClick={() => setOpenIds(allOpen ? new Set() : new Set(hardwareParts.map((part) => part.id)))}
          >
            {allOpen ? 'Collapse all' : 'Expand all'}
          </button>
        </div>
        <div className="hardware-list">
          {hardwareParts.map((part) => (
            <HardwareRow key={part.id} part={part} open={openIds.has(part.id)} onToggle={() => toggle(part.id)} />
          ))}
        </div>
      </div>
    </section>
  );
}
