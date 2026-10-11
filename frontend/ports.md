# Firmware to frontend integration ports

This file is the frontend-side map of every interface between the main operator UI and the DirecSun box. It records the contract that frontend code may rely on, the parts the current frontend uses, and the implementation status on `main`.

The binding wire contract is [`../protocol/schema-v1.json`](../protocol/schema-v1.json). Firmware Design (FD) section 15 and Technical Design (TD) section 7 are the prose descriptions. If those sources disagree, follow the precedence in [`../changes/CONTEXT.md`](../changes/CONTEXT.md) and treat the disagreement as a defect. Do not change `protocol/`, `firmware/`, or `docs/` from frontend work.

## Network and transport ports

The browser and box use one full-duplex WebSocket carrying UTF-8 JSON text frames. There is no firmware HTTP API, UDP interface, or serial connection used by the frontend.

### Real hardware endpoints

Use one of these only when connecting the frontend to a physical DirecSun box:

| Box network mode | Real endpoint | When to use it |
| --- | --- | --- |
| Station mode with mDNS | `ws://direcsun.local/ws` | The laptop and physical box are on the same 2.4 GHz network and mDNS resolves. |
| Station mode by address | `ws://<address-printed-on-serial>/ws` | Use the physical box's assigned IPv4 address when mDNS does not resolve. Port 80 is implicit. |
| Box-hosted SoftAP | `ws://192.168.4.1/ws` | The laptop has joined the physical box's Wi-Fi. This is the frontend placeholder and contracted fallback. |

**Real-box invariant:** firmware serves port **80** at path **`/ws`**. Never use port 8081 for physical hardware.

SoftAP is selected at firmware build time. The box does not switch network modes automatically during a session.

### Mock endpoint, development only

Use this only with `tools/mock-box.py`:

```text
ws://127.0.0.1:8081/ws
```

**Mock invariant:** `127.0.0.1` is the developer laptop, not the ESP32. Port **8081** belongs to the mock process and is not part of the real firmware contract. The mock uses 8081 because binding port 80 normally requires elevated privileges.

| Target selected in the UI | Process receiving the connection | Hardware evidence? |
| --- | --- | --- |
| `direcsun.local`, the printed box address, or `192.168.4.1`, all on port 80 | Physical ESP32 firmware | Yes, when the real-box test procedure is followed |
| `127.0.0.1:8081` | Local Python mock | No. It validates frontend and wire behavior only |

Only `/ws` is valid on the real box and mock. Port 81 at `/` is obsolete. Two comments in `firmware/sdkconfig.defaults` and `firmware/components/direcsun_comms/CMakeLists.txt` still say port 81, but the protocol schema, FD section 15, TD section 7, frontend placeholder, and mock all specify port 80 at `/ws`.

The frontend accepts a user-entered `ws://` or `wss://` URL. Current firmware provides plain `ws://`, not TLS. A page loaded over HTTPS cannot open that insecure socket because browsers block mixed content. Serve the operator UI over plain HTTP for a real-box run.

### Frontend-only HTTP ports, not device endpoints

These serve the browser application and are not firmware endpoints:

| Process | Default URL |
| --- | --- |
| Landing-page Vite server | `http://localhost:5173/` |
| Ground-station Vite server | `http://localhost:5174/` |
| Landing-page Vite preview | `http://localhost:4173/` |
| Combined offline demo | `http://localhost:8080/ground-station/` |

The page is served by the laptop. The ESP32 does not serve the frontend bundle.

## WebSocket session rules

- Protocol version is `v: 1`.
- Every schema-valid frame has `v`, `type`, and `seq`.
- `seq` is the sending side's unsigned 32-bit counter. An `ack` is the exception: its `seq` echoes the command being acknowledged.
- The frontend does not reconnect automatically. The operator must reconnect and arm again after a disconnect.
- The box is authoritative for mode. The UI changes displayed mode only from telemetry or events, never merely because a button was pressed.
- The box allows at most three clients. Telemetry and events are broadcast. An acknowledgement goes only to the command sender.
- The firmware's inbound message limit is 512 bytes. Oversized messages are refused with `reason: "too large"`.
- Telemetry may be skipped for a slow client, incrementing `tlm_dropped`. Acknowledgements and events are not skipped.

## Timing and safety contract

