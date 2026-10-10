#!/usr/bin/env bash
# Build and run the direcsun_core host tests (desktop compiler, no ESP-IDF).
#
#   tools/host-test.sh             run the suite
#   tools/host-test.sh --selftest  positive control: build with a deliberately
#                                  failing test and succeed only if the run fails
#
# A run passes only if doctest's summary is present, reports at least one test
# case, and reports zero failures, so a build that silently runs nothing fails.
# Full output is kept in build/host-tests-<mode>/output.txt.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mode="normal"
selftest="OFF"
if [[ "${1:-}" == "--selftest" ]]; then
    mode="selftest"
    selftest="ON"
elif [[ $# -gt 0 ]]; then
    echo "usage: tools/host-test.sh [--selftest]" >&2
    exit 64
fi

build="$repo_root/build/host-tests-$mode"
output="$build/output.txt"
mkdir -p "$build"

cmake -S "$repo_root/firmware/test/host" -B "$build" \
    -DCMAKE_BUILD_TYPE=Debug -DDIRECSUN_HT_SELFTEST="$selftest" >"$build/configure.txt" 2>&1 ||
    { cat "$build/configure.txt" >&2; echo "host-test: configure failed" >&2; exit 1; }
cmake --build "$build" -j >"$build/build.txt" 2>&1 ||
    { cat "$build/build.txt" >&2; echo "host-test: build failed" >&2; exit 1; }

set +e
"$build/host_tests" >"$output" 2>&1
status=$?
set -e
cat "$output"

summary="$(grep -E '^\[doctest\] test cases:' "$output" || true)"
if [[ -z "$summary" ]]; then
    echo "host-test: no doctest summary in $output; the test binary did not run to completion" >&2
    exit 1
fi
cases="$(sed -E 's/^\[doctest\] test cases: *([0-9]+).*/\1/' <<<"$summary")"
failed="$(sed -E 's/.*\| *([0-9]+) failed.*/\1/' <<<"$summary")"

if [[ "$mode" == "normal" ]]; then
    if [[ "$status" -ne 0 || "$failed" -ne 0 ]]; then
        echo "host-test: FAILED ($failed failing test case(s), exit $status)" >&2
        exit 1
    fi
    if [[ "$cases" -lt 1 ]]; then
        echo "host-test: FAILED, zero test cases ran" >&2
        exit 1
    fi
    echo "host-test: PASSED, $cases test case(s)"
else
    if [[ "$status" -ne 0 && "$failed" -ge 1 ]] && grep -q 'selftest_must_fail' "$output"; then
        echo "host-test selftest: PASSED, the deliberate failure was detected ($failed failing, exit $status)"
    else
        echo "host-test selftest: FAILED, a deliberately failing test did not fail the run" >&2
        exit 1
    fi
fi
