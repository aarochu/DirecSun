# Wire protocol

`schema-v1.json` is the machine-checkable contract between the DirecSun box and any ground station: every JSON text frame on the WebSocket on port 80 at path `/ws`. The prose version is `docs/firmware-design.md` section 15; when the two disagree, whichever changed last has the bug.

The main UI is `frontend/` (Aaron's), and the backend matches it (`changes/CONTEXT.md`). For the wire interface the UI leads: the schema encodes what the UI expects, and FD section 15 and the firmware follow.

| Item | Value |
| --- | --- |
| Endpoint, station mode | `ws://direcsun.local/ws` (mDNS), or `ws://<address printed on serial>/ws` |
| Endpoint, SoftAP mode | `ws://192.168.4.1/ws`, the UI's default |
| Sign (wire frame) | Clockwise positive seen from above, +z up the spin axis, for every quantity about the spin axis: `psi_deg`, `psi_ref_deg`, `mag_deg`, `sun_deg`, `rate_dps`, `tau_cmd_mNm`, the SLEW `psi_ref_deg`, and the MANUAL `rate_dps`. "Rotate right" in the UI is +90 degrees |
| Firmware frame | Counter-clockwise positive (FD section 2.3). The firmware converts only in its protocol encode and decode |
| Not converted | `gyro_raw` and `mag_raw` (bench only) are raw sensor counts in the sensor's own axes |

| Message | Direction | When |
| --- | --- | --- |
| `tlm` | box to all clients | 50 Hz |
| `cmd` | client to box | `arm`, `disarm`, `estop`, `mode`, `manual`, `respin`, `zero_heading`, `set_params`, `wheel_step` |
| `ack` | box to the sender | one per `cmd`, `seq` echoes the command |
| `ping` / `pong` | client to box, box to client | 5 Hz heartbeat and round-trip time |
| `event` | box to every client that has sent at least one message | every mode change (`event: "mode"`, with the `cause`), and the notices `PARAMS_RESET` and `HALL_BYPASS`. A page that only watches receives only `tlm`, which carries the mode in every frame |

Rules the schema enforces, so drift fails loudly:

- `additionalProperties: false` everywhere. Bench-only `tlm` fields (`accel_rms_g`, `gyro_raw`, `mag_raw`, `sun_a_mv`, `sun_b_mv`) are optional; every other documented `tlm` field is always present, with `null` where the box has no value.
- `tlm.throttle` is 0 or 48..2047. DShot values 1..47 are ESC commands that flight code never sends.
- `tlm.manual_input` is set exactly when `mode` is `MANUAL`; `psi_ref_deg` is `null` in SAFE, SPINUP, READY, and DETUMBLE.
- `cmd mode` needs `psi_ref_deg` for SLEW and `input` for MANUAL, and rejects them otherwise. SAFE and SPINUP are not mode targets (use `disarm`, `estop`, `arm`, `respin`).
- `cmd manual` carries exactly one of `throttle`, `wheel_rpm`, `rate_dps`.
- `set_params` cannot carry `omega_cap_rpm` or Wi-Fi settings.

Ranges that depend on other values (a monotonic feed-forward table, the bias between the limits, the clamp bands, `hall_expect_throttle` below `spinup_throttle`) are checked by the box, not by the schema.

## Fixtures

`examples/valid/` holds one or more valid messages per kind. `examples/invalid/` holds messages that must be rejected, each named `<valid base>--<rule>.json` and differing from `examples/valid/<valid base>.json` in exactly one top-level key, so each one isolates a single rule.

```bash
tools/check-schema.py               # every valid fixture passes, every invalid one is rejected
tools/check-schema.py capture.jsonl # validate every line of a JSON-lines capture
```

## Changing the protocol

Versioning rule (`changes/CONTEXT.md`, 2026-10-10):

- **Additive, backward-compatible changes stay `v: 1`.** These are new optional fields and new flag, event, notice, or parameter names. Each one is listed under Changes below, so Aaron's UI can pick it up.
- **Breaking changes bump `v`.** These are renamed or removed fields, and changed types, units, or meaning.

Either way, the schema, FD section 15, `ground/link/`, `tools/mock-box.py`, and the firmware encoder change together.

## Changes

| Date | Change | Kind |
| --- | --- | --- |
| 2026-10-10 | The sign of every quantity about the spin axis flips to clockwise positive, and the endpoint moves from port 81 at `/` to port 80 at `/ws` (R1.8). The UI already expected both. | v1 correction. Not a `v: 2` break, because no backend had emitted v1 yet, and the only consumer (the main UI) already assumed clockwise positive and `/ws` |
| 2026-10-10 | `tlm.flags` may contain `HALL_BYPASS`, and `event` `notice` may be named `HALL_BYPASS` with `detail` `on` or `off`: the Hall bench bypass (bench only, FD section 14.1) | Additive, stays v1 |
| 2026-10-10 | `set_params` accepts `spinup_throttle` and `hall_expect_throttle` (integers 48..2047) and `hall_installed` (boolean; `false` only in SAFE with `bench_mode` already on, never persisted) | Additive, stays v1 |
| 2026-10-10 | Events go only to clients that have sent at least one message (R3.5). A read-only viewer, like the main UI's device link today, receives only `tlm`, so its ignored-frame count stays 0 | Delivery rule, no schema change |
| 2026-10-10 | Refusal reasons are fixed strings (FD section 15.3), and an `ack` for a frame with no readable `seq` (too large, malformed JSON) carries `seq` 0. `mag_deg` is null until magnetometer heading exists (Set 5) | Clarification, stays v1 |
| 2026-10-10 | `tlm` may carry `throttle_band`: `[lo, hi]`, the band the box clamps MANUAL throttle setpoints into (`manual_throttle_min`, `manual_throttle_max`, with the top capped at 400 while the Hall is bypassed), or `null` until known (R3.7). The bench console draws its slider from it; the main UI may ignore it | Additive, stays v1 |
| 2026-10-10 | `set_params` is refused with `invalid param: hall_expect_throttle` unless `hall_expect_throttle` stays below `spinup_throttle`, so the Hall-silent interlock arms in every spin-up. This holds for the merged set, so lowering `spinup_throttle` alone to the stored threshold is refused too; send both in one update. A stored set that breaks the rule loads the defaults (`PARAMS_RESET` `invalid`). FD section 16, V-64 | Box rule, no schema change |
| 2026-10-10 | `set_params` is refused with the existing reason `ramp-down in progress` while the 5 s ramp-down after an e-stop, `disarm`, or heartbeat loss runs. The box reports `SAFE` then, with a non-zero `throttle`; wait for `throttle` 0. FD section 16, V-65 | Box rule, no schema change |
| 2026-10-10 | `set_params` accepts `bench_underspeed_rpm` and `bench_cap_rpm` (numbers; 0 clears; bench mode only, never persisted, cleared when bench mode turns off). `tlm.flags` may contain `WHEEL_STEP` while a bench `wheel_step` hold is active, and `wheel_ref_rpm` is nonzero while the closed wheel loop runs (R3.8) | Additive, stays v1 |
| 2026-10-10 | `set_params` accepts `rate_gain_scale` (number 0..1, default 0.2, persisted), which multiplies `k_omega` and `k_i`. In READY the rate loop runs: `tau_cmd_mNm` is its commanded torque, and `tlm.flags` may contain `MOMENTUM_SATURATED` while the wheel reference sits on a limit in the direction the loop needs (R3.9) | Additive, stays v1 |
| 2026-10-10 | `tlm.flags` may contain `HEADING_GYRO`, set on every frame until Set 5: `psi_deg` is integrated from the gyro alone. `psi_deg`, `psi_ref_deg`, and SLEW's `psi_ref_deg` are relative to the last `zero_heading`, which the box accepts in SAFE and READY only and refuses elsewhere with `not in SAFE or READY` (R4.2). A UI should not present the heading as a compass bearing while the flag is set | Additive and clarification, stays v1 |
| 2026-10-10 | `HOLD` and `SLEW` run (R4.3). In both, `tlm.psi_ref_deg` is the heading reference, a number, clockwise positive and relative to the last `zero_heading`. `SLEW` moves it along a 60 deg/s, 120 deg/s² trapezoid on the shortest wrapped path; a half-turn goes counter-clockwise (negative). `SLEW` ends with a `mode` event to `READY` whose `cause` is `slew_complete` (within 3 deg and 5 deg/s for 0.5 s after the profile) or `SLEW_TIMEOUT` (6 s). `mode HOLD` and `mode SLEW` are refused with `not in READY` outside `READY` and with `wheel loop not available` until `ff_table` is set | Behaviour inside v1, no schema change |
| 2026-10-11 | Ordering guarantee: the first `tlm` after an `ack` or `event` reflects that tick or later, so after a `mode` event the next `tlm` already shows the new mode. Order events against `tlm` by socket order, not by `t_ms`. Contract confirmed against the main UI's operator client (`frontend/draft` at c4e3b0c, now `frontend/ground-station` at c72559c): no new command, field, or rule (FD section 17, R4.1) | Behaviour inside v1 |
| 2026-10-11 | `DETUMBLE` runs (R4.4): accepted from READY by the controlling client, `psi_ref_deg` null, same controller as READY. Refusals `not in READY` and `wheel loop not available`. Fixtures `cmd-mode-detumble.json`, `tlm-detumble.json`. No schema change. | Behaviour inside v1 |