| Interface | Contract | Frontend behavior on `main` |
| --- | --- | --- |
| Telemetry | Box broadcasts `tlm` at 50 Hz | Every valid frame updates heading/history. React telemetry readouts are published at most every 50 ms. |
| Heartbeat | Controlling client sends `ping` at 5 Hz; only its traffic keeps an armed box alive | Pinging begins with the first command and runs every 200 ms until disconnect. Passive viewing sends nothing. |
| Heartbeat loss | Armed box ramps down after 1 second without a controlling-client command or ping | UI reports the link loss. Safety does not depend on the browser. |
| Command acknowledgement | One `ack` per `cmd` | Promise matched by echoed `seq`; frontend times out after 1 second. |
| Manual setpoint | Accepted in `MANUAL`; box refuses accepted setpoints less than 50 ms apart | Frontend sends the active manual setpoint every 100 ms. |
| E-stop | Accepted in every state from every client | Sent immediately by the red button or Space. It is not queued behind another command. |

## Client to box messages

Every command frame is `{"v":1,"type":"cmd","seq":N,"cmd":"..."}` plus the fields below.

| Command | Additional fields | Firmware acceptance rule | Frontend status on `main` |
| --- | --- | --- | --- |
| `arm` | None | SAFE and all arm guards pass; sender becomes controlling client | Sent after the physical lock-pin/held-still confirmation. |
| `disarm` | None | Armed modes | Sent by Disarm; frontend stops manual streaming and any turn sequence first. |
| `estop` | None | Any state, any client | Sent immediately by button or Space. |
| `mode` | `mode`; `psi_ref_deg` only for SLEW; `input` only for MANUAL | Mode-machine guards in FD section 13 | Sends READY, HOLD, SLEW, MANUAL, DETUMBLE, and SUN_POINT requests. DETUMBLE and SUN_POINT are marked firmware-pending in the UI. |
| `manual` | Exactly one of `throttle`, `wheel_rpm`, or `rate_dps` | MANUAL, controlling client, field matches active `manual_input` | Sent every 100 ms. UI ranges are rate -90 to +90 deg/s, wheel 800 to 4000 rpm, and the telemetry-provided `throttle_band`. |
| `respin` | Optional `wheel_rpm` | READY; box accepts its calibrated safe interval | UI accepts blank for bias or 1000 to 3800 rpm and marks the feature firmware-pending. |
| `zero_heading` | None | SAFE or READY | Used at the start of each judge turn. |
| `set_params` | Non-empty `params` object | SAFE only; box validates, stores to NVS, and returns `ack.applied` | Contracted but not sent by the frontend on `main`. |
| `wheel_step` | `delta_rpm`, -500 to +500 | READY with `bench_mode` enabled | Bench-only contract; not sent by the main frontend. |

The UI's 90-degree judge turn is a client-side sequence, not a distinct wire command:

1. `zero_heading`
2. `mode: "SLEW", psi_ref_deg: -90` for left or `+90` for right
3. Wait for mode event `READY` with cause `slew_complete`
4. Hold for 1 second
5. `mode: "SLEW", psi_ref_deg: 0`
6. Wait for the matching completion event

Stop Turn sends `mode: "READY"` only if the box is currently in SLEW. It is a UI action, not a `stop` command.

### `set_params` keys

The schema permits these SAFE-only keys: `k_psi`, `k_omega`, `k_i`, `wheel_kp`, `wheel_ki`, `ff_table`, `omega_low_rpm`, `omega_bias_rpm`, `s_w`, `s_gyro`, `mag_axis_map`, `mag_cal`, `sun_cal`, `light_threshold`, `dshot_rate`, `bench_mode`, `manual_throttle_min`, and `manual_throttle_max`.

`omega_cap_rpm` and Wi-Fi settings are deliberately unavailable over the wire. The box also validates relationships that JSON Schema cannot express, such as monotonic calibration tables and minimum values below maximum values.

## Box to client messages

### `tlm`

The schema requires all normal telemetry fields on every 50 Hz frame. Nullable values remain present as `null`; the five bench-only fields may be absent.

| Field group | Wire fields | Frontend use on `main` |
| --- | --- | --- |
| Identity and time | `v`, `type`, `seq`, `t_ms` | Requires `type: "tlm"` and finite `psi_deg`. Accepts `v: 1` or an absent `v` for backward compatibility. Uses `t_ms` for history time. |
| Mode | `mode`, `manual_input` | Displays mode; drives control availability and active manual control. |
| Attitude | `psi_deg`, `psi_ref_deg`, `rate_dps` | Drives model, dial, history, target, rate readout, and arm stillness check. |
| Direction sensors | `mag_deg`, `sun_deg`, `sun_valid` | Shown in sensor readouts; sun validity gates SUN_POINT. |
| Wheel and actuation | `wheel_rpm`, `wheel_ref_rpm`, `throttle`, `tau_cmd_mNm` | Uses wheel actual/reference. `throttle` and `tau_cmd_mNm` are currently not displayed. |
| Power and timing | `vbat`, `loop_us` | Displays battery and loop time; battery also participates in the frontend arm precheck. |
| State flags | `flags` | Displays all strings. Known latched faults are styled as faults; other non-OK flags are warnings. |
| Environment | `t_c`, `p_pa` | Shown when present. |
| Drop counters | `tlm_dropped`, `dropped_events` | Contracted but currently not displayed. |
| Bench-only sensors | `accel_rms_g`, `gyro_raw`, `mag_raw`, `sun_a_mv`, `sun_b_mv` | Parsed and shown where the sensor panel exposes them. Raw vectors remain in sensor axes. |

