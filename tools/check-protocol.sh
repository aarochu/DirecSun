#!/usr/bin/env bash
# Check the firmware's protocol encoder against protocol/schema-v1.json.
#
# Runs the host tests (tools/host-test.sh), whose HT-9 case writes one of every
# message kind the firmware encodes to build/host-tests-normal/protocol-capture/
# encoded.jsonl, and one deliberately malformed tlm to malformed.jsonl. Passes
# only if every encoded message validates and the malformed one is rejected, so
# a schema check that accepts everything cannot pass.
#
# Python: $DIRECSUN_PYTHON, else the repo .venv (tools/bootstrap-venv.sh).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
capture="$repo_root/build/host-tests-normal/protocol-capture"
out="$repo_root/build/check-protocol"
mkdir -p "$out"

python="${DIRECSUN_PYTHON:-$repo_root/.venv/bin/python}"
if [[ ! -x "$python" ]]; then
    echo "check-protocol: no Python with jsonschema; run tools/bootstrap-venv.sh or set DIRECSUN_PYTHON" >&2
    exit 1
fi

rm -f "$capture/encoded.jsonl" "$capture/malformed.jsonl"
"$repo_root/tools/host-test.sh" >"$out/host-test.txt" 2>&1 ||
    { tail -30 "$out/host-test.txt" >&2; echo "check-protocol: host tests failed" >&2; exit 1; }

for file in encoded.jsonl malformed.jsonl; do
    if [[ ! -s "$capture/$file" ]]; then
        echo "check-protocol: the host tests wrote no $file; the capture test did not run" >&2
        exit 1
    fi
done

encoded_count="$(wc -l <"$capture/encoded.jsonl")"
if ! "$python" "$repo_root/tools/check-schema.py" "$capture/encoded.jsonl" >"$out/encoded.txt" 2>&1; then
    cat "$out/encoded.txt" >&2
    echo "check-protocol: FAILED, a message the firmware encodes does not match the schema" >&2
    exit 1
fi
if "$python" "$repo_root/tools/check-schema.py" "$capture/malformed.jsonl" >"$out/malformed.txt" 2>&1; then
    cat "$out/malformed.txt" >&2
    echo "check-protocol: FAILED, the deliberately malformed tlm passed the schema check" >&2
    exit 1
fi
echo "check-protocol: PASSED, $encoded_count encoded message(s) valid against the schema; the malformed tlm was rejected"
