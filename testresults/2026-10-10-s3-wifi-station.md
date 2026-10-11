# Test: Wi-Fi station join on the bench network, first boot (R3.5)

- Date/time and timezone: 2026-10-10, 23:00 to 23:09 (America/New_York): three build-and-flash attempts, the last with a 28 s boot capture at 23:08.
- Operators and witnesses: Julian (provided the bench network's credentials; not at the board); Julian's Claude Code agent (credentials file, build, flash, serial capture). Written up afterwards from the session transcript and the raw capture by a separate Claude Code agent.
- Julian's explicit go for any motor-turning step: Not applicable. No ESC was connected; DShot 0 only.
- Component model, markings, and serial/lot: XIAO ESP32-S3, MAC e0:72:a1:fc:49:5c (flash log), station MAC the same (boot log), with the three I²C sensors.
- Firmware branch/commit and ESP32 target: `feat/set3/R3.5-comms` at 3799644 (station mode, the V-62 fix included), compiled 23:07:08, 0 warnings. ESP-IDF v6.0, esp32s3. The credentials live only in the gitignored `firmware/sdkconfig.defaults.local`; they appear nowhere in this log or its capture.
- Procedure and acceptance-criterion reference: `changes/3-teleop/R3.5-comms.md`, Tests, "Wi-Fi and mDNS on the bench network: the boot log shows the IP and URL; `ping direcsun.local` resolves from the laptop; both the bench console and the main UI's Device link panel connect at `ws://direcsun.local/ws`."
- Rig setup, wiring revision, guard, and stop method: the breadboard rig on USB. Nothing could move.
- Power source, set voltage, current limit, and measured voltage: laptop USB 5 V. Not measured.
- Instruments, calibration or positive-control check: the boot log, read by `rawts.py`. The firmware's own failure messages are the positive control: the two failed attempts below were reported clearly (an empty-SSID error, then a repeating disconnect with reason 201), so a silent failure would have been visible. The laptop was not on the bench network during this session, so nothing was checked from the laptop side.

## Steps and observations

| Step | Command or condition | Measured value and units | Expected | Result (pass/fail/inconclusive) |
| --- | --- | --- | --- | --- |
| 1 | Build and flash with the credentials file present (23:00) | `E (1326) wifi: no Wi-Fi network configured`. The build reused a config generated before the file existed, so the SSID was empty | Joins the network | Fail (set-up error, not firmware). Fixed by deleting the generated config |
| 2 | Rebuild, flash (23:01 to 23:05) | `joining "..."`, then `disconnected (reason 201), retrying in 1 s` every 3.4 s, from 4.0 s to at least 34.7 s after boot. Reason 201 is "no access point found": the network name had been typed with one letter wrong | Joins | Fail (set-up error). Fixed by correcting the name |
| 3 | Rebuild, flash, boot capture (23:05 to 23:08) | Associated at 2724 ms after boot (channel 8, WPA2-PSK, RSSI -40 dBm). `sta ip: 192.168.8.224` at 6766 ms. Boot log: `mDNS hostname direcsun.local` and `WebSocket: ws://direcsun.local/ws  or  ws://192.168.8.224/ws` | IP and URL in the boot log | **Pass** |
| 4 | Tick timing during the join, from the status lines | First second 3008 µs worst tick (no missed ticks); second 2, during association, 1811 µs; second 5, around DHCP, 1216 µs; every other second 754 to 755 µs. 0 missed ticks, 0 dropped frames, 0 invalid IMU reads over 28 s | Under 1000 µs (R3.4) | Observation: three seconds over 1000 µs while joining, none after |
| 5 | `ping direcsun.local` from the laptop; the console and the main UI connect | Not in this session: the laptop stayed on another network. Later evidence: on 2026-10-11 at 04:15 the laptop resolved `direcsun.local` to this board's 192.168.8.224 and pinged it, and the bench console connected at 04:21 ([2026-10-11-s3-first-spin-dry-run.md](2026-10-11-s3-first-spin-dry-run.md)) | Resolves and connects | Not run here |

## Outcome

- Overall: **Pass for the boot-log half** of R3.5's Wi-Fi and mDNS item: the board joins the bench network and logs its IP and URLs. The laptop-side checks were not run in this session.
- Faults, unexpected motion, heat, noise, or supply current limiting: none from the firmware. Both failures were configuration mistakes on the agent's side, each reported plainly by the firmware. The second XIAO ESP32-S3 (e0:72:a1:fc:49:60) also joined the same network on an R3.7 build at 00:52 the next morning, as 192.168.8.228 ([capture](2026-10-11-s3-sensor-boot/1-s3-4960-no-sensors-0050.txt)).
- Raw data and photos: [boot capture, 23:08](2026-10-10-s3-wifi-station/station-boot-2308.txt). The network name and the access point's BSSID are scrubbed to `<bench-network>` and `<bssid>`. The first two attempts were not saved as files; their lines above are from the session's tool output.
- Follow-up owner and action: Julian's agent. The rest of R3.5's item: `ping direcsun.local` by name, and the main UI's Device link at `ws://direcsun.local/ws`.
