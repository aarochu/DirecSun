# Test results

Record measured results here, one Markdown file per test session. Use `TEMPLATE.md` as the starting format and name logs `YYYY-MM-DD-component-test.md`. Keep raw scope captures, photos, and CSV files beside the corresponding log, with links from it. Do not mark a test passed until it was actually run.

The planned procedures and acceptance criteria live in `docs/technical-design.md` §9 and the relevant `changes/` feature spec. This folder records observations; it does not replace those contracts. Any step that can turn the motor requires Julian's explicit go every time under `changes/CONTEXT.md`.

## Components to cover

| Component or subsystem | Planned evidence | Status |
| --- | --- | --- |
| XIAO ESP32-C3 spare bench controller | Board ID, firmware target/commit, pin map, USB boot and output checks | [Partial; boot, pin map and scan output only, no commit](2026-10-10-i2c-scan-esp32c3.md) |
| XIAO ESP32-S3 flight controller | Boot, DShot output, sensor bus, watchdog and telemetry checks | Partial: [boot passes](2026-10-10-s3-first-boot.md); control task [failed its first run](2026-10-10-s3-control-task-first-run.md) and [passes after the fixes, timing without Wi-Fi](2026-10-10-s3-control-task-fixed.md); [stall watchdog resets within 150 ms, scope half not run](2026-10-10-s3-stall-watchdog.md); [the barometer read costs about 135 µs of tick margin](2026-10-10-s3-loop-time-barometer.md); [tick 752 to 760 µs with the Wi-Fi radio up after V-62, 45 min; a 4 ms tick in the first second of 16 of 25 boots, open](2026-10-10-s3-control-tick-wifi.md); [joins the bench network and logs its IP and URL](2026-10-10-s3-wifi-station.md); [first-spin dry run with the ESC unpowered passes its command path; an unplanned heartbeat loss and 2 s over 1000 µs, open](2026-10-11-s3-first-spin-dry-run.md). DShot on the scope not run |
| LANRC 35A BLHeli_S ESC | Settings, DShot150 zero-stream/arming tones, T-5 steps, T-12 stop behavior | [**Fail on DShot150: two ESCs spun the motor in bursts on a zero stream while the box was SAFE and never armed; still for 30 s on DShot300 (V-68); cause open**](2026-10-11-esc-dshot150-spins.md) |
| 2212 brushless motor | Mounting, direction, current, RPM versus throttle, temperature | Not run (unplanned motion only: [ESC spins](2026-10-11-esc-dshot150-spins.md)) |
| A1104 Hall sensor and magnets | Polarity, edge count, speed cross-check (T-6) | [Partial: south face identified, one edge per south hold and none for north; speed not run, deferred to the flywheel](2026-10-10-a1104-hall.md) |
| MPU-6050 gyro/accelerometer | Address, axis/sign, bias/noise and vibration (T-7/T-8) | [Partial: WHO_AM_I 0x68 on the S3, values at rest, accel norm 0.871 g; axis sign, T-7 and T-8 not run](2026-10-10-s3-sensor-bus.md) |
| QMC5883P magnetometer | Address, axes, calibration at three wheel speeds (T-9) | [Partial: chip ID 0x80 on the S3, raw values at rest; 360° loop and T-9 not run](2026-10-10-s3-sensor-bus.md) |
| BMP280 | Address and pressure/temperature sanity check | [Pass on the S3: chip ID 0x58 at 0x77, about 24.7 °C and 102.68 kPa; first 4 readings stale](2026-10-10-s3-sensor-bus.md); [dropped off after a replug, back after rewiring](2026-10-10-s3-i2c-scan-bmp280.md) |
| Three-sensor I²C bring-up | Individual driver reports, address scan, and combined 400 kHz bus | [Partial; shared bus inconclusive](2026-10-10-i2c-sensors.md); [ESP32-C3 scan: each sensor and three-device bus detected at 100 kHz, full three-board chain failed](2026-10-10-i2c-scan-esp32c3.md); [S3: all three on one chain, each configured for 400 kHz, 73 s soak with 0 errors; SCL not scoped](2026-10-10-s3-sensor-bus.md); [IMU lost its configuration before the rewire; all OK on resets after it; IMU silent after some power-on boots, open](2026-10-11-s3-sensor-boot.md) |
| Two OP550 sun sensors | ADC levels and angle response (T-10) | Not run ([inputs not wired; the readings seen were a floating pin](2026-10-10-s3-adc-unwired.md)) |
| 8 × AA NiMH pack and holder | Cell order, polarity, open-circuit and loaded voltage, contact heating | Not run |
| 5 V buck and Schottky path | Output voltage and polarity, load and brownout checks | Not run |
| Fuse, 470 µF capacitor, wiring and battery divider | Continuity, polarity, fuse value, capacitance rating, ADC ratio | Battery divider: [pass on the V-67 build, within 0.2 % at 10.01 V and 0.1 % at 9.00 V](2026-10-11-battery-divider.md) (earlier: [not fitted, a floating pin](2026-10-10-s3-adc-unwired.md)). Fuse and capacitor not run |

## Software and integration checks

These run without the box, but they are tests all the same: each log names its instrument check, and its raw output sits beside it.

| Subject | Status |
| --- | --- |
| CI host-test selftest killed with exit 143 (#57) | [Reproduced: unbounded `-j` started 538 compilers and hit the OOM killer; fixed by capping the jobs, 19 of 19 mutants caught](2026-10-11-ci-host-test-oom.md) |
| Operator heartbeat with the tab hidden (#33) | [Pass for #33's Worker: 5.0 Hz hidden, box stayed READY; `main`'s page timer tripped `HEARTBEAT_LOST`](2026-10-11-ui-hidden-tab-heartbeat.md) |
| R3.5 kit against the parity mock (#50) | [Fail: 42 of 43; the kit's t3 timing assumed a 2 s spin-up; carried into #86](2026-10-11-r35-mock-suite-parity.md) |
| UI turn recorder and capture checker (#51) | [Fail, fixes decided: a failed run hung, headless starved, the loopback check was loose, and the old ping rule failed good runs; carried into #86](2026-10-11-ui-recorder-linux.md) |
| Ground station display lag (#78, #82) | [#82 bounds it: headless lag no longer grows; `main` reached 56 s](2026-10-11-ui-display-lag.md) |
| R4.1 UI items with a GPU, and the R3.5 kit on #86 | [Pass: turn, stop, estop, watch and the hidden tab (plain Chrome over CDP) with the new ping rule; kit 41 of 43, the 2 failures predate #86](2026-10-11-r41-ui-items-gpu.md) |
| Approved UI PRs merged together | [Pass after two fixes: turn readiness under the coalesced render, and the parameters panel under the plots panel](2026-10-11-ui-stack-integration.md) |

Update this index only from linked session logs. Keep failed and inconclusive runs instead of overwriting them.
