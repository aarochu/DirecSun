#!/usr/bin/env bash
# One-time preparation for the R3.5 kit. Touches no hardware.
#   . env.sh && ./prep.sh
# Checks both checkouts, installs and builds the main UI, and runs the positive
# controls of the tools the kit relies on. Full output goes to $R35_OUT/prep-<time>.txt.
set -u
KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${R35_REPO:?source env.sh first}" "${R35_UI:?set R35_UI to frontend/ground-station in a checkout of origin/main}" "${DIRECSUN_PYTHON:?source env.sh first}"
mkdir -p "$R35_OUT"
LOG="$R35_OUT/prep-$(date +%Y%m%d-%H%M%S).txt"
fail=0
step() { echo "== $*" | tee -a "$LOG"; }
check() {  # check <label> <command...>: full output to the log, one line to the terminal
  local label=$1; shift
  if "$@" >> "$LOG" 2>&1; then echo "  [ok  ] $label" | tee -a "$LOG"; else echo "  [FAIL] $label (see $LOG)" | tee -a "$LOG"; fail=1; fi
}

step "checkouts"
check "R35_REPO is feat/set3/R3.5-comms ($(git -C "$R35_REPO" log -1 --format='%h %s' 2>/dev/null))" \
  test -f "$R35_REPO/firmware/components/direcsun_comms/src/ws_server.cpp"
check "R35_UI is frontend/ground-station ($(git -C "$R35_UI" log -1 --format='%h %s' 2>/dev/null))" \
  test -f "$R35_UI/src/ground/DeviceLinkPanel.tsx"
check "python has websockets and jsonschema" "$DIRECSUN_PYTHON" -c "import websockets, jsonschema"
check "node 20 or later" node -e 'process.exit(Number(process.versions.node.split(".")[0]) >= 20 ? 0 : 1)'
check "google chrome installed" command -v google-chrome

step "main UI: npm ci (if needed) and build"
if [ ! -d "$R35_UI/node_modules/@playwright/test" ]; then
  check "npm ci" bash -c "cd '$R35_UI' && npm ci"
fi
check "npm run build (type-check and vite build)" bash -c "cd '$R35_UI' && npm run build"

step "positive controls of the tools the kit uses"
check "check-schema: fixtures (valid pass, invalid fail)" "$DIRECSUN_PYTHON" "$R35_REPO/tools/check-schema.py"
check "mock-box --selftest" "$DIRECSUN_PYTHON" "$R35_REPO/tools/mock-box.py" --selftest
check "the laptop resolves its own .local name (mDNS works here)" timeout 10 getent ahostsv4 "$(hostname).local"

step "done"
[ $fail -eq 0 ] && echo "prep: all ok (log $LOG)" || echo "prep: FAILED (log $LOG)"
exit $fail
