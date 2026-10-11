# DirecSun pitch and demo

This is the three-minute judge path. The ground-station sequence is the centrepiece. Use the local HTTP build so the page works without internet and the browser can connect to the box over `ws://`.

## Roles

- **Presenter:** speaks, scrolls the landing page, and narrates the result.
- **Julian:** owns the bench and is the only person who may approve arming or spinning the real motor.
- **Operator:** uses the ground station only after Julian gives the explicit go for that run.

One person may fill more than one role, but the hardware approval stays with Julian.

## Preflight, before judges arrive

1. Put the assembled rig on its bolt with the guard fitted and the lock pin inserted.
2. Use the battery or bench configuration Julian has approved. Confirm the e-stop is reachable.
3. On the laptop, from `frontend/`, run `npm run demo` while dependencies are available. Keep `npm run demo:serve` as the offline restart command.
4. Open `http://localhost:8080/` and `http://localhost:8080/ground-station/`. Confirm both load with the network disconnected.
5. If demonstrating the real box, join its Wi-Fi and enter `ws://192.168.4.1/ws`, or use `ws://direcsun.local/ws` on the shared network. Confirm live telemetry before the presentation.
6. Check the box's turn readiness:
   - In READY, `wheel_ref_rpm` must be above 0 before attempting a turn.
   - Spin-up normally takes about 6 s. If it remains in SPINUP beyond 10 s, disarm and ask Julian whether the box is characterised.
   - After any parameter reset, Julian must set `rate_gain_scale` to 1 from the bench console before a judge turn.
   - Allow about 5 s after an e-stop before trying to re-arm.
7. Rehearse e-stop once under Julian's bench procedure. Do not count rehearsal as presentation evidence.
8. Decide the fallback level before the judges enter:
   - **A:** real judge sequence;
   - **B:** live telemetry, simulated judge sequence;
   - **C:** fully local simulated sequence;
   - **D:** landing-page explanation and recorded results only.

Never delay the pitch while troubleshooting hardware. Drop one fallback level and keep the story moving.

## Three-minute script

### 0:00 to 0:25, the problem

Show the landing-page hero.

> Satellites point without pushing against the air. DirecSun makes that invisible idea visible on a desk. This frame turns around one vertical axis, and the wheel inside trades angular momentum with the body.

Scroll to **System**.

### 0:25 to 0:55, how it works

Select **Sensing**, then **Control**, in the System diagram.

> The gyro measures turn rate, the compass gives heading, and two light sensors show where the lamp is. The ESP32 runs the control loop and commands the motor through the ESC. The browser is the operator station, but the box itself enforces the safety interlocks.

Do not claim that 50 Hz telemetry, closed-loop turns, or latency targets are measured unless the corresponding bench result has been recorded.

### 0:55 to 1:20, the hardware

Scroll to **Hardware** and pause on the reaction wheel.

> Speeding this internal wheel one way turns the body the other way. The CAD view and the physical stack use the same component map, so we can explain the system from frame to sensor deck instead of hiding it in a black box.

### 1:20 to 2:25, the judge sequence

Open **Ground Station**.

For a simulated run, the banner must say `SIMULATED - NO DEVICE CONNECTION`. Click **Rotate right 90°** or **Rotate left 90°** and narrate the visible phases:

> First it establishes zero heading. Then it slews to ninety degrees, holds for one second, and returns to zero. The same interface shows heading, body rate, wheel speed, target, safety flags, and the throttle band.

For a real-box run:

1. Confirm the device banner says it is receiving telemetry.
2. Confirm the lock pin is inserted and the rig matches Julian's bench checklist.
3. Stop and ask: **“Julian, go to arm for this run?”**
4. Wait for Julian's explicit yes. If there is no yes, use fallback B or C.
5. Confirm the lock-pin prompt and arm. Follow Julian's cue for removing the pin.
6. Run one direction only unless Julian explicitly approves another motor run.
7. Keep a hand clear of the rig. Use the red e-stop or space bar immediately if the motion, sound, telemetry, or hardware differs from the rehearsed run.

Narrate only what is visible. Do not call a simulated run hardware evidence.

### 2:25 to 3:00, close

Return to the landing page or leave the final ground-station readout visible.

> The important part is not just that the model turns. The same chain runs from sensors, through estimation and control, to a reaction wheel, with the operator able to see the state and stop it immediately. DirecSun turns spacecraft attitude control into something you can inspect, command, and understand on a desk.

## Recovery lines

- **No box connection:** “The operator interface is running locally and the same command sequence is available in simulation. Hardware validation is tracked separately from this software demonstration.”
- **No WebGL:** “The controls and system diagram remain available; the page has fallen back because this browser cannot create the 3D context.”
- **Unexpected telemetry or motion:** press e-stop, keep the lock pin out until the wheel has stopped, and say, “The safety path worked; we will continue with the software fallback.” Follow Julian's physical procedure.
- **Venue network failure:** use `npm run demo:serve` and fallback C. The local bundle needs no internet.

## After the presentation

- Disarm and follow Julian's shutdown procedure.
- Save any real-box capture used as evidence with the date, setup, firmware commit, and result.
- Record failures as failures. A clean simulated run does not close a hardware test.
