#!/usr/bin/env bash
# Create the repo's Python 3.12 venv (.venv) for CAD builds and tool scripts.
# CadQuery's OCC kernel has no wheels for Python 3.13+, so 3.12 is required.
# Usage: tools/bootstrap-venv.sh            (creates .venv if missing, then installs)
# Base interpreter: $DIRECSUN_BASE_PYTHON, else python3.12. (DIRECSUN_PYTHON is
# different: the check scripts use it for an interpreter that already has the
# tool packages.)
#        tools/bootstrap-venv.sh --recreate (deletes and rebuilds .venv)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="$ROOT/.venv"
REQ="$ROOT/tools/requirements.txt"
PY="${DIRECSUN_BASE_PYTHON:-python3.12}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "bootstrap-venv: $PY not found. Install Python 3.12 (CadQuery has no 3.13 wheels) or set DIRECSUN_BASE_PYTHON." >&2
  exit 1
fi
if ! "$PY" -c 'import sys; sys.exit(0 if sys.version_info[:2] == (3, 12) else 1)'; then
  echo "bootstrap-venv: $PY is $("$PY" --version 2>&1), need Python 3.12." >&2
  exit 1
fi

if [ "${1:-}" = "--recreate" ] && [ -d "$VENV" ]; then
  rm -rf "$VENV"
fi
if [ ! -x "$VENV/bin/python" ]; then
  "$PY" -m venv "$VENV"
fi

"$VENV/bin/python" -m pip install --quiet --upgrade pip
"$VENV/bin/python" -m pip install --quiet -r "$REQ"
"$VENV/bin/python" -c 'import cadquery, trimesh, jsonschema, websockets; print("bootstrap-venv: ok, cadquery", cadquery.__version__)'
