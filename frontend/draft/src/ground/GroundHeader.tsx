import { ArrowLeft } from 'lucide-react';
import { Link } from 'react-router-dom';

export function GroundHeader({ deviceLive = false }: { deviceLive?: boolean }) {
  return (
    <header className="gs-header">
      <div className="gs-header__left">
        <Link to="/" className="gs-header__back">
          <ArrowLeft strokeWidth={1.75} aria-hidden />
          Overview
        </Link>
        <span className="wordmark gs-header__wordmark" aria-hidden="true">
          DirecSun
        </span>
      </div>
      <p className="gs-header__title mono-label">GROUND STATION</p>
      <p className={`gs-header__status${deviceLive ? ' gs-header__status--live' : ''}`} role="status">
        {deviceLive ? 'DEVICE CONNECTED — RECEIVING TELEMETRY' : 'SIMULATED — NO DEVICE CONNECTION'}
      </p>
    </header>
  );
}
