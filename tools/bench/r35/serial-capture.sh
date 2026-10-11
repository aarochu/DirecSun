#!/usr/bin/env bash
# Keep a live capture of the box's serial log for t1 and t7 (RUNBOOK step 1).
#   . env.sh && ./serial-capture.sh [build flash]      # build, flash, then monitor
#   . env.sh && ./serial-capture.sh                    # monitor only (resets the board)
#   ./serial-capture.sh --dry-run [build flash]        # print the command, open nothing
# Runs `tools/idf.sh -p $R35_PORT <args> monitor` from $R35_REPO under `script -f`,
# so the log file grows line by line while the monitor shows the same output.
# $R35_OUT/serial.log always points at the newest capture. Quit the monitor with Ctrl+].
set -euo pipefail
: "${R35_REPO:?source env.sh first}" "${R35_OUT:?source env.sh first}"
PORT="${R35_PORT:-/dev/ttyACM0}"
dry=0
if [[ "${1:-}" == "--dry-run" ]]; then dry=1; shift; fi
mkdir -p "$R35_OUT"
log="$R35_OUT/serial-$(date +%Y%m%d-%H%M%S).log"
cmd="'$R35_REPO/tools/idf.sh' -p '$PORT' $* monitor"
echo "serial-capture: $cmd"
echo "serial-capture: log $log (and $R35_OUT/serial.log)"
if [[ $dry -eq 1 ]]; then exit 0; fi
ln -sfn "$log" "$R35_OUT/serial.log"
exec script -q -f -c "$cmd" "$log"
