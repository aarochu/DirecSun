# R3.5 hardware tests: runbook

One command per R3.5 Tests item (`changes/3-teleop/R3.5-comms.md` on `feat/set3-teleop`), plus R3.4's timing-budget item, which runs on the R3.5 build. Every script takes the box URL, prints `PASS`, `FAIL`, or `BLOCKED` with the numbers the item asks for, and writes everything it saw to its own directory under `$R35_OUT`.

| Step | Item | Command | Time | Julian does | Arms? |
| --- | --- | --- | --- | --- | --- |
| 1 | (setup) serial capture | `./serial-capture.sh build flash` | build + boot | credentials, USB, rig state | no |
| 2 | Wi-Fi and mDNS | `./t1-mdns.py --url $R35_URL --serial-log $R35_OUT/serial.log` | 30 s | nothing | no |
| 3 | Firmware traffic matches the schema | `./t2-schema.py --url $R35_URL` | 70 s | nothing | no |
| 4 | Main UI reads the real box | `./t3-ui.py --url $R35_URL` | 30 s | turns the rig clockwise by hand at "TURN NOW", then checks the screenshots | no |
| 5 | Read-only client sees only tlm | `./t4-readonly.py --url $R35_URL` | 80 s | nothing | no (e-stop only) |
| 6 | T-11, and R3.4 timing with Wi-Fi | `./t7-timing.py --url $R35_URL --serial-log $R35_OUT/serial.log` | 10 min | leaves the rig still | no |
| 7 | Controlling client and heartbeat | `./t6-control.py --url $R35_URL --confirm-arm esc-unplugged` | 20 s | confirms the ESC is unplugged, feeds the battery divider | **yes** |

`t5-t11.py --url $R35_URL` runs T-11 alone (10 min). Step 6 already judges T-11 on its own stream and prints a second `RESULT t5-t11` line, so step 6 covers both items in one 10-minute run.

Exit codes: 0 PASS, 1 FAIL, 2 BLOCKED or not run (a precondition is missing, or a confirmation flag was not given). The last two lines of every run are `RESULT <item>: <verdict> (...)` and the path of its directory, which holds `output.txt` (everything printed), `result.json` (every check), the raw captures (`*.jsonl`), the tools' own outputs, and the screenshots.

## Before the box: setup (done once; no hardware)

```bash
cd tools/bench/r35  # in the checkout that gets flashed
export R35_UI=<checkout of origin/main>/frontend/ground-station
. ./env.sh          # R35_REPO (this checkout), DIRECSUN_PYTHON, R35_OUT, R35_URL; export your own values first to override
./prep.sh           # npm ci if needed, builds the main UI, runs check-schema and mock-box --selftest, checks mDNS on the laptop
```

- `R35_REPO` defaults to the checkout the kit lives in, which must be the one that gets flashed (`feat/set3/R3.5-comms` or later). The kit uses its `tools/capture-box.py`, `tools/check-schema.py`, `protocol/schema-v1.json`, and `ground/console`, so the schema the box is judged against is the one built into it. Check `git -C $R35_REPO log -1` is the commit under test. If the branch has moved, point `R35_REPO` at the new checkout and rerun `prep.sh`.
- `R35_UI` is `frontend/ground-station` from a checkout of `origin/main` (the main UI's operator client, Aaron's; PR #28 moved it from `frontend/draft`). `prep.sh` builds it into `dist/`; rerun `prep.sh` if `main` moves.
- The UI checks drive Chrome headless with GPU rendering (Vulkan). The ground station's CAD view, rendered in software while telemetry streams, starves the page and times out every probe call. On a laptop without Vulkan, expect t1, t3, and t4 to fail that way; that says nothing about the box.
- `prep.sh` must end with `prep: all ok`. Its last check proves the laptop resolves its own `<hostname>.local`: without that, a failure to resolve `direcsun.local` says nothing about the box.

## Physical state for the whole session

- The XIAO on USB to the laptop. The laptop on the Wi-Fi network named in the credentials.
- **The ESC physically disconnected: signal off D8 and its power off.** Steps 1 to 6 never arm, but step 7 does.
- The rig on the turntable, level (the TILT interlock trips past 30 degrees) and still (arming needs under 5 deg/s).
- For step 7 only: the battery divider (100k/22k on D9) fed from the bench supply at 10 to 12 V. The arm guard needs the 1 s battery average above 9.6 V, and the BATTERY_LOW interlock trips below 8.8 V. With D9 floating, `arm` is likely refused and step 7 reports BLOCKED.
- No other WebSocket client open: close the bench console and every ground-station tab. The box takes at most 3 clients, and step 5 uses all 3.

## Step 1: credentials, build, flash, and the serial capture (Julian)

```bash
cp $R35_REPO/firmware/sdkconfig.defaults.local.example $R35_REPO/firmware/sdkconfig.defaults.local
$EDITOR $R35_REPO/firmware/sdkconfig.defaults.local     # CONFIG_DIRECSUN_WIFI_SSID, CONFIG_DIRECSUN_WIFI_PASSWORD
rm -f $R35_REPO/firmware/sdkconfig                      # or the old values win (CONTEXT.md)
./serial-capture.sh build flash                         # in its own terminal; leave it running
```

`serial-capture.sh` runs `tools/idf.sh -p /dev/ttyACM0 build flash monitor` from `$R35_REPO` under `script -f`, so `$R35_OUT/serial.log` grows line by line while the monitor shows the same output. Use `R35_PORT=/dev/ttyACMx` for another port, and `./serial-capture.sh --dry-run build flash` to see the command without running it. Leave it running through step 6; quit with Ctrl+] at the end. If the board was already flashed, `./serial-capture.sh` alone monitors, and the monitor's reset gives a fresh boot log.

