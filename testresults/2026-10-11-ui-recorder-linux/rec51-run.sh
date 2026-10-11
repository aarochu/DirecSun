#!/usr/bin/env bash
# usage: rec51-run.sh <scenario> <ui-port> <outdir> [extra recorder args...]
# Starts a fresh parity mock (#50) on 8765, runs the #51 recorder against the UI already
# served on <ui-port>, stops the mock, then runs the checker on the capture.
set -u
S=/tmp/claude-1000/-home-jazoe-projects-DirecSun/2cd541dd-44e4-4126-8e9d-aa31a633f95f/scratchpad
PY=/home/jazoe/projects/DirecSun/.venv/bin/python
scenario=$1; uiport=$2; out=$3; shift 3
rm -rf "$out"; mkdir -p "$out"
(cd $S/wt/pr50 && exec $PY tools/mock-box.py --port 8765) >"$out/mock.log" 2>&1 &
mock=$!
for _ in $(seq 1 40); do ss -ltn | grep -q ":8765\b" && break; sleep 0.25; done
(cd $S/wt/pr51 && timeout 300 node tools/ui-turn-recorder.mjs --scenario "$scenario" --url ws://127.0.0.1:8765/ws \
    --base "http://127.0.0.1:$uiport" --out "$out" --ui-dir ${UI_DIR:-$S/wt/pr33/frontend/ground-station} "$@") >"$out/recorder.txt" 2>&1
echo "recorder exit $?" >>"$out/recorder.txt"
kill $mock; wait $mock 2>/dev/null
check_args=()
[[ "$scenario" == "hidden" ]] && check_args=(--expect-hidden --min-hidden-s 30)
(cd $S/wt/pr51 && $PY tools/check-ui-capture.py "${check_args[@]}" "$out/frames.jsonl") >"$out/check.txt" 2>&1
echo "checker exit $?" >>"$out/check.txt"
