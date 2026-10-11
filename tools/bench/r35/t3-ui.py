#!/usr/bin/env python3
"""R3.5 Tests item 4: the main UI reads the real box.

    t3-ui.py --url ws://direcsun.local/ws [--turn-s 15] [--min-turn 30]

The item: "The frontend/ ground station's Device link, pointed at
ws://direcsun.local/ws, switches to device mode with its ignored-frame counter at 0.
Turning the rig clockwise by hand turns the UI's dial clockwise and makes its
heading readout increase. If the sign conversion were missing, the dial would turn
the other way. Screenshots are checked by eye."

The main UI (frontend/ground-station from origin/main, built by prep.sh) runs in the
installed Chrome through its own Playwright. After it connects, the script prints
"TURN NOW": turn the rig clockwise by hand, about 90 degrees, over a few seconds,
then hold it still until sampling ends. Sampling reads, ten times a second:
- the HEADING readout text (wrapped to +-180, unwrapped here sample to sample);
- the dial needle's SVG end point, an independent reading of the same value;
- the Device link status line (packets, and "N IGNORED" once N > 0);
and Chrome records every WebSocket frame the page receives and sends.

PASS needs: device mode, ignored 0 in every sample, only tlm on the wire, nothing
sent by the page, and both readout and needle turning clockwise by at least
--min-turn degrees and agreeing within 5 degrees. The script cannot know which way
the rig was physically turned: a human confirms that from the screenshots
(probe-turn/*.png). A counter-clockwise result after a clockwise turn means the
wire sign is wrong.

--turn-cmd runs a command at "TURN NOW" instead of waiting for a hand (the mock
harness uses it to turn the mock box).
"""
import argparse
import shlex
import subprocess
import sys

import r35common as rc


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--url", default="ws://direcsun.local/ws")
    parser.add_argument("--turn-s", type=float, default=15.0, help="sampling window after TURN NOW")
    parser.add_argument("--min-turn", type=float, default=30.0, help="degrees the readout must rise")
    parser.add_argument("--turn-cmd", help="test hook: command to run at TURN NOW (mock harness only)")
    parser.add_argument("--out")
    args = parser.parse_args()
    run = rc.Run("t3-ui", args.out)

    spawned = []

    def on_line(line):
        if "TURN NOW" in line:
            print("\a", end="", flush=True)
            run.say(">>> TURN THE RIG CLOCKWISE NOW, about 90 degrees, then hold it still <<<")
            if args.turn_cmd:
                log = open(run.dir / "turn-cmd.txt", "w")
                spawned.append((subprocess.Popen(shlex.split(args.turn_cmd), stdout=log, stderr=subprocess.STDOUT), log))

    try:
        with rc.ui_server(run) as base:
            result = rc.probe(run, "turn", args.url, base, ["--turn-s", str(args.turn_s)], on_line=on_line,
                              timeout=args.turn_s + 120)
    except RuntimeError as exc:
        run.check("main UI served", False, str(exc))
        return run.finish()
    finally:
        for proc, log in spawned:
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
            log.close()
            run.check("turn command", None, f"exit {proc.returncode}, output in turn-cmd.txt")

    if not result.get("ok"):
        run.check("Device link connects", False, result.get("error"))
        return run.finish()
    frames = result.get("wsFrames", {})
    received = frames.get("received", {})
    turn = result.get("turn", {})
    run.check("device mode", result.get("headerAlwaysLive") is True and all(m.endswith("/ DEVICE") for m in result.get("modesSeen", [])),
              f"header 'DEVICE CONNECTED' in every sample; mode readout {result.get('modesSeen')}")
    run.check("ignored-frame counter 0", result.get("ignoredMax") == 0,
              f"max {result.get('ignoredMax')} over {result.get('samples')} samples; last status "
              f"{result.get('last', {}).get('status')!r}")
    run.check("only tlm on the wire", set(received) == {"tlm"},
              f"page received {received}" + (f"; non-tlm samples {frames.get('receivedNonTlmSamples')[:3]}"
                                             if set(received) - {"tlm"} else ""))
    run.check("the page sent nothing (read-only viewer)", frames.get("sent") == 0,
              f"{frames.get('sent')} frames sent" + (f": {frames.get('sentSamples')[:3]}" if frames.get("sent") else ""))
    heading, needle = turn.get("headingChangeDeg"), turn.get("needleChangeDeg")
    if heading is None or needle is None:
        run.check("turn sampled", False, f"no turn data: {turn}")
        return run.finish()
    run.check("heading readout increased (clockwise)", heading >= args.min_turn,
              f"{turn.get('headingStart')} -> {turn.get('headingEnd')}, net {heading:+.1f} deg "
              f"(range {turn.get('headingMinDeg'):+.1f} to {turn.get('headingMaxDeg'):+.1f}); needs >= +{args.min_turn:.0f}"
              + ("; it went DOWN: counter-clockwise on the wire" if heading <= -args.min_turn else ""))
    run.check("dial needle turned clockwise", needle >= args.min_turn, f"net {needle:+.1f} deg on screen")
    run.check("needle and readout agree", abs(needle - heading) <= 5.0, f"difference {needle - heading:+.1f} deg")
    run.check("screenshots for the human check", None,
              f"{len(result.get('screenshots', []))} in {run.dir / 'probe-turn'}: confirm the needle moves clockwise "
              "frame to frame, matching the physical turn")
    return run.finish(summary=f"device mode, ignored 0, heading {heading:+.1f} deg, needle {needle:+.1f} deg clockwise; "
                              "dial direction still needs the human check of the screenshots"
                      if not any(c["ok"] is False for c in run.checks) else "")


if __name__ == "__main__":
    sys.exit(main())
