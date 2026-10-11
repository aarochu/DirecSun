#!/usr/bin/env bash
# Run the ground link tests and validate every message they send against
# protocol/schema-v1.json.
#
# Passes only if node --test reports at least 8 tests and 0 failures, the capture
# file holds at least one message, and every captured message validates. Full
# outputs are kept in build/check-link/.
#
# Python: $DIRECSUN_PYTHON, else the repo .venv (tools/bootstrap-venv.sh).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$repo_root/build/check-link"
mkdir -p "$out"
capture="$out/capture.jsonl"
: >"$capture"

python="${DIRECSUN_PYTHON:-$repo_root/.venv/bin/python}"
if [[ ! -x "$python" ]]; then
    echo "check-link: no Python with jsonschema; run tools/bootstrap-venv.sh or set DIRECSUN_PYTHON" >&2
    exit 1
fi

cd "$repo_root"
set +e
DIRECSUN_LINK_CAPTURE="$capture" node --test 'ground/link/**/*.test.mjs' >"$out/node-test.txt" 2>&1
status=$?
set -e
cat "$out/node-test.txt"

tests="$(sed -nE 's/^(ℹ|#) tests ([0-9]+)$/\2/p' "$out/node-test.txt")"
failed="$(sed -nE 's/^(ℹ|#) fail ([0-9]+)$/\2/p' "$out/node-test.txt")"
if [[ -z "$tests" || -z "$failed" ]]; then
    echo "check-link: no node --test summary in $out/node-test.txt" >&2
    exit 1
fi
if [[ "$status" -ne 0 || "$failed" -ne 0 ]]; then
    echo "check-link: FAILED, $failed failing test(s)" >&2
    exit 1
fi
if [[ "$tests" -lt 8 ]]; then
    echo "check-link: FAILED, only $tests test(s) ran; expected at least 8" >&2
    exit 1
fi

"$python" "$repo_root/tools/check-schema.py" "$capture"
echo "check-link: PASSED, $tests tests, $(wc -l <"$capture") captured message(s) valid against the schema"
