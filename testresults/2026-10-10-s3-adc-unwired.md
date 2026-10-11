# Test: battery and sun ADC inputs on the XIAO ESP32-S3, with nothing wired to them (R3.3)

- Date/time and timezone: 2026-10-10, 17:19:51 (America/New_York), a 25 s capture.
- Operators and witnesses: Julian's Claude Code agent (build, flash, serial capture). Julian later corrected the interpretation (see Outcome).
- Julian's explicit go for any motor-turning step: Not applicable. No ESC or motor was connected.
- Component model, markings, and serial/lot: XIAO ESP32-S3 (MAC e0:72:a1:fc:49:5c). **No battery divider, OP550s or A1104 were connected.**
- Firmware branch/commit and ESP32 target: `feat/set3/R3.3-hall-and-adc` at 872f624, the `firmware/test_apps/hall_adc` app, ESP-IDF v6.0, esp32s3.
- Procedure and acceptance-criterion reference: `changes/3-teleop/R3.3-hall-and-adc.md`, Tests (battery ADC against the multimeter at 10.0 V and 9.0 V; sun ADC covered and uncovered). Neither could be run: their inputs were not wired.
- Rig setup, wiring revision, guard, and stop method: the XIAO and the three I²C sensors on the breadboard. D9 (GPIO8, battery), D0 (GPIO1, sun A), D1 (GPIO2, sun B) and D10 (GPIO9, Hall) were all unconnected. Nothing can move.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. No bench supply was connected.
- Instruments, calibration or positive-control check: none. The R3.3 battery item requires the reading to follow a known source at two voltages against a meter. That was not done, which is how the readings below were misread.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Battery channel, GPIO8 floating | Pin 1816 to 1957 mV, which the divider ratio turns into "10.07 to 10.85 V" | No meaning: nothing is connected | Invalid: floating input |
| 2 | Sun channels, GPIO1 and GPIO2 floating | A 654 to 1336 mV, B 601 to 969 mV | No meaning: nothing is connected | Invalid: floating input |
| 3 | Hall edges, GPIO9 with the internal pull-up and nothing connected | 0 edges | 0 | Consistent, but proves nothing about the A1104 |

## Outcome

- Overall: **Invalid.** No source was wired to any of these inputs, so no reading here is a measurement.
- Faults, unexpected motion, heat, noise, or supply current limiting:
  - At the time, the "10.6 V" figure was wrongly read as the bench supply. Two firmware defects were filed on the strength of it: a 5.09 V boot-window average latching BATTERY_LOW in the R3.4 flight build, and a mismatch with this app. Julian corrected it ("we never had the escs wired up nor a battery divider in place"), and both defects were withdrawn in the R3.4 log. A garbage battery reading latching BATTERY_LOW is correct behaviour.
  - Lesson: floating ADC pins return stable, plausible values. An analog reading counts only after it has followed a known source.
- Raw data and photos: [serial log](2026-10-10-s3-adc-unwired/serial-hall-adc.txt).
- Follow-up owner and action: Julian. Fit the 100k/22k divider on D9 and step the bench supply between 10.0 V and 9.0 V against the multimeter (R3.3 battery item). The OP550s on D0/D1 are needed only in Set 5. The Hall half of R3.3 ran later; see [2026-10-10-a1104-hall.md](2026-10-10-a1104-hall.md).
