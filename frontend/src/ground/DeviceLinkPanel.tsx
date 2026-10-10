import { Plug, Unplug } from 'lucide-react';
import { useId, useState, useSyncExternalStore, type FormEvent } from 'react';
import { validateLinkUrl, type DeviceLink, type LinkState } from '../hardware/deviceLink';

const STORAGE_KEY = 'direcsun.deviceLinkUrl';

function readSavedUrl(): string {
  try {
    return localStorage.getItem(STORAGE_KEY) ?? '';
  } catch {
    return '';
  }
}

function saveUrl(url: string) {
  try {
    localStorage.setItem(STORAGE_KEY, url);
  } catch {
  }
}

function statusText(state: LinkState): string {
  switch (state.status) {
    case 'idle':
      return 'NO DEVICE LINK — SHOWING SIMULATION';
    case 'connecting':
      return 'CONNECTING…';
    case 'open':
      return state.packets === 0
        ? 'SOCKET OPEN — WAITING FOR TELEMETRY'
        : `RECEIVING TELEMETRY · ${state.packets} PACKETS${state.ignored ? ` · ${state.ignored} IGNORED` : ''}`;
    case 'closed':
      return 'DISCONNECTED — SHOWING SIMULATION';
    case 'error':
      return 'CONNECTION FAILED — SHOWING SIMULATION';
  }
}

export function DeviceLinkPanel({ link }: { link: DeviceLink }) {
  const state = useSyncExternalStore(link.subscribeState, link.getState);
  const [url, setUrl] = useState(readSavedUrl);
  const [error, setError] = useState<string | null>(null);
  const inputId = useId();
  const messageId = useId();
  const active = state.status === 'connecting' || state.status === 'open';

  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (active) {
      link.disconnect();
      return;
    }
    const problem = validateLinkUrl(url);
    setError(problem);
    if (problem) return;
    saveUrl(url.trim());
    link.connect(url.trim());
  };

  return (
    <section className={`link link--${state.status}`} aria-labelledby={`${inputId}-title`}>
      <form className="link__form" onSubmit={onSubmit} noValidate>
        <label className="link__label mono-label" id={`${inputId}-title`} htmlFor={inputId}>
          DEVICE LINK / WEBSOCKET
        </label>
        <input
          id={inputId}
          className="link__input"
          type="url"
          inputMode="url"
          autoComplete="off"
          spellCheck={false}
          placeholder="ws://192.168.4.1/ws"
          value={url}
          disabled={active}
          aria-invalid={error ? true : undefined}
          aria-describedby={messageId}
          onChange={(event) => {
            setUrl(event.target.value);
            setError(null);
          }}
        />
        <button type="submit" className={`button ${active ? 'button--secondary' : 'button--primary'} link__button`}>
          {active ? <Unplug strokeWidth={1.75} aria-hidden /> : <Plug strokeWidth={1.75} aria-hidden />}
          {active ? 'Disconnect' : 'Connect'}
        </button>
      </form>
      <p className="link__status mono-label" id={messageId} role="status">
        {error ? <span className="link__error">{error}</span> : statusText(state)}
        {!error && state.message && state.status !== 'open' && <span> · {state.message}</span>}
      </p>
      <p className="link__note">
        Read-only: displays v1 telemetry (<code>{'{"type":"tlm",…}'}</code>) from your backend. Nothing is
        sent to the device, and it does not reconnect automatically.
      </p>
    </section>
  );
}