Expected in the monitor within a few seconds of boot (format strings in `direcsun_comms/src/wifi.cpp` and `ws_server.cpp`):

```
I (...) wifi: mDNS hostname direcsun.local
I (...) ws: WebSocket server on port 80 at /ws (up to 3 clients)
I (...) wifi: connected, address 192.168.x.y
I (...) wifi: WebSocket: ws://direcsun.local/ws  or  ws://192.168.x.y/ws
I (...) status: s 1 ticks 500 missed 0 (total 0) loop_us_max ... baro ok
```

`wifi: no Wi-Fi network configured` means the credentials did not reach the build (delete `firmware/sdkconfig` and rebuild). Repeated `wifi: disconnected (reason N)` lines mean the box cannot join: check the SSID, the password, and 2.4 GHz.

## Step 2: Wi-Fi and mDNS (t1)

```bash
./t1-mdns.py --url $R35_URL --serial-log $R35_OUT/serial.log
```

Checks: the boot log's IP line, URL line, mDNS hostname, and server line; the laptop resolves its own `.local` name (instrument check); `ping -c 3 direcsun.local` resolves to the boot log's address; a raw client gets a `tlm`; the bench console (`ground/console`, served from `$R35_REPO`) shows CONNECTED and a tlm seq; the main UI's Device link shows DEVICE CONNECTED with the ignored counter at 0.

Expected: `RESULT t1-mdns: PASS (boot IP 192.168.x.y, URL ws://direcsun.local/ws)`. Screenshots of the console and the UI are in `probe-console/` and `probe-connect/`.

If the resolver check passes but `direcsun.local` does not resolve, the box's mDNS is not answering (look for `wifi: mDNS failed` in the log). If `ping` resolves but the raw client cannot connect, look at the laptop's firewall and the `ws:` lines in the monitor.

## Step 3: firmware traffic matches the schema (t2)

```bash
./t2-schema.py --url $R35_URL
```

Runs `tools/capture-box.py --seconds 60` (pings at 5 Hz, so it gets pongs) and, alongside it, a second client that once a second sends a frame the box must refuse with an ack and never act on: `disarm` while SAFE, malformed JSON, an unknown `cmd`, `v` 2, and an unknown `type`. capture-box alone never sends a command, so its capture can hold no ack; the item asks for acks too. Both captures go through `tools/check-schema.py`. The box must be SAFE; nothing here can arm it.

Expected: about 3000 tlm (at least 2900), about 300 pongs, one ack per refused frame (about 59), 0 accepted, 0 schema failures. Expected ack reasons from the firmware (`protocol.cpp`, `modes.cpp`): `not armed`, `malformed JSON` (seq 0), `unknown cmd`, `unsupported v`, `unknown type`.

## Step 4: the main UI reads the real box (t3) (Julian turns the rig)

```bash
./t3-ui.py --url $R35_URL
```

The main UI opens in headless Chrome, connects, and the terminal beeps and prints:

```
>>> TURN THE RIG CLOCKWISE NOW, about 90 degrees, then hold it still <<<
```

**Julian: turn the rig clockwise seen from above, about 90 degrees, over 2 to 5 seconds, then hold it still** until sampling ends (15 s, `--turn-s` to change). The script samples the HEADING readout and the dial needle ten times a second, and Chrome records every frame the page receives and sends.

Expected: `device mode` with `SAFE / DEVICE`, ignored counter 0, only `tlm` on the wire, 0 frames sent by the page, heading and needle both about +90 (at least +30) and within 5 degrees of each other. A result of about -90 after a clockwise turn means the wire sign is wrong (`encode_tlm` is not negating).

