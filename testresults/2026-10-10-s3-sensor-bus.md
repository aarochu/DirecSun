# Test: three-sensor I²C bus on the XIAO ESP32-S3 (R3.2)

- Date/time and timezone: 2026-10-10, 17:17:43 (America/New_York), a 75 s capture.
- Operators and witnesses: Julian (bench rig); Julian's Claude Code agent (build, flash, serial capture, analysis).
- Julian's explicit go for any motor-turning step: Not applicable. No ESC or motor was connected.
- Component model, markings, and serial/lot: XIAO ESP32-S3 (MAC e0:72:a1:fc:49:5c); Adafruit MPU-6050 (#3886), QMC5883P (#6388) and BMP280 (#2651), chained with STEMMA QT cables.
- Firmware branch/commit and ESP32 target: `feat/set3/R3.2-sensor-bus` at fa407e5, the `firmware/test_apps/sensors` app, ESP-IDF v6.0, esp32s3.
- Procedure and acceptance-criterion reference: `changes/3-teleop/R3.2-sensor-bus.md`, Tests (boot probe; 400 kHz; 60 s soak); FD §9.2 for the burst-time check.
- Rig setup, wiring revision, guard, and stop method: breadboard; SDA on D4 (GPIO5), SCL on D5 (GPIO6). The bus is owned by i2cdev, with each device configured for 400 kHz. Nothing can move; the stop method is unplugging USB.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V; the sensors run from the XIAO's 3V3 pin. Not measured.
- Instruments, calibration or positive-control check: the app's own counters over the serial log. The chip-ID reads are their own positive control: a wrong address, a dead device or a stuck bus cannot return the right ID. The burst time is an indirect clock check (FD §9.2): about 1550 µs would mean 100 kHz, about 180 µs would mean 1 MHz. The oscilloscope check of SCL was not run.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Boot probe | MPU-6050 WHO_AM_I 0x68, QMC5883P chip ID 0x80, BMP280 chip ID 0x58 at 0x77; one shared bus, no "bus already in use" error | 0x68, 0x80, 0x58 | Pass |
| 2 | IMU reads at 500 Hz plus magnetometer polling for 73 s | 500 IMU reads every second; 0 invalid, 0 errors, 0 slow reads, 0 bus resets. Magnetometer 192 to 195 reads/s, 0 errors, 0 overflows | 0 errors and 0 resets over 60 s | Pass |
| 3 | IMU burst time, per second | Median 453 µs in every second. Max 997 µs in the first second, at most 629 µs after | Median about 400 µs at 400 kHz; max under 1 ms | Pass for the burst-time method; the SCL scope check was not run |
| 4 | Raw values at rest, board flat | Gyro about (-1.4, +0.8, -2.1) deg/s; accel (+0.033, -0.025, +0.871) g; magnetometer raw about (-1305, +525, -2795) | A gyro offset of a few deg/s; an accel norm near 1 g | Observation: the accelerometer norm reads 0.871 g, 13% low |
| 5 | BMP280 readings | The first 4 readings repeat 27.50 °C, 67479 Pa; from 1.36 s, about 24.7 °C, 102681 to 102686 Pa | Plausible room values from the first reading | Minor defect: stale first readings |

## Outcome

- Overall: **Pass** for the boot probe and the soak. The "400 kHz" item is half done (burst time only), and the gyro sign, magnetometer loop and cable-pull fault paths were not run.
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - The BMP280's first four readings are stale; telemetry should discard readings until the first completed measurement. It does not affect control, because the barometer never feeds it.
  - The 0.871 g accelerometer norm later broke the R3.4 gyro-bias gate; see [2026-10-10-s3-control-task-first-run.md](2026-10-10-s3-control-task-first-run.md).
- Raw data and photos: [serial log](2026-10-10-s3-sensor-bus/serial-sensors-app.txt).
- Follow-up owner and action: Julian. Scope SCL (2.5 µs period, rise under 300 ns); gyro sign by hand rotation; a 360° magnetometer turn; the cable-pull fault paths (all in R3.2's Tests).

This session supersedes the open question in [2026-10-10-i2c-sensors.md](2026-10-10-i2c-sensors.md) for the S3: all three boards answered on one bus with each device configured for 400 kHz, and a 73 s soak ran clean. The SCL frequency itself is still unmeasured.
