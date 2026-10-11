# Source this before running the kit:   . tools/bench/r35/env.sh
# Every value can be overridden by exporting it first.
KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The checkout this kit lives in (feat/set3/R3.5-comms or later): tools/capture-box.py,
# tools/check-schema.py, protocol/schema-v1.json, ground/console, and the firmware.
export R35_REPO="${R35_REPO:-$(cd "$KIT_DIR/../../.." && pwd)}"
# The main UI's operator client: frontend/ground-station from a checkout of origin/main,
# with node_modules and dist/ (prep.sh builds it). No default; set it before prep.sh.
export R35_UI="${R35_UI:-}"
# Python with websockets and jsonschema (the repo's .venv).
export DIRECSUN_PYTHON="${DIRECSUN_PYTHON:-$R35_REPO/.venv/bin/python}"
# Where each run writes its own directory (gitignored).
export R35_OUT="${R35_OUT:-$KIT_DIR/results}"
# The box.
export R35_URL="${R35_URL:-ws://direcsun.local/ws}"