**Then check by eye** the screenshots in the run's `probe-turn/` directory: `03-connected-dial.png`, the `turn-dial-*deg.png` frames every 2 s, and `07-after-turn.png`. The needle must move clockwise frame to frame, matching the way the rig was turned. The script cannot know which way the rig was physically turned; only this check can.

## Step 5: a read-only client receives only tlm (t4)

```bash
./t4-readonly.py --url $R35_URL                 # default: --events estop
```

Three clients for 60 s: the main UI (never sends; Chrome records its frames and the UI shows its ignored count), a pinger (one ping, then listens: the positive control), and a generator. By default the generator sends `estop` every 3 s while the box is SAFE. The firmware posts `event mode SAFE cause ESTOP` for every e-stop, even in SAFE (`modes.cpp` step 4, host test HT-8), and latches ESTOP until the next `arm`. Nothing turns: there is no arm.

Expected: the UI receives only `tlm` (about 3400 in 68 s of watching), ignored counter 0, sends nothing; the generator gets an ack `ok` and an ESTOP mode event for each e-stop; the pinger receives every one of those events. The box ends SAFE with ESTOP latched; step 7's first `arm` clears it.

If the generator receives no events, the positive control fails and the run is a FAIL that proves nothing. Rerun with `--events hall`: `set_params` turns `bench_mode` on, toggles `hall_installed` (a `HALL_BYPASS` notice each time), then restores both. The firmware also resets both at every boot.

## Step 6: T-11 and R3.4's timing budget with Wi-Fi (t7) (10 minutes)

```bash
./t7-timing.py --url $R35_URL --serial-log $R35_OUT/serial.log
```

Leave the rig still and the laptop awake and in range. The script checks the serial log is growing, streams for 600 s with `tools/capture-box.py --t11` (one client, pings at 5 Hz), then parses only the status lines written during the stream.

Expected output, two verdicts:

- `RESULT t7-timing`: a status line for at least 95 % of the 600 s; the `s` counter never restarts; `baro ok` in every status line and `imu_invalid` 0 ("every sensor read"); no `wifi: disconnected` line, and the stream got tlm at 50 Hz for the whole window ("Wi-Fi connected"); the worst `loop_us_max` under 1000 us, with its second, the median, the p99, and how many seconds came within 50 us of the budget.
- `RESULT t5-t11`: 50 Hz (49 to 51), loss under 1 %, round-trip p95 under 100 ms, and the capture covering the whole 600 s of box time. Loss is counted from the capture file: frames received against the frames the span implies. capture-box's own loss figure (gaps of more than 20 ms) and its exit status are printed as information only, because its method calls late frames lost even when they all arrive; the mock under CPU load fails capture-box's check with 0 frames missing. On the box, `t_ms` comes from the control tick's timer, so the two figures should agree; if they do not, the count is the one that means frames went missing. The box seq numbers missing (tlm, pong, and event share one counter) are a third view of the same loss.

Context for the timing number: R3.4's flight build measured 976 us worst over 11.6 min without Wi-Fi, so the margin is about 25 us. A FAIL here is a real finding, not a kit problem. To see where the time goes, flash the diagnostic build (stage timing; never judged as the item):

```bash
rm -f $R35_REPO/firmware/build-diag/sdkconfig          # so the credentials reach this build too
R35_PORT=/dev/ttyACM0 ./serial-capture.sh -B build-diag -D SDKCONFIG=build-diag/sdkconfig -D DIRECSUN_EXTRA_DEFAULTS=sdkconfig.diag build flash
./t7-timing.py --url $R35_URL --serial-log $R35_OUT/serial.log --seconds 120
```

t7 fails that run on purpose (`flight build` check: it sees the `stages:` lines); read the `stages:` lines in its `window.txt` for the per-stage maxima. Reflash the flight build afterwards (`./serial-capture.sh build flash`). `t7-timing.py --serial-log <file> --log-only` re-analyses any saved log.

## Step 7: controlling client and heartbeat (t6) (ARMS THE BOX)

**Safety, binding (CONTEXT.md):** this step sends `arm` twice. Arming with no ESC connected is safe only if the ESC is physically disconnected: signal off D8 and power off. If the ESC is connected, the step needs Julian's explicit go for this run, every time.

- On the R3.5 build the throttle controller is `ZeroThrottleController` (`firmware/main/control_task.cpp`): SPINUP commands DShot 0. That is read from the source, not measured, and any build with R3.7 or later merged commands real throttle in SPINUP.
- The script's throttle guard sends `estop` at once and fails the run if any tlm shows throttle above `--throttle-guard` (default 0).
- On any exit, if the box is not SAFE, it sends `estop`.

