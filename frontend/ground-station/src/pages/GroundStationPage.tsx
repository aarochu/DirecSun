import { useEffect, useMemo, useState, useSyncExternalStore } from 'react';
import { DeviceControls } from '../ground/DeviceControls';
import { DeviceLinkPanel } from '../ground/DeviceLinkPanel';
import { operatorDisplaySource, simulationSource } from '../ground/displaySource';
import { GroundHeader } from '../ground/GroundHeader';
import { JudgeControls } from '../ground/JudgeControls';
import { ModelViewport } from '../ground/ModelViewport';
import { ParametersPanel } from '../ground/ParametersPanel';
import { SensorPanel } from '../ground/SensorPanel';
import { TelemetryRail } from '../ground/TelemetryRail';
import { DeviceLink } from '../hardware/deviceLink';
import { DeviceOperator } from '../hardware/deviceOperator';
import { useMockPlant } from '../simulation/useMockPlant';
import '../styles/ground.css';

function useDeviceLink(): DeviceLink {
  const [link] = useState(() => new DeviceLink());
  useEffect(() => () => link.disconnect(), [link]);
  return link;
}

function useDeviceOperator(link: DeviceLink): DeviceOperator {
  const [operator] = useState(() => new DeviceOperator(link));
  useEffect(() => {
    operator.attach();
    return () => operator.detach();
  }, [operator]);
  return operator;
}

export default function GroundStationPage() {
  const plant = useMockPlant();
  const simulation = useMemo(() => simulationSource(plant), [plant]);
  const link = useDeviceLink();
  const operator = useDeviceOperator(link);
  const deviceLive = useSyncExternalStore(link.subscribeState, () => link.receiving);
  const operatorState = useSyncExternalStore(operator.subscribe, operator.getState);
  const deviceDisplay = useMemo(() => operatorDisplaySource(link, operator), [link, operator]);

  useEffect(() => {
    if (deviceLive) plant.reset();
  }, [deviceLive, plant]);

  const showDeviceControls = deviceLive || operatorState.linkLost;
  const source = showDeviceControls ? deviceDisplay : simulation;
  return (
    <div className="gs">
      <GroundHeader deviceLive={deviceLive} deviceClosed={!deviceLive && showDeviceControls} />
      <main className="gs-main" id="main">
        <h1 className="sr-only">
          DirecSun ground station — {deviceLive ? 'device telemetry' : showDeviceControls ? 'device link closed' : 'simulated, no device connection'}
        </h1>
        <ModelViewport source={source} />
        {showDeviceControls ? <DeviceControls link={link} operator={operator} /> : <JudgeControls plant={plant} />}
        <div className="gs-side">
          <DeviceLinkPanel link={link} />
          {deviceLive ? <ParametersPanel link={link} /> : null}
          <TelemetryRail source={source} />
          <SensorPanel source={source} />
        </div>
      </main>
    </div>
  );
}
