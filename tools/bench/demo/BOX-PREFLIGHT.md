# Demo box preflight (firmware side)

This is the box's half of the demo. `frontend/DEMO.md` (Aaron's) covers the laptop, the roles, and the script; follow it for everything else. This file was written on 2026-10-10 from `feat/set4/R4.3-hold-and-slew` at a5a6fa2. The full judge sequence has not run on hardware yet. Every timing below is from the code, the host simulation, or the bench logs, and is marked as such.

Only Julian arms or spins the motor, each time with his explicit go (`changes/CONTEXT.md`).

## 1. Build and flash the demo firmware

1. Take the top of the stack: `feat/set4/R4.3-hold-and-slew`, at a5a6fa2 or later. Once the spin-safety branch and R4.1 are merged, take the newest top.
2. Set the access point in `firmware/sdkconfig.defaults.local`, which is gitignored and must never be committed. The main UI's default endpoint is `ws://192.168.4.1/ws`.
   ```
   CONFIG_DIRECSUN_WIFI_SOFTAP=y
   CONFIG_DIRECSUN_SOFTAP_SSID="DirecSun"
   CONFIG_DIRECSUN_SOFTAP_PASSWORD="<at least 8 characters>"
   CONFIG_DIRECSUN_SOFTAP_CHANNEL=6
   ```
3. Build and flash:
   ```bash
   rm -f firmware/sdkconfig
   tools/idf.sh build        # expect 0 warnings
   tools/idf.sh -p /dev/ttyACM0 flash
   ```
4. Check the boot log:
   - All three sensors answer.
   - `SoftAP "<ssid>" on channel 6, address 192.168.4.1` and `WebSocket: ws://192.168.4.1/ws ...`.
   - From the second second on, the status line shows `missed 0` and `loop_us_max` near 755. Measured: 755 to 760 µs on this firmware with the access point up, idle in SAFE (FD §9.2, V-62).
   - In about half of boots, the first second shows one tick of about 4 ms and a missed tick during Wi-Fi start-up. This is known, open, and SAFE only.

## 2. What each fallback level needs from the box

| Level | What the box needs | State on 2026-10-10 |
| --- | --- | --- |
| **A**, the real judge sequence | See the list below the table. | None of the hardware steps done yet |
| **Rung 1** (not in DEMO.md): nudge the wheel, and the box turns the other way | ESC and motor, plus the flywheel for visible torque. Battery reading above 9.6 V. The console's **Bench setup** (bench mode, Hall bypass, throttle capped at 400; about 1600 rpm, estimated). MANUAL throttle from the bench console. | Needs the ESC wiring and `tools/bench/first-spin/RUNBOOK.md` on `feat/set3/R3.7-spin-safety` |
| **B**, live telemetry | The box on its access point and the laptop joined to it. Turning the box by hand moves the UI's heading and dial (R3.5, "the main UI reads the real box"). Nothing is armed. | Firmware ready; not yet run against the real box. `tools/bench/r35/` has the check |
| **C** and **D** | Nothing from the box | |

Level A needs all of these:
- ESC and motor.
- The printed flywheel, with both magnets south-out toward the A1104's printed face, at the design gap.
- The battery divider on D9, reading above 9.6 V. `arm` is refused below that.
- T-5 done, with `ff_table`, `wheel_kp`, and `wheel_ki` loaded through `set_params`.
- The rate-loop sign confirmed (R3.9), then `rate_gain_scale` set to 1.
- The rig on its bolt or turntable, level (under 30° tilt) and still (under 5 deg/s) at arm.
- R4.1's ordering fix, or Aaron's guard in #32. Without one of them, the turn can stop after the first leg.

## 3. What the operator will see from the box

- **No `ff_table`:** `mode SLEW` is refused with `wheel loop not available`, and the UI shows the reason.
- **`arm` refusals:**
  - `battery low`: no divider, or a reading under 9.6 V;
  - `box not still`;
  - `latched fault: <NAME>`;
  - `ramp-down in progress`: within 5 s of an e-stop or disarm.
- **Spin-up:** READY comes about 6.2 s after arm (the reference ramps at 400 rpm/s, then a 0.5 s dwell; from the code). The mock box takes 2 s.
- **One turn:** each slew takes about 3 s at the design gains (host simulation), then the 1 s hold, then about 3 s back. The box times a slew out at 6.0 s.
- **E-stop:** a 5 s ramp, then DShot 0. While it ramps, telemetry reads SAFE with the throttle above 0.
- **After a reboot:** bench mode and the Hall bypass are off. `ff_table` and the gains persist in NVS once set.
