# Test: I²C sensor boot probe across the night's boots, before and after the rewire (R3.2)

- Date/time and timezone: 2026-10-11, 00:50 to 04:32 (America/New_York): 11 captured boots, plus two power-on boots seen only through what followed them (02:51 and 03:37).
- Operators and witnesses: Julian (wiring, the 02:16 to 02:36 rewire, plugging boards and sensors); Julian's Claude Code agent (resets, serial capture). Written up afterwards from the session transcript, the raw captures and the kernel's USB log by a separate Claude Code agent.
- Julian's explicit go for any motor-turning step: Not applicable. No step in this log commanded the motor; the ESC was powered at times on DShot 0 (see [2026-10-11-esc-dshot150-spins.md](2026-10-11-esc-dshot150-spins.md)).
- Component model, markings, and serial/lot:
  - XIAO ESP32-S3 e0:72:a1:fc:49:60 (on USB 00:49 to 01:13) with no sensors attached.
  - XIAO ESP32-S3 e0:72:a1:fc:49:5c (on USB from 01:13) with the MPU-6050, QMC5883P and BMP280 on one STEMMA QT chain; inferred to be the boards in [2026-10-10-s3-sensor-bus.md](2026-10-10-s3-sensor-bus.md).
  - The A1104 Hall input initialised on GPIO9 at every boot. No Hall edges were tested.
- Firmware branch/commit and ESP32 target (ESP-IDF v6.0, esp32s3):
  - 00:50, board 49:60: R3.7 (`feat/set3/R3.7-manual-throttle`) built from the 912aed8 tree; its version string reads 65f9c11.
  - 02:04 to 03:49, board 49:5c: R3.5 (`feat/set3/R3.5-comms`) at 3799644.
  - 03:55 to 03:58: R3.7 scratch builds (`912aed8-dirty`).
  - From 04:03: R3.7 built from the b9fe88d tree; version string 65f9c11.
