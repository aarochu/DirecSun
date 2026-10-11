# First spin runbook: R3.7 rung 1 on the bench, Hall bypassed

Written 2026-10-10 from an independent review of `origin/feat/set3/R3.7-manual-throttle` at `bd5f6af`, and updated the same evening for `feat/set3/R3.7-spin-safety` (6531e64). That branch fixes F1 to F5 below (FD V-63 to V-66) and adds a bench-setup control to the console. Run the session from it. Nothing in this runbook has been run on hardware. Every number marked "expected" comes from the code, the host tests, or the R3.4 bench logs; the hardware has the final say.

**Safety rule (CONTEXT.md):** only Julian arms or spins the motor, each time with his explicit go. Every step below that can turn the motor carries the line **Julian's explicit go required.** An agent assisting at the bench stops at each of those lines, says exactly what the step will do, and waits.

## 0. Known issues that change how this session is run

These come from the review. None blocks the session. F1 to F5 are fixed on `feat/set3/R3.7-spin-safety`: F1 by V-63, F2 by the console's Bench setup control (3cfb430), F3 by V-64, F4 by V-65, and F5 by V-66. The workarounds stay in place as a second layer.

| # | Issue | Workaround in this runbook |
| --- | --- | --- |
| F1 | **Space does not e-stop while the endpoint text field has focus.** Pressing Enter in the endpoint field to connect leaves focus there; Space then types a space into the URL and sends nothing. Verified in Chrome against the mock box: the box stayed in MANUAL. | Connect by clicking **Connect**, never with Enter. Then click an empty part of the page. Before every spin, confirm Space works (step 6 dry run). Keep a hand on the bench supply's output button. |
| F2 | **The R3.7 console has no way to send `set_params`**, so it cannot switch on bench mode or the Hall bypass. | Use the console's **Bench setup** control (section 5, step 6). It sets known parameters and the bypass and never arms; tested against the mock box. |
| F3 | Without the bypass and without a Hall sensor, HALL_SILENT is the only thing that stops SPINUP, and its threshold `hall_expect_throttle` is a stored parameter. With stored values of 2047 for both `spinup_throttle` and `hall_expect_throttle`, nothing stops a full-throttle SPINUP (host test). The stock values stop it at 1.5 s and throttle 300. | Bench setup writes the throttle parameters to known values first, then the bypass. Never arm unless the console shows `HALL_BYPASS` and the box clamp reads `150 to 400`. |
| F4 | `set_params` is accepted during the 5 s ramp-down (the box is already SAFE) and triggers a flash write while the motor still turns. | Never run Bench setup or change parameters while the throttle reads anything other than 0 (the fix branch refuses it anyway). |
| F5 | Missed control ticks stretch the heartbeat and ramp timing (the supervisor counts executed ticks, not timer alarms). Timing with Wi-Fi on has never been measured. | Watch `missed` in the status line; it must stay 0. |
| F6 | Not verified: Chrome may slow the console's 5 Hz heartbeat to 1 Hz when its tab is in the background, against a 1 s timeout. That fails safe, as a spurious ramp-down. | Keep the console in its own visible window, in front, for the whole spin. |
| F7 | While the 5 s ramp-down runs, the box reports `SAFE` with a non-zero throttle. There is no "ramping" field on the wire. | SAFE with throttle above 0 means "still ramping down". Wait for 0. |

## 1. Before the session (desk, no hardware powered)

