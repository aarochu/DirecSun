# DirecSun ground link

`direcsun-link.js` is the client side of wire schema v1 (`protocol/schema-v1.json`) as a dependency-free ES module for browsers and Node 20+. The demo UI and `ground/console/` import it, so neither handles raw WebSocket frames.

```js
import { DirecSunLink } from "./direcsun-link.js";

const link = new DirecSunLink("ws://direcsun.local:81/");
link.on("tlm", (t) => plot(t));              // 50 Hz telemetry, wire values unchanged
link.on("event", (e) => showMode(e));        // mode changes with their cause
link.on("state", ({ state }) => badge(state));
await link.connect();
await link.command("arm");                   // resolves on the ack with this seq
await link.command("mode", { mode: "MANUAL", input: "rate" });
link.manual({ rate_dps: 20 });               // spaced at least 50 ms apart, latest wins
link.estop();                                // sent at once; throws if the link is not open
```

What the module guarantees:

| Rule | Why |
| --- | --- |
| Pings at 5 Hz while open; `link.rtt` holds round-trip statistics | The box ramps down 1 s after the controlling client goes quiet (FD section 14.4) |
| `command()` resolves on the ack with its `seq` and rejects with `DirecSunCommandError` (`code` `REFUSED`, `TIMEOUT` after 1 s, or `CLOSED`) | The UI shows what the box decided, never what the button asked for |
| `estop()` is never queued or rate-limited, and throws when the link is not open | A stop that did not leave must be visible |
| No automatic reconnect: after a close, only `connect()` opens a new socket | No path resumes wheel motion without the operator (FD section 21 rule 8) |
| Frames with `v` other than 1, non-JSON, or unknown `type` are dropped and reported on `raw` | A mismatched box or a stray sender cannot drive the UI |

Values are passed through as the wire sends them: angles and rates counter-clockwise positive. A UI that draws clockwise-positive headings converts for display (FD section 17, finding V-17).

## Tests

```bash
tools/check-link.sh   # node --test plus schema validation of every message the tests send
```

The tests use a fake WebSocket and a fake clock, so ack timeouts, the 5 Hz ping, and the manual rate limit are checked deterministically.