- Procedure and acceptance-criterion reference:
  - `changes/3-teleop/R3.2-sensor-bus.md`, Tests: the boot probe (WHO_AM_I 0x68, QMC chip ID 0x80, BMP280 chip ID 0x58 at 0x77, one bus) and the fault-path item (a missing device is reported and the rest still read).
  - TD §5.3 (sensors on the XIAO's 3V3). FD §14.1 (IMU_SILENT; arming needs the IMU).
  - `tools/bench/first-spin/RUNBOOK.md` §11 lists an unexpected IMU_SILENT as a stop condition.
- Rig setup, wiring revision, guard, and stop method: the breadboard rig. Before 02:16 the sensor chain's supply pin was in question: at 02:10 Julian asked whether the sensors belonged on 5 V or 3V3, and which pin the IMU had been on was never reported. From 02:36 everything ran on the rewired breadboard, with the sensors on 3V3 per the agent's wiring table; nobody reported the pin or measured the voltage.
- Power source, set voltage, current limit, and measured voltage: the XIAO from laptop USB; the sensors from the XIAO's 3V3 pin after the rewire. Not measured.
- Instruments, calibration or positive-control check:
  - The firmware's boot probe lines, and in the status lines the interlock latch (bit 0x0020 is IMU_SILENT, bit 0x0010 BATTERY_LOW) and the per-second count of invalid IMU reads.
  - The kernel's USB log, to tell a power-on boot (USB re-plugged or dropped out) from a chip reset over USB (`rst:0x15`).
  - Positive control: the same probe reports each device present on the good boots and absent on the board with no sensors, so its answers distinguish the two.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Board 49:60 with no sensors attached, chip reset (00:50:41) | IMU: no answer at 0x68 after 4 retries, `IMU_MISSING: the box stays in SAFE`. Magnetometer: chip ID 0x00, MAG_MISSING. Barometer: ESP_ERR_NOT_FOUND, chip ID 0x00. Gyro bias not seeded. The probes took until 12.2 s after boot; DShot zeros ran from 40 ms | Every device reported missing; box held in SAFE | Pass for the missing-device path (all three absent, so not R3.2's one-cable fault test) |
| 2 | Board 49:5c, chip reset (02:04:11) | IMU answers WHO_AM_I 0x68, but `read-back 0x1A = 0x00, expected DLPF_CFG 4`: ESP_ERR_INVALID_RESPONSE, IMU_MISSING, SAFE. Magnetometer 0x80 OK. Barometer 0x58 OK | All three OK | **Fail** |
| 3 | Chip reset after Julian reseated a connection (02:13) | Same IMU read-back failure; magnetometer and barometer OK | All three OK | **Fail** |
| 4 | After the full rewire, chip reset (02:36:31) | IMU 0x68 OK, magnetometer 0x80 OK, barometer 0x58 OK. Gyro bias seeded at -2.096 deg/s from 425 still samples; accel reference 0.870 g | All three OK | **Pass** |
| 5 | Running, 02:37:07 to 02:51:52 (886 s) | 0 invalid IMU reads; barometer OK on every line | No errors | Pass |
| 6 | **Power-on boot**: USB dropped out at 02:51:52 and returned at 02:51:55 (kernel log). The probe lines were lost while the monitor reconnected | From 02:51:56: `gyro z bias not seeded (0 samples)`, IMU_SILENT latched, 481 invalid IMU reads in the first second and 500 in every second after, 154,481 in all, until the capture ended at 02:57:04. First second: worst tick 40,942 µs, 2 missed ticks. Barometer OK | All three OK | **Fail.** The session reported this capture as having no resets; it missed this reboot |
| 7 | Power-on boot at 03:37:53 (USB re-plugged while the ESC and motor were swapped); boot not captured | 03:39:20 to 03:48:44 (565 s): 500 invalid IMU reads every second (282,500), IMU_SILENT and BATTERY_LOW latched. Barometer OK until the BMP280 was unplugged at about 03:47:55 | All three OK | **Fail** |
| 8 | Chip reset with the BMP280 unplugged (03:48:53) | IMU 0x68 OK. The magnetometer and barometer lines were lost: the session's reader collided with a leftover monitor on the serial port. Status lines show the barometer absent; 0 invalid IMU reads through 03:55 | IMU OK, barometer absent | Pass for the IMU; magnetometer not shown |
| 9 | Chip resets of three R3.7 scratch builds (03:55:53, 03:57:49, 03:58:36) and the R3.7 V-68 build (04:03:40), BMP280 still unplugged | IMU 0x68 and magnetometer 0x80 OK every time. Barometer `ESP_ERR_INVALID_RESPONSE (chip ID 0x08)` every time, then shown absent | Barometer reported missing | Observation: with no BMP280 on the bus, the probe reads ID 0x08 rather than not-found (board 49:60, with no sensors at all, read 0x00). The box treats it as absent either way. Unexplained |
| 10 | BMP280 plugged back in with the board running (04:05) | Barometer stays absent until the next boot | The probe runs at boot only | Observation |
| 11 | Chip reset with everything connected (04:10:05) | IMU 0x68, magnetometer 0x80, barometer 0x58 all OK. Gyro bias seeded at -2.096 deg/s from 425 still samples; accel reference 0.870 g | All three OK | **Pass** |
| 12 | Running, 04:18:45 to 04:32 | 0 invalid IMU reads; barometer OK on every line | No errors | Pass |
| 13 | Pattern after the rewire | Every captured chip reset found the IMU (7 of 7: 02:36, 03:48, 03:55, 03:57, 03:58, 04:03, 04:10). Both power-on boots whose aftermath was captured (02:51, 03:37) left it silent | | **Open.** A power-up timing problem at the MPU-6050 would explain it: the probe running before the IMU is ready after power is applied. Inferred, not shown; two power-on boots is a small sample |

## Outcome

- Overall: **Pass on chip resets after the rewire; fail, open, on power-on boots.** Before the rewire the IMU answered its ID but lost its configuration (steps 2 and 3).
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - With the IMU missing or silent the box stayed in SAFE every time and could not arm, the intended outcome.
  - A box that loses its IMU after an unplanned power cycle cannot arm until it is reset. That matters for the demo, where the box will start from power-on.
- Raw data and photos (terminal colour codes stripped, the network name and BSSID scrubbed):
  - [step 1](2026-10-11-s3-sensor-boot/1-s3-4960-no-sensors-0050.txt)
  - [step 2](2026-10-11-s3-sensor-boot/2-imu-readback-fail-0204.txt), [step 3](2026-10-11-s3-sensor-boot/3-imu-readback-fail-again-0212.txt)
  - [step 4](2026-10-11-s3-sensor-boot/4-after-rewire-0236.txt)
  - steps 5 and 6: [the 02:37 session](2026-10-11-esc-dshot150-spins/2-esc1-session-0237.txt.gz)
  - step 7: [the 03:39 session](2026-10-11-esc-dshot150-spins/3-esc2-session-0339.txt.gz)
  - [step 8](2026-10-11-s3-sensor-boot/5-bmp280-unplugged-0348.txt)
  - step 9: [03:58](2026-10-11-s3-sensor-boot/6-bmp280-unplugged-r37-0358.txt), [04:03](2026-10-11-esc-dshot150-spins/6-v68-build-boot-0403.txt)
  - [step 11](2026-10-11-s3-sensor-boot/7-all-sensors-0410.txt)
  - step 12: [the dry run](2026-10-11-s3-first-spin-dry-run/dry-run-0418-0432.txt.gz)
- Follow-up owner and action:
  - Julian's agent: reproduce the power-on failure deliberately (unplug and replug USB several times with the sensors connected) and read the probe lines. If it is a power-up delay, wait for the IMU before the probe, or retry the configuration.
  - Julian: none until that run.