The frontend parser is intentionally less strict than the schema so one bad optional value does not discard heading telemetry. Schema conformance remains the box's responsibility.

### `ack`

Required fields are `v`, `type: "ack"`, echoed `seq`, and `ok`. A refusal also carries `reason`. A successful `set_params` acknowledgement may carry `applied`. The frontend resolves or rejects the pending command by `seq`; on `main` it does not consume `applied`.

### `pong`

The box returns its own `seq`, the client's `ping_seq`, echoed `t_client_ms`, and box `t_ms`. The frontend matches `ping_seq` to its send timestamp and displays round-trip time.

### `event`

Mode events carry `event: "mode"`, the new `mode`, and `cause`. Notice events carry `event: "notice"`, `name`, and optional `detail`. The current schema defines notice name `PARAMS_RESET`. The frontend consumes both forms and specifically reacts to `slew_complete`, `SLEW_TIMEOUT`, `sun_invalid`, and safety transitions to SAFE.

### Enumerated wire values

- Modes: `SAFE`, `SPINUP`, `READY`, `DETUMBLE`, `HOLD`, `SLEW`, `SUN_POINT`, `MANUAL`.
- Non-fault flags: `MOMENTUM_OK`, `MOMENTUM_NEAR_LIMIT`, `MOMENTUM_SATURATED`, `LOOP_OVERRUN`, `MAG_MISSING`, `IMU_MISSING`, `MANUAL_STALE`.
- Latched interlocks, also valid as flags and event causes: `ESTOP`, `HEARTBEAT_LOST`, `OVERSPEED`, `UNDERSPEED`, `BATTERY_LOW`, `IMU_SILENT`, `TILT`, `CONTROL_STALL`, `HALL_SILENT`.
- Automatic mode-event causes: `boot`, `spinup_complete`, `slew_complete`, `SLEW_TIMEOUT`, `sun_invalid`. Any command name or interlock name may also be a cause.
- Notice names: `PARAMS_RESET`.

Command refusal `reason` is intentionally not a closed enum. The frontend displays the box's text and adds guidance for common reasons such as `not implemented`, `not in READY`, `not armed`, `wheel not at bias`, `wheel loop not available`, `input not available`, `sun not valid`, and `not controlling client`.

## Units and sign boundary

- Angles are degrees, rates are degrees per second, wheel speed is rpm, torque is mN m, voltage is V, and `t_ms` is milliseconds since box boot.
- On the wire, spin-axis heading, references, rate, sun/magnetic angles, and commanded box torque are clockwise-positive when viewed from above. Right is +90 degrees.
- Firmware control math is counter-clockwise-positive. Firmware converts only at protocol encode/decode. The frontend must not convert wire signs again.
- `gyro_raw` and `mag_raw` are unconverted sensor-axis counts.

## Validation snapshot

Validated on 2026-10-11 against:

- Frontend: `ground-station/src/hardware/deviceLink.ts`, `deviceOperator.ts`, `modes.ts`, device controls, and their tests.
- Firmware: `firmware/sdkconfig.defaults` and `firmware/components/direcsun_comms/`.
- Contract: `protocol/schema-v1.json` and `protocol/README.md`.
- Design: FD sections 14.4, 15, and 17; TD sections 7.1 through 7.4.

Known cross-source drift on `main`:

1. The firmware communications component is still an empty Set 1 skeleton, so the real ESP32 endpoint and message handlers are specified but not implemented on `main`.
2. The two firmware comments named above still say port 81. They are stale; the binding endpoint is port 80 at `/ws`.
3. FD section 17 and TD section 7.4 still describe the frontend as read-only. The current frontend sends the commands listed above and implements heartbeat, ack matching, immediate e-stop, and no automatic reconnect.
4. `set_params` and `wheel_step` exist in the schema and design but are not emitted by the current frontend.

When any endpoint, message, field, unit, sign, rate, or safety rule changes, update this file in the same integration PR after the binding schema and design change has been approved by the firmware owner.
