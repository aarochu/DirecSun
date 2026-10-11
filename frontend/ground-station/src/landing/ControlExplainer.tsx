import { SectionHeading } from '../components/SectionHeading';
import { useReveal } from '../lib/useReveal';

const STEPS = [
  {
    index: '01',
    title: 'Sense',
    body: 'The gyro measures body rate. A magnetic reference constrains heading drift, while the sun sensors provide an optical pointing error. A Hall switch reads wheel speed.',
  },
  {
    index: '02',
    title: 'Calculate',
    body: "The ESP32 estimates the body's orientation, compares it with the target, and computes the reaction-wheel correction using cascaded controllers.",
  },
  {
    index: '03',
    title: 'Actuate',
    body: "DShot commands reach the ESC. Changing the flywheel's speed applies reaction torque to the frame. The sensors measure the result and the loop repeats.",
  },
] as const;

export function ControlExplainer() {
  const loopRef = useReveal<HTMLDivElement>();
  return (
    <section className="section control" id="control" tabIndex={-1} aria-labelledby="control-title">
      <div className="container">
        <SectionHeading index="003 / CONTROL LOOP" title="Sense. Calculate. Correct." titleId="control-title" />

        <div className="control__loop" ref={loopRef}>
          <ol className="control-steps">
            {STEPS.map((step) => (
              <li className="control-step" key={step.index}>
                <p className="control-step__index mono-label">
                  {step.index} / {step.title.toUpperCase()}
                </p>
                <h3 className="control-step__title">{step.title}</h3>
                <p className="control-step__body">{step.body}</p>
              </li>
            ))}
          </ol>
          <div className="control-feedback" aria-hidden="true">
            <span className="control-feedback__line" />
            <span className="control-feedback__label mono-label">FEEDBACK — MEASURE AGAIN</span>
          </div>
        </div>

        <div className="control-targets">
          <dl>
            <div>
              <dt className="mono-label">ONBOARD CONTROL TARGET</dt>
              <dd>500 Hz</dd>
            </div>
            <div>
              <dt className="mono-label">GROUND TELEMETRY TARGET</dt>
              <dd>50 Hz</dd>
            </div>
          </dl>
          <p className="control-targets__note mono-label">DESIGN TARGETS — NOT VERIFIED PERFORMANCE</p>
        </div>
      </div>
    </section>
  );
}