1. **Make the R3.7 checkout** (a separate worktree leaves the main checkout alone):
   ```bash
   git -C ~/projects/DirecSun fetch
   git -C ~/projects/DirecSun worktree add --detach ~/projects/direcsun-spin origin/feat/set3/R3.7-spin-safety
   cp ~/projects/DirecSun/firmware/sdkconfig.defaults.local ~/projects/direcsun-spin/firmware/
   ```
   Check `git -C ~/projects/direcsun-spin log -1 --oneline` starts with `6531e64` (or the branch's newer head, if it has moved since).
2. **Build the flight firmware.** Use the flight build only. Not `build-bench`: its stall test resets the board 10 s after every boot. Not `build-diag` either.
   ```bash
   cd ~/projects/direcsun-spin
   rm -f firmware/sdkconfig
   tools/idf.sh build 2>&1 | tee /tmp/spin-build.txt
   grep -c 'warning:' /tmp/spin-build.txt        # expect 0
   grep -E 'CONFIG_DIRECSUN_BENCH_STALL_TEST|CONFIG_DIRECSUN_WIFI_SSID' firmware/sdkconfig
   ```
   Expect `# CONFIG_DIRECSUN_BENCH_STALL_TEST is not set` and your SSID (not `""`). The reviewer's build of the same commit (without Wi-Fi credentials) gave 0 warnings and a 0x746c0-byte image. That image had no stall-test symbol and no raw-frame symbol, while `DshotRmt::send` and `Supervisor::step` were present.
3. **Optional host check:** `tools/host-test.sh` gives `PASSED, 102 test case(s)`, and `tools/host-test.sh --selftest` catches all 6 mutants.
4. **Read the motor label.** The docs assume 920 KV, from the owner's statement, not inspected. Expected speed at the bypass ceiling (throttle 400, 17.6 % of the DShot range) at 10 V is about KV x 10 x 0.176:

   | KV | Expected rpm at 400 | Go? |
   | --- | --- | --- |
   | 920 | about 1600 | yes |
   | 1000 | about 1760 | yes, margin small |
   | above 1100 | above 1940 | **no**: lower `kBypassThrottleMax` in a spec and build first |

5. **Count the magnets** in the bell through the stator gaps. Expect 14 (12N14P), so 7 pole pairs. Pole pairs = magnets / 2. Use your count in every rpm conversion below.
6. Tell Aaron not to connect any UI to the box during the session. His `feat/ui-box-control` branch can send `arm`, and the box accepts `arm` from any client while it is SAFE.
7. Have ready: safety glasses, the multimeter, the scope with two 10x probes, the bench supply, this runbook, and a paint-marker dot on the motor bell (for an optional phone slow-motion cross-check).

## 2. Wiring checklist (everything off: USB unplugged, supply output OFF)

```
bench supply +  ->  10 A fuse  ->  node S+  ->  ESC power connector (+)  ->  ESC +    [470 uF across ESC + and -, at the ESC, short leads]
                                     |
                                     +-> 100 kOhm -> tap -> 22 kOhm -> GND           [tap -> XIAO D9 (GPIO8), connected only after M8]
bench supply -  ->  node GND  ->  ESC power connector (-)  ->  ESC -
                    node GND  ->  XIAO GND  (the one signal-ground wire)
XIAO D8 (GPIO7) ->  ESC signal wire;   ESC signal ground -> XIAO GND
ESC red BEC wire, if it has one: NOT connected, insulated
motor: 3 phase wires to the ESC, secured; motor clamped to the bench
scope ground clips: node GND only
```

- The **divider taps the supply side of the ESC power connector**, so it reads the supply with the ESC unplugged. The dry run (step 6) depends on this. If the ESC has no connector, add one (XT30, XT60, or bullets); it also gives you a physical disconnect.
- **Capacitor polarity:** stripe (-) to GND. 35 V rating (TD 5.1).
- **Motor:** no flywheel, no prop, no washers, no loose nut on the shaft. Nothing within 5 cm of the bell: wires, probes, tools. The clamp must hold the motor body, not the bell.
- **Routing:** twist the three phase wires and the supply leads. Keep them away from the STEMMA QT cable and the IMU board (I2C noise trips IMU_SILENT, which fails safe but ends the run).
- **IMU board flat, component side up.** More than 30 degrees of tilt latches TILT and blocks `arm`.
- **Scope:** CH1 10x probe tip on one motor phase wire, ground clip on node GND. CH2 on D8, ground clip on node GND. **Never clip a scope ground to a motor phase or to D8:** an earthed scope ground on a phase shorts that phase through the ESC.

**Measure before any power (multimeter):**

| ID | Measure | Expect | Stop if |
| --- | --- | --- | --- |
| M1 | ESC + to - at the ESC input, ESC plugged in | reading climbs as the 470 uF charges from the meter | steady below about 10 ohm (short) |
| M2 | Fuse | below 1 ohm | open |
| M3 | Capacitor stripe | on GND | reversed |
| M4 | D8 to ESC signal wire; XIAO GND to ESC signal ground and to supply - | below 1 ohm each | open |
| M5 | D8 to GND | above 1 kohm | short |
| M6 | Motor phase to phase, all 3 pairs (at the motor leads, before they go to the ESC if you can) | about 0.1 to 0.3 ohm, all three alike | OL or very different on one pair |
| M7 | Divider, tap disconnected from D9: S+ to tap, tap to GND | about 100 k and 22 k | swapped (that would put 8.2 V on D9) |
| M8 | Supply set to 10.0 V and 2.0 A (output on, ESC unplugged, tap not on D9): tap voltage | 1.80 V (10 x 22/122) | above 2.5 V |

Then supply output OFF.

## 3. Power order: USB first, supply second; off in reverse

**Up:** USB (XIAO) first. Supply output ON only when the box has booted, runs SAFE, and you have checked D8 on the scope.
**Down:** supply output OFF first, then USB.

Why:
- Within about 0.3 s of reset, the box sends DShot 0 continuously on D8 (`app_main.cpp:139-142`, then the control task from its first tick). When the ESC then gets power, the first valid signal it ever sees is 0, so it arms on zero (TD 5.2). It can never see a non-zero first frame: the box boots SAFE, and the zero sender sends only 0 (host-tested).
- Any XIAO problem (wrong build, boot loop, a latched fault, no Wi-Fi) shows up while the motor has no power at all.
- No back-powering of an unpowered ESP32 pin through the signal line or the divider.
- Flashing resets the S3 and stops the frames. With USB-first, flashing only ever happens with the ESC unpowered. **Never flash with the ESC powered.**
- Supply-first-off removes motor power while the box is still holding DShot 0.

Side effect: with the divider on the supply, booting with the supply off reads 0 V. That latches BATTERY_LOW at boot (`latched 0x0010`, a red `BATTERY_LOW` flag). This is expected. `arm` clears the latch once the 1 s average is above 9.6 V (host-tested).

## 4. Flash and boot (ESC unplugged, supply output OFF)

```bash
cd ~/projects/direcsun-spin
tools/idf.sh -p /dev/ttyACM0 flash monitor
```

Keep this monitor open for the whole session. Do not restart it during a spin: opening the port can reset the S3. That fails safe, but it reboots the box. To re-attach mid-session, use `tools/idf.sh -p /dev/ttyACM0 monitor --no-reset`.

**Expected boot lines** (tags in brackets; lines from the two cores may interleave):

```
I [dshot]     GPIO7 DShot150: bit 267 ticks, '1' high 200, '0' high 100 at 40000000 Hz
I [direcsun]  DirecSun flight firmware
I [direcsun]  firmware 6531e64..., ESP-IDF v6.0...           (no "-dirty")
I [direcsun]  target esp32s3, chip ESP32-S3 rev v..., 2 cores
I [direcsun]  flash 8MB configured, 8MB detected
I [direcsun]  reset reason: power-on (1)                     (or USB peripheral right after flashing)
I [direcsun]  DShot150 zeros on GPIO7 from N us after boot
W [direcsun]  parameters reset to defaults (missing)         (ONLY if NVS holds no parameters; if absent, stored values are in use)
I [i2c_bus]   bus on port 0: SDA GPIO5, SCL GPIO6, 400000 Hz per device; probe of 0x68: ESP_OK
I [direcsun]  IMU: ESP_OK (WHO_AM_I 0x68)
I [direcsun]  magnetometer: ESP_OK (chip ID 0x80)
I [direcsun]  barometer: ESP_OK (chip ID 0x58)
I [adc_inputs] battery: GPIO8 = ADC1 channel 7, 12 dB, curve-fitting calibration
I [direcsun]  gyro z bias seeded at about -2.08 deg/s from N still samples ...; accel reference about 0.871 g
I [direcsun]  boot window done: 500 or more DShot frames sent, battery 0.00 V over N readings     (0 V: supply off)
I [hall]      A1104 edge capture on GPIO9, falling edge, ring of 8
I [control]   control task on core 1; it takes the DShot output over from the zero sender at its first tick
I [ws]        WebSocket server on port 80 at /ws (up to 3 clients)
I [wifi]      connected, address A.B.C.D
I [wifi]      WebSocket: ws://direcsun.local/ws  or  ws://A.B.C.D/ws
```

**Must not appear:** `BENCH BUILD`, `IMU_MISSING`, `NVS unavailable`, `previous reset: CONTROL_STALL`, `no Wi-Fi network configured`, or any `E` line from `dshot`. If one does, stop and fix before going on.

**Status lines, once a second:**
```
I [status] s N ticks 500 missed 0 (total 0) loop_us_max <1000 ... dshot sent +500/s dropped 0 imu_invalid 0
I [status] mode SAFE throttle 0 vbat ... rate ~0 bias ... tilt <30 psi ... wheel 0 latched 0x0010 flags 0x00 baro ok
```

Decoding `latched`: 0x0001 ESTOP, 0x0002 HEARTBEAT_LOST, 0x0004 OVERSPEED, 0x0008 UNDERSPEED, 0x0010 BATTERY_LOW, 0x0020 IMU_SILENT, 0x0040 TILT, 0x0080 CONTROL_STALL, 0x0100 HALL_SILENT. Decoding `flags`: 0x01 MOMENTUM_OK, 0x02 MOMENTUM_NEAR_LIMIT, 0x04 MANUAL_STALE, 0x08 HALL_BYPASS.

**Stop here if:** `missed` is ever non-zero, `loop_us_max` is near 2000, `dshot dropped` rises, `imu_invalid` rises, or `ticks` is not 500 (500 or 501 is fine).

## 5. Scope on D8, console, battery check, bench setup (ESC still unplugged)

1. **D8 on CH2** (still no ESC power). Expect one frame every 2.0 ms, 16 bits of 6.67 us each, every high pulse 2.5 us (frame 0x0000). This closes the scope halves of R3.1 and R3.4, and proves the probe can see the frames before you rely on it.
2. **Supply output ON** (10.0 V, 2.0 A, ESC still unplugged). Re-check M8 at the tap (1.80 V), then connect the tap to D9.
3. **Battery reading against the meter** (R3.3's open item). Within 2 s, tlm `vbat` and the status line should read the meter's supply voltage within 2 %. Set the supply to 9.0 V: the reading must follow within 2 %. Then set it back to 10.0 V. If the reading does not move with the supply, the channel is broken: stop; BATTERY_LOW would be acting on garbage. If it reads more than 4 % low at 10.0 V, `arm` will refuse with `battery low` (the guard is above 9.6 V). In that case set the supply to 10.5 V and note it.
4. **Open the console** from the R3.7 checkout:
   ```bash
   cd ~/projects/direcsun-spin && python3 -m http.server 8000
   ```
   Open `http://localhost:8000/ground/console/` in Chrome, in its own window. Type `ws://A.B.C.D/ws` (the address from the boot log) into the endpoint field and **click Connect. Do not press Enter (F1).** Then click an empty part of the page. Serial shows `ws: client N connected (fd ...)`.
5. **Check only one client is connected.** There should be exactly one `client ... connected` line with no matching `disconnected`, for the console.
6. **Bench setup** (never arms; it refuses unless the box is SAFE at throttle 0). In the console's teleop panel, under **Bench setup · Hall bypass**, click **Bench setup**, then **Confirm setup**. It sends three `set_params`: the bench throttle parameters (band 150 to 400, spin-up 300, Hall expect 200), then `bench_mode`, then `hall_installed` false, and it lists each ack. Expect three `set_params ...: ok` lines, a `HALL_BYPASS on` notice, and `BENCH SETUP OK: SAFE, HALL_BYPASS, band [150,400]`. Serial shows `persist: parameters saved to NVS (version 1)`. The console flags now include `HALL_BYPASS`, and the teleop panel's box clamp will read `150 to 400`. The bypass lasts until the box reboots. After any reboot, run this again before arming.

## 6. Dry run with the ESC unplugged

The whole command path runs on the real box while the motor cannot have power. Confirm the ESC power connector is unplugged: the meter reads 0 V on the ESC side.

1. **Arm.** **Julian's explicit go required** (sends `arm`; the ESC is unplugged, so the motor cannot turn). Click Arm, then Confirm arm, then click an empty part of the page. Expect: mode SPINUP. tlm throttle is 0 for 0.24 s, then 48 rising to 300 at 1.5 s. Then mode READY (`cause spinup_complete`) at 2.0 s. On CH2, the frames change from all-short pulses to the ramp, then hold 300 = `0x258F` (`0010 0101 1000 1111`).
2. **Enter MANUAL throttle.** **Julian's explicit go required.** Throttle stays 300 (no step).
3. **Nudge.** **Julian's explicit go required.** Press Up 10 times, one press at a time. "Box sends" rises by 10 per press to 400. An 11th press leaves it at 400. On CH2, 400 = `0x3201` (`0011 0010 0000 0001`). Then press Down 25 times: it stops at 150 (`0x12CF`).
4. **Space test.** Click an empty part of the page, then press Space. Expect `estop: ack ok`, mode SAFE, and an `ESTOP` flag. The throttle falls as throttle_0 x (1 - t/5 s) and is sent as 0 once below 48. From 150 it reads 0 at 3.4 s (4.2 s from 300, 4.4 s from 400). On CH2, all-short pulses again. **If Space does nothing, do not go on:** the e-stop path is not working on this machine.
5. **Tab-close test (rehearsal).** Re-arm (**Julian's explicit go required**), go to READY and MANUAL, then start a silent viewer in another terminal (it never sends, so it cannot keep the box alive):
   ```bash
   cd ~/projects/direcsun-spin
   DIRECSUN_PYTHON=~/projects/DirecSun/.venv/bin/python tools/capture-box.py --silent --url ws://A.B.C.D/ws --seconds 30 --out dry-close.jsonl
   ```
   Close the console tab. Serial shows `ws: client N disconnected`, and within about 1.2 s the status line reads SAFE with a falling throttle (`latched 0x0002`). Reopen the console and click Connect: the box stays SAFE and resumes nothing.

## 7. ESC power on, DShot 0 only

**Julian's go (R3.1).** This cannot spin the motor while the box sends DShot 0. The ESC beeps through the motor windings, and the bell may twitch.

1. Box SAFE, throttle 0, console connected, HALL_BYPASS shown.
2. Supply output OFF. Plug in the ESC power connector. Supply output ON.
3. **Expected tones:** first the ESC's power-on tones (BLHeli_S plays a short rising sequence; inferred, not yet confirmed on this LANRC). Then a low beep and a high beep, which is BLHeli_S arming on zero throttle (TD 6.3). Idle supply current should be tens of mA.
   - No tones at all: the ESC has no power (measure at the ESC input).
   - Power-on tones but no low and high beeps, or a repeating beep: the ESC sees no valid DShot. Check D8 on the scope, the signal ground, and the signal wire. Do not arm.
4. The box did not reboot (no new boot banner), so the bypass is still on. If it did reboot, redo step 5.6.
5. Record the tones you heard (R3.1's "ESC accepts the stream" item).

## 8. The first spin

**Before arming, check every box:**
- [ ] Console shows SAFE, throttle 0, flags include `HALL_BYPASS`; the box clamp will read 150 to 400.
- [ ] Latched flags: none, or only BATTERY_LOW or ESTOP (both clear on `arm`). Never TILT, IMU_SILENT, or CONTROL_STALL.
- [ ] vbat reads 10.0 V (matches the meter); tilt under 30; rate under 5 deg/s; status `missed 0`.
- [ ] Focus on an empty part of the console page, not the endpoint field.
- [ ] CH1 on a motor phase at 5 ms/div, trigger on CH1 with HF reject. CH2 may stay on D8.
- [ ] Supply current display visible. One hand near the supply's output button.
- [ ] Safety glasses on. Nothing within 5 cm of the bell.

**Speed from the scope:** rpm = 60 x f_e / p, where p is the pole-pair count you counted (7 for 14 magnets, so rpm = 8.571 x f_e).

| rpm | 500 | 800 | 1000 | 1200 | 1500 | 1600 | 1800 | **2000** |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| f_e, Hz (p = 7) | 58.3 | 93.3 | 116.7 | 140.0 | 175.0 | 186.7 | 210.0 | **233.3** |
| period, ms | 17.1 | 10.7 | 8.57 | 7.14 | 5.71 | 5.36 | 4.76 | **4.29** |

The phase waveform is chopped by the ESC's PWM at tens of kHz. If the scope's frequency readout shows kHz, it is measuring the PWM. Use HF-reject triggering, or cursors across 7 electrical cycles (one mechanical turn). An independent cross-check at the gate point: film the marker dot on the bell at 240 fps (about 9 frames per turn at 1600 rpm).

1. **Arm.** **Julian's explicit go required** (the motor will spin). Arm, Confirm arm, click empty page. Expect the motor to start once the throttle passes the ESC's start threshold, somewhere after 0.24 s. It reaches throttle 300 at 1.5 s, and READY follows at 2.0 s. Expected f_e at 300: about 135 Hz (about 1160 rpm, ideal estimate). Record f_e and supply current.
2. **Enter MANUAL throttle.** **Julian's explicit go required.** Throttle stays 300.
3. **Up to the bypass ceiling.** **Julian's explicit go required.** Press Up once per second, 10 presses, to 400; f_e should rise with each press (R3.7 item). At 400, wait 5 s, then measure f_e.
   **GATE: f_e at 400 must be below 233 Hz (2000 rpm, p = 7).** If it is 233 Hz or more, press Space now and end the session. `kBypassThrottleMax` must be lowered in code, through a spec, before any further spin.
4. **Throttle-to-speed map, from 400 down.** **Julian's explicit go required** for the series. Five Down presses per step: 400, 350, 300, 250, 200, 150. Hold each step 5 s and record it (section 10). The low end may stutter (sensorless commutation is assumed unreliable below about 800 rpm, A-8). If the motor stalls or stutters, press Up once. If it does not recover, press Space. Record the lowest throttle that ran smoothly.
5. **E-stop test.** Go back up to 300 or 400 (**Julian's explicit go required**), then press Space. Expect `estop: ack ok`, mode SAFE, and an `ESTOP` flag. The throttle falls over 5 s and reads 0 at 4.2 s from 300 or 4.4 s from 400, and f_e on CH1 falls with it. Record how long the motor takes to stop after DShot 0 (BLHeli_S damped light may brake it).
6. **Download the CSV now** (the CSV lives in the tab and is lost when the tab closes). Check the logged maximum throttle never exceeded 400 (R3.7 item):
   ```bash
   python3 -c "import csv,sys; r=list(csv.DictReader(open(sys.argv[1]))); print('rows',len(r),'max throttle',max(int(x['throttle']) for x in r))" ~/Downloads/direcsun-tlm-*.csv
   ```
7. **Tab-close test.** Re-arm (**Julian's explicit go required**), go to READY, then MANUAL at 300. Start `tools/capture-box.py --silent ... --out close.jsonl` as in step 6.5, then close the console tab. Pass if the ramp starts within 1.5 s (R3.7 item). Read the box's own clock from both sides:
   ```bash
   python3 - close.jsonl <<'EOF'
   import json, sys
   prev = None
   for line in open(sys.argv[1]):
       m = json.loads(line)
       if m.get("type") != "tlm": continue
       key = (m["mode"], m["throttle"] == 0)
       if key != prev: print(m["t_ms"], m["mode"], m["throttle"], m["flags"]); prev = key
   EOF
   ```
   Compare the `t_ms` of the first SAFE row with the timestamp on the serial `ws: client N disconnected` line. Expected about 1.0 s, at most 1.2 s.
8. **Optional disarm test:** re-arm, go to MANUAL (**Julian's explicit go required**), then click Disarm. Expect the same 5 s ramp without an ESTOP flag.
9. **Shut down:** supply output OFF, then USB.

## 9. Abort methods, in order of preference

1. **Space** in the console, or click the big **E-stop** button. A 5 s ramp, then DShot 0. Accepted from any client in any state, and it bypasses the command queue. Before relying on Space, click an empty part of the page (F1).
2. **Disarm** button. The same 5 s ramp, without the ESTOP latch.
3. **Bench supply output OFF.** Motor power is gone at once; the motor coasts. This needs no software at all.
4. **Close the console tab, click Disconnect, or turn off the laptop's Wi-Fi.** Heartbeat loss starts the 5 s ramp within about 1.2 s.
5. **Unplug USB or press the XIAO reset button.** The frames stop, and BLHeli_S cuts drive about 0.3 s later (from the BLHeli_S source; not yet confirmed on this LANRC, T-12). The box then reboots SAFE. This is the least preferred method because its behaviour on this ESC is unverified.

**Exception: for anything physical** (smoke, smell, a new noise, the clamp moving, a wire near the bell, a hot part), use **supply OFF first**, then Space. Do not wait out a 5 s ramp.

## 10. What to record

**Session header:** date and time; the commit and the `firmware ...` banner line; reset reason; ESC model; motor label KV; magnet count and pole pairs; supply set-points; divider fitted (and its resistor values); vbat against the meter at 10.0 V and 9.0 V; the ESC tones heard; the D8 scope readings (bit period, '0' high time, frame spacing); the motor's direction of rotation seen from above (CW or CCW, for `s_w` later).

**Throttle-to-speed map (T-5, goes into TD 12.2):**

| Throttle (box sends) | f_e, Hz | rpm = 60 f_e / p | Supply V | Supply A | Smooth? | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 300 (READY) | | | | | | |
| 400 (gate) | | | | | | |
| 350 | | | | | | |
| 300 | | | | | | |
| 250 | | | | | | |
| 200 | | | | | | |
| 150 | | | | | | |

**Stop tests:**

| Test | Throttle before | Time to DShot 0 (tlm) | Motor stop after DShot 0 | Pass? |
| --- | --- | --- | --- | --- |
| Space (e-stop) | | expect 5 x (1 - 48/throttle_0) s: 4.2 s from 300, 4.4 s from 400 | | |
| Tab close | | ramp start after disconnect: expect at most 1.2 s, pass at most 1.5 s | | |
| Disarm (optional) | | same as the e-stop | | |

Also keep: the console CSV (and its maximum throttle), `close.jsonl`, the serial log of the whole session, and any scope screenshots.

## 11. Stop conditions that end the session

End the session (supply OFF, then make it safe) if any of these happens:

- f_e at throttle 400 is 233 Hz or more (2000 rpm or more at p = 7).
- The box reports a throttle above its band (above 400, or above `throttle_band[1]`) at any moment, or the CSV maximum exceeds 400.
- Any stop path misbehaves: Space does not give SAFE; the ramp does not reach 0 by 5 s; the tab close does not start the ramp within 1.5 s; the motor keeps turning after DShot 0.
- The motor turns without an `arm`, or turns at ESC power-up beyond a beep twitch.
- The motor speed does not follow the throttle, or it stalls repeatedly anywhere other than the bottom of the map.
- Smoke, a smell, the fuse opens, the supply hits its current limit in normal running, or the motor or ESC is too hot to keep a finger on.
- The clamp moves, or anything comes within reach of the bell.
- An unexpected latch while running: TILT, IMU_SILENT, BATTERY_LOW with the divider verified and the supply steady, or any CONTROL_STALL (a reboot and a defect).
- The XIAO reboots during a spin (a new banner, or a reset reason other than power-on).
- `missed` is non-zero in the status line, or you lose the serial log or telemetry. Do not continue blind.

## 12. Afterwards

The results feed these items. Mark each `[x]` or `[FAIL]` only for what was actually run:

- R3.7: rung 1 hardware item; throttle-to-speed map; the TD 12.2 rows for the throttle-to-speed map and the motor pole count.
- R3.1: scope on D8; ESC accepts the stream.
- R3.3: battery against the multimeter.
- R3.4: DShot never gaps (boot and 60 s on the scope).
