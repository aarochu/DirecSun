# UI turn recorder (issue #40)

This tool drives the **main ground-station UI** in Chrome, not the box protocol directly. It records every WebSocket frame the page sends and receives, status/result transitions, visibility changes, and screenshots. The checker judges the capture independently. Neither a mock pass nor a capture is hardware evidence.

## Mock run

Use the R4.3 mock from this checkout and a built ground station from `main` plus PR #33. The UI build needs its own `npm ci` and `npm run build` from `frontend/ground-station/`.

```sh
# Terminal 1, in the UI checkout
cd frontend/ground-station
npm run preview -- --host 127.0.0.1 --port 4174

# Terminal 2, in this checkout, using a Python with websockets and jsonschema
python tools/mock-box.py --port 8084

# Terminal 3, in this checkout
node tools/ui-turn-recorder.mjs --scenario turn --turns 4 \
  --url ws://127.0.0.1:8084/ws --base http://127.0.0.1:4174 \
  --ui-dir /absolute/path/to/frontend/ground-station --out /tmp/direcsun-turn
python tools/check-ui-capture.py --scenario turn /tmp/direcsun-turn/frames.jsonl
```

Replace `turn` with `stop`, `estop`, or `watch` and use a new output directory for each. `hidden` launches **plain headed Google Chrome** through CDP, opens a second tab, and requires a recorded `visibilitychange` to `hidden`. Set `CHROME_BIN` if Chrome is not at `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome`. Headless/Playwright-launched tabs do not prove hidden-tab timer behavior.

The four headless scenarios use the schematic fallback. Their purpose is UI control and wire timing, not CAD rendering. Each run ends with a UI e-stop if still armed, then disconnects. A screenshot failure is logged but does not suppress the wire capture; inspect the output directory and treat a missing screenshot as an incomplete visual check.

## Real-box gate

Do not run this against hardware alone. A non-loopback URL is refused unless `--confirm-arm julian-go` is present. **That flag is not itself Julian's go.** Before each action that can turn the motor, the tool states the complete action and prompts for `julian-go` for that one action. Julian must give explicit go at the bench each time. Confirm the physical rig, lock pin, supply, guard, and reachable e-stop first. The tool's end-of-run e-stop requests ramp-down; the bench supply output switch is the last resort. Never infer that a browser process closing has made the box safe.

The hardware scenarios and their required results are in #44 Stage 4. Do not mark them passed from mock captures.

## Checker

`python tools/check-ui-capture.py --selftest` proves the checker catches a planted stale telemetry frame after `slew_complete` and a missing ACK. A capture pass requires one ACK per command within 1000 ms, event/first-telemetry mode agreement, completion of each uninterrupted SLEW within 6 s, ping gaps of 200 ± 60 ms with one pong per ping, and no non-telemetry frame delivered to a watch-only page before it sends. The checker prints ACK p50/max and the most common `tlm.t_ms mod 20` phase.

Use `--scenario stop` for an intentionally interrupted SLEW. A failure is a result to investigate, not a reason to relax timing limits. This tool does not update the spec's `[x]` markers; Julian does that after reviewing the real run.
