import { ArrowLeft } from 'lucide-react';
import { SITE_URL } from '../data/navigation';

export function GroundHeader({ deviceLive = false, deviceClosed = false }: { deviceLive?: boolean; deviceClosed?: boolean }) {
  const status = deviceLive
    ? 'DEVICE CONNECTED — RECEIVING TELEMETRY'
    : deviceClosed
      ? 'DEVICE LINK CLOSED — TELEMETRY FROZEN'
      : 'SIMULATED — NO DEVICE CONNECTION';
  return (
    <header className="gs-header">
      <div className="gs-header__left">
        <a href={SITE_URL} className="gs-header__back">
          <ArrowLeft strokeWidth={1.75} aria-hidden />
          Overview
        </a>
        <span className="wordmark gs-header__wordmark" aria-hidden="true">
          DirecSun
        </span>
      </div>
      <p className="gs-header__title mono-label">GROUND STATION</p>
      <p className={`gs-header__status${deviceLive ? ' gs-header__status--live' : ''}`} role="status">
        {status}
      </p>
    </header>
  );
}
