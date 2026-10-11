#!/usr/bin/env bash
# usage: r41-run.sh <scenario> <outdir> [extra recorder args...]
# A fresh #86 mock on 8765, #86's recorder headed against the stack UI on 4176,
# then #86's checker on the capture.
set -u
S=/tmp/claude-1000/-home-jazoe-projects-DirecSun/2cd541dd-44e4-4126-8e9d-aa31a633f95f/scratchpad
PY=/home/jazoe/projects/DirecSun/.venv/bin/python
scenario=$1; out=$2; shift 2
rm -rf "$out"; mkdir -p "$out"
(cd $S/wt/pr86 && exec $PY tools/mock-box.py --port 8765) >"$out/../$(basename "$out")-mock.log" 2>&1 &
mock=$!
for _ in $(seq 1 40); do ss -ltn | grep -q ":8765\b" && break; sleep 0.25; done
(cd $S/wt/pr86 && timeout 300 node tools/ui-turn-recorder.mjs --ui http://127.0.0.1:4176/ --box ws://127.0.0.1:8765/ws \
    --scenario "$scenario" --out "$out" --headed \
    --playwright $S/wt/m70/frontend/ground-station/node_modules/playwright/index.mjs "$@") >"$out/../$(basename "$out")-recorder.txt" 2>&1
echo "recorder exit $?" >>"$out/../$(basename "$out")-recorder.txt"
kill $mock; wait $mock 2>/dev/null
(cd $S/wt/pr86 && $PY tools/check-ui-capture.py "$out") >"$out/../$(basename "$out")-check.txt" 2>&1
echo "checker exit $?" >>"$out/../$(basename "$out")-check.txt"
