# Wire protocol

`schema-v1.json` is the machine-checkable contract between the DirecSun box and any ground station: every JSON text frame on the WebSocket at `ws://direcsun.local:81/`. The prose version is `docs/firmware-design.md` section 15; when the two disagree, whichever changed last has the bug.

| Message | Direction | When |
| --- | --- | --- |
| `tlm` | box to all clients | 50 Hz |
| `cmd` | client to box | `arm`, `disarm`, `estop`, `mode`, `manual`, `respin`, `zero_heading`, `set_params`, `wheel_step` |
| `ack` | box to the sender | one per `cmd`, `seq` echoes the command |
| `ping` / `pong` | client to box, box to client | 5 Hz heartbeat and round-trip time |
| `event` | box to all clients | every mode change (`event: "mode"`, with the `cause`), and notices such as `PARAMS_RESET` |

Rules the schema enforces, so drift fails loudly:

- `additionalProperties: false` everywhere. Bench-only `tlm` fields (`accel_rms_g`, `gyro_raw`, `mag_raw`, `sun_a_mv`, `sun_b_mv`) are optional; every other documented `tlm` field is always present, with `null` where the box has no value.
- `tlm.throttle` is 0 or 48..2047. DShot values 1..47 are ESC commands that flight code never sends.
- `tlm.manual_input` is set exactly when `mode` is `MANUAL`; `psi_ref_deg` is `null` in SAFE, SPINUP, READY, and DETUMBLE.
- `cmd mode` needs `psi_ref_deg` for SLEW and `input` for MANUAL, and rejects them otherwise. SAFE and SPINUP are not mode targets (use `disarm`, `estop`, `arm`, `respin`).
- `cmd manual` carries exactly one of `throttle`, `wheel_rpm`, `rate_dps`.
- `set_params` cannot carry `omega_cap_rpm` or Wi-Fi settings.

Ranges that depend on other values (a monotonic feed-forward table, the bias between the limits, the clamp bands) are checked by the box, not by the schema.

## Fixtures

`examples/valid/` holds one or more valid messages per kind. `examples/invalid/` holds messages that must be rejected, each named `<valid base>--<rule>.json` and differing from `examples/valid/<valid base>.json` in exactly one top-level key, so each one isolates a single rule.

```bash
tools/check-schema.py               # every valid fixture passes, every invalid one is rejected
tools/check-schema.py capture.jsonl # validate every line of a JSON-lines capture
```

## Changing the protocol

Additions before the end of Set 1 stay `v: 1`. After that, any change bumps `v`, and the schema, FD section 15, `ground/link/`, `tools/mock-box.py`, and the firmware encoder change together.
