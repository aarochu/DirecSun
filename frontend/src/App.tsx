import { useLayoutEffect } from 'react';
import { Navigate, Route, Routes, useLocation } from 'react-router-dom';
import { GROUND_STATION_PATH } from './data/navigation';
import GroundStationPage from './pages/GroundStationPage';
import LandingPage from './pages/LandingPage';

function ResetScrollOnRouteChange() {
  const { pathname, hash } = useLocation();
  useLayoutEffect(() => {
    if (!hash) window.scrollTo(0, 0);
  }, [pathname]);
  return null;
}

export function App() {
  return (
    <>
      <ResetScrollOnRouteChange />
      <Routes>
        <Route path="/" element={<LandingPage />} />
        <Route path={GROUND_STATION_PATH} element={<GroundStationPage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </>
  );
}
