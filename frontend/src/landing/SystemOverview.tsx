import { SectionHeading } from '../components/SectionHeading';
import { SystemDiagram } from './SystemDiagram';

export function SystemOverview() {
  return (
    <section className="section" id="system" tabIndex={-1} aria-labelledby="system-title">
      <div className="container">
        <SectionHeading
          index="001 / THE SYSTEM"
          title="One axis. One reaction wheel. A complete feedback loop."
          titleId="system-title"
          intro="DirecSun measures its orientation, calculates a correction onboard, and changes wheel speed to turn the frame in the opposite direction."
        />
        <SystemDiagram />
      </div>
    </section>
  );
}
