import { ChevronDown, SlidersHorizontal } from 'lucide-react';
import { useState, useSyncExternalStore, type FormEvent } from 'react';
import { DeviceCommandError, type DeviceLink } from '../hardware/deviceLink';
import {
  formatApplied,
  PARAMETER_FIELDS,
  parameterRange,
  parseParameterChanges,
  type FeedForwardDraft,
} from '../hardware/parameters';

const EMPTY_TABLE: readonly FeedForwardDraft[] = Array.from({ length: 8 }, () => ({ rpm: '', throttle: '' }));

export function ParametersPanel({ link }: { link: DeviceLink }) {
  const telemetry = useSyncExternalStore(link.subscribeTelemetry, link.getTelemetry);
  const [open, setOpen] = useState(false);
  const [draft, setDraft] = useState<Record<string, string>>({});
  const [ffTable, setFfTable] = useState<readonly FeedForwardDraft[]>(EMPTY_TABLE);
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<{ tone: 'ok' | 'error'; text: string } | null>(null);

  if (telemetry.statusWord !== 'SAFE') return null;

  const submit = async (event: FormEvent) => {
    event.preventDefault();
    const parsed = parseParameterChanges(draft, ffTable);
    if (parsed.error) {
      setResult({ tone: 'error', text: parsed.error });
      return;
    }
    setBusy(true);
    setResult(null);
    try {
      const ack = await link.command('set_params', { params: parsed.params });
      if (!ack.applied) {
        setResult({ tone: 'error', text: 'The box accepted set_params without returning the applied set.' });
        return;
      }
      setResult({ tone: 'ok', text: `Applied: ${formatApplied(ack.applied)}` });
      setDraft({});
      setFfTable(EMPTY_TABLE);
    } catch (error) {
      const reason = error instanceof DeviceCommandError && error.reason ? error.reason : error instanceof Error ? error.message : 'unknown error';
      setResult({ tone: 'error', text: `Set params refused: ${reason}` });
    } finally {
      setBusy(false);
    }
  };

  return (
    <section className={`params${open ? ' is-open' : ''}`} aria-labelledby="params-title">
      <h2 className="params__heading" id="params-title">
        <button type="button" className="params__toggle" aria-expanded={open} onClick={() => setOpen((value) => !value)}>
          <SlidersHorizontal size={17} strokeWidth={1.75} aria-hidden />
          <span className="mono-label">PARAMETERS / SAFE ONLY</span>
          <span className="params__summary">Gains and limits</span>
          <ChevronDown className="params__chevron" size={16} aria-hidden />
        </button>
      </h2>
      {open ? (
        <form className="params__body" onSubmit={(event) => void submit(event)}>
          <p className="params__note">
            Enter only values you want to change. The box validates the complete set, stores it in NVS, and either applies all of it or refuses all of it.
          </p>
          <div className="params__fields">
            {PARAMETER_FIELDS.map((field) => (
              <label className="params__field" key={field.key}>
                <span className="params__field-name">{field.label}</span>
                <span className="params__field-key mono-label">{field.description}</span>
                {field.options ? (
                  <select
                    value={draft[field.key] ?? ''}
                    onChange={(event) => setDraft((current) => ({ ...current, [field.key]: event.target.value }))}
                  >
                    <option value="">Unchanged</option>
                    {field.options.map((option) => <option value={option} key={option}>{option > 0 && field.key.startsWith('s_') ? `+${option}` : option}</option>)}
                  </select>
                ) : (
                  <input
                    type="number"
                    inputMode="decimal"
                    min={field.minimum}
                    max={field.maximum}
                    step={field.input === 'integer' ? 1 : 'any'}
                    placeholder={`Unchanged · default ${field.defaultValue}`}
                    value={draft[field.key] ?? ''}
                    onChange={(event) => setDraft((current) => ({ ...current, [field.key]: event.target.value }))}
                  />
                )}
                <span className="params__range">Allowed: {parameterRange(field)}</span>
              </label>
            ))}
          </div>

          <fieldset className="params__ff">
            <legend>Feed-forward table · ff_table</legend>
            <p>Optional. If changed, provide all eight points in increasing RPM order.</p>
            <div className="params__ff-head mono-label"><span>POINT</span><span>RPM ≥ 0</span><span>THROTTLE 48–2047</span></div>
            {ffTable.map((point, index) => (
              <div className="params__ff-row" key={index}>
                <span className="mono-label">{index + 1}</span>
                <input
                  type="number"
                  min={0}
                  step="any"
                  aria-label={`Feed-forward point ${index + 1} RPM`}
                  value={point.rpm}
                  onChange={(event) => setFfTable((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, rpm: event.target.value } : item))}
                />
                <input
                  type="number"
                  min={48}
                  max={2047}
                  step={1}
                  aria-label={`Feed-forward point ${index + 1} throttle`}
                  value={point.throttle}
                  onChange={(event) => setFfTable((current) => current.map((item, itemIndex) => itemIndex === index ? { ...item, throttle: event.target.value } : item))}
                />
              </div>
            ))}
          </fieldset>

          <p className="params__deferred">
            Calibration arrays and the light threshold are not implemented by firmware yet. Bench mode stays in the engineering tools.
          </p>
          <button type="submit" className="button button--primary params__apply" disabled={busy}>
            {busy ? 'Waiting for box…' : 'Apply changed parameters'}
          </button>
          <p
            className={`params__result mono-label${result?.tone === 'error' ? ' params__result--error' : ''}`}
            role="status"
            aria-live="polite"
          >
            {result?.text ?? ''}
          </p>
        </form>
      ) : null}
    </section>
  );
}