```bash
./t6-control.py --url $R35_URL                                    # prints exactly what it will do, sends nothing
./t6-control.py --url $R35_URL --confirm-arm esc-unplugged        # ESC physically disconnected
./t6-control.py --url $R35_URL --confirm-arm julian-go            # ESC connected, Julian said go for this run
```

Sequence (two Python clients stand in for the two tabs; the box cannot tell them from browsers):

1. B connects and pings at 5 Hz throughout, so it gets events, and so the run also shows that a non-controlling client's pings cannot hold the box.
2. A connects, pings at 5 Hz, and arms: `ack ok`, `event mode SPINUP cause arm`.
3. B sends `mode READY`: refused `not controlling client`; the next tlm is still armed.
4. B sends `estop`: `ack ok`, `event mode SAFE cause ESTOP`.
5. A arms again (retrying up to 7 s while `ramp-down in progress`), then holds 2 s while pinging. The box must stay armed; this is the positive control for step 6. On the R3.5 build it stays in SPINUP, because the wheel never reaches bias speed at throttle 0.
6. A's socket closes. Expected: `event mode SAFE cause HEARTBEAT_LOST` about 0.8 to 1.0 s after the close (1 s after A's last ping), at most 1.5 s; `HEARTBEAT_LOST` in the tlm flags. On the R3.5 build the throttle is 0, so per FD V-36 there is no ramp to watch: the box drops straight to SAFE with DShot 0. The ramp's shape needs a build with throttle and the ESC.
7. A reconnects as A2 and pings for 5 s: SAFE throughout, no mode event, throttle and wheel reference never rise, and A2's `mode READY` is refused (`not armed`).

`BLOCKED` means `arm` was refused for a precondition: `battery low` or `latched fault: BATTERY_LOW` (feed the divider), `box not still` (hold the rig still), `latched fault: CONTROL_STALL` (not releasable: reboot the box). Nothing was proven; fix it and rerun.

## Afterwards

- Quit the monitor (Ctrl+]). `serial.log` and every run directory stay under `$R35_OUT`.
- Record each item in `changes/3-teleop/R3.5-comms.md` (and R3.4's timing item) as `[x]` or `[FAIL]`, with the numbers from the RESULT lines and the run directory names. Step 4 is `[x]` only after the screenshots were checked by eye.

## What the kit cannot judge

- Which way the rig was physically turned in step 4: only the human check of the screenshots ties the dial to the turn.
- That the ESC is physically disconnected in step 7: only Julian can confirm it.
- That `direcsun.local` resolves from devices other than this laptop (phones, other OSes).
- The ramp-down's shape (step 7) on the R3.5 build: with throttle 0 there is none.
- Round-trip time is capture-box's own measurement: its capture file has no arrival times, so the kit cannot recompute it independently.

## How the kit was checked

`test/mock-suite.py` runs every script against `tools/mock-box.py`, deliberately broken copies of it, `test/junk-box.py` (a box that misbehaves on purpose), and `test/fake-serial.py` (a serial log in the firmware's format). For each check it shows the check can fail: a falsifier case per check that must end in the specific `[FAIL]` line. The latest table is `results/mock-*/SUMMARY.txt`. None of that is evidence about the firmware: the mock is a protocol stub, and the serial lines are fixtures.

Known differences between the mock and the R3.5 firmware, which change what a mock PASS means:

- An `estop` in SAFE: the firmware posts `event mode SAFE cause ESTOP` (HT-8); the mock posts nothing. So `t4 --events estop` fails on the stock mock (no events), and the suite proves it with a firmware-like copy instead.
- Refusal reasons and the `arm` guards now match the firmware (`not armed`, `not in SAFE`, `not in READY`, `wheel not at bias`, `not implemented`, `ramp-down in progress`, `bench_mode required`, `box not still`, `battery low`); the mock still says `invalid: <schema error>` for malformed frames where the firmware says `unsupported v`, `unknown type`, and `unknown cmd`. The kit only relies on `not controlling client`, which both share.
- The mock's SPINUP ramps the wheel at 400 rpm/s and reaches READY after about 6.2 s, as the firmware does with a Hall sensor fitted. With the Hall bypassed it takes the spin-up throttle ramp plus the 0.5 s dwell. The R3.5 build stays in SPINUP at throttle 0, so a PASS on the mock says nothing about the wheel loop on the box. The t3 cases give the turn window 14 s and `mock-turn.py` waits 12 s for READY.
