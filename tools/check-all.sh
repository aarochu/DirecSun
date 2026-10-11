#!/usr/bin/env bash
# Run every repository check in a fixed order, the same way locally and in CI.
#
#   tools/check-all.sh             all checks except the firmware build
#   tools/check-all.sh --firmware  also build the firmware (needs ESP-IDF v6.0)
#   tools/check-all.sh --frontend  also check the main UI: npm ci and build for
#                                  frontend/, then npm ci, typecheck, unit and e2e
#                                  tests for frontend/draft (needs Node 22+ and
#                                  Google Chrome)
#
# Each check with a --selftest mode runs it too: the selftest plants a fault and
# passes only if the check catches it, so a check that silently passes
# everything fails here.
#
# Python for the schema and link checks: $DIRECSUN_PYTHON (an interpreter with
# jsonschema and websockets), else the repo .venv from tools/bootstrap-venv.sh.
# Logs: build/check-all/<step>.txt, one file per step, full output.
set -uo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
logs="$repo_root/build/check-all"
mkdir -p "$logs"

firmware=0
frontend=0
for arg in "$@"; do
    case "$arg" in
        --firmware) firmware=1 ;;
        --frontend) frontend=1 ;;
        *) echo "usage: tools/check-all.sh [--firmware] [--frontend]" >&2; exit 64 ;;
    esac
done

python="${DIRECSUN_PYTHON:-$repo_root/.venv/bin/python}"
export DIRECSUN_PYTHON="$python"

passed=()
failed=()

step() {
    local name="$1"
    shift
    printf '%-28s ' "$name"
    if "$@" >"$logs/$name.txt" 2>&1; then
        echo "PASS"
        passed+=("$name")
    else
        echo "FAIL (log: build/check-all/$name.txt)"
        failed+=("$name")
        tail -20 "$logs/$name.txt" | sed 's/^/    /'
    fi
}

step core-purity           "$repo_root/tools/check-core-purity.sh"
step core-purity-selftest  "$repo_root/tools/check-core-purity.sh" --selftest
step host-tests            "$repo_root/tools/host-test.sh"
step host-tests-selftest   "$repo_root/tools/host-test.sh" --selftest
step schema-fixtures       "$python" "$repo_root/tools/check-schema.py"
step link-tests            "$repo_root/tools/check-link.sh"
step mock-box-selftest     "$python" "$repo_root/tools/mock-box.py" --selftest
step cad-stale             "$python" "$repo_root/tools/check-cad-stale.py"
step cad-stale-selftest    "$python" "$repo_root/tools/check-cad-stale.py" --selftest
if [[ "$firmware" -eq 1 ]]; then
    build_firmware() {
        if [[ ! -f "$repo_root/firmware/sdkconfig" ]]; then
            "$repo_root/tools/idf.sh" set-target esp32s3 || return 1
        fi
        "$repo_root/tools/idf.sh" build
    }
    step firmware-build build_firmware
fi

if [[ "$frontend" -eq 1 ]]; then
    npm_in_frontend() { (cd "$repo_root/frontend" && npm "$@"); }
    npm_in_draft() { (cd "$repo_root/frontend/draft" && npm "$@"); }
    step frontend-install         npm_in_frontend ci
    step frontend-build           npm_in_frontend run build
    step frontend-test            npm_in_frontend test
    step frontend-draft-install   npm_in_draft ci
    step frontend-demo-bundle     npm_in_frontend run demo:check
    step frontend-draft-typecheck npm_in_draft run typecheck
    step frontend-draft-unit      npm_in_draft test
    step frontend-draft-e2e       npm_in_draft run test:e2e
fi

echo
echo "check-all: ${#passed[@]} passed, ${#failed[@]} failed"
if [[ ${#failed[@]} -gt 0 ]]; then
    printf '  failed: %s\n' "${failed[@]}"
    exit 1
fi
