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

    # Mutants (firmware/test/host/CMakeLists.txt): each has one safety mechanism
    # compiled out. The named test must run (exactly one case) and fail; a
    # mutant whose test still passes means that test cannot see the defect.
    mutants=(
        "no_latch|HT-8 latch: no path leaves SAFE after a fault without a new arm"
        "no_bypass_clamp|bypass property: throttle never exceeds kBypassThrottleMax while bypassed"
        "no_still_gate|HT-3 positive control: a spinning box in SAFE leaves the bias unchanged"
        "absolute_stillness|boot bias seed: an uncalibrated 0.871 g accelerometer at rest still seeds"
        "no_wire_sign|sign conversion: internal CCW encodes as wire CW and decodes back"
        "no_spinup_ramp|rung 1: SPINUP ramps at 200 DShot units per second through the supervisor"
        "no_wheel_antiwindup|R3.8 positive control: a saturating step overshoots under 10 % with anti-windup"
        "rate_sign_flip|HT-5 sim: READY detumbles a 1 rev/s spin within 5 s at the design gains"
        "no_rate_antiwindup|R3.9 anti-windup: a command held into MOMENTUM_SATURATED does not wind up"
        "no_rate_bleed|R3.9 stick-slip bleed: a stopped box does not walk the wheel to a limit"
        "floor_at_low|V-52 floor: a reference pinned on the floor does not trip UNDERSPEED"
        "raw_rate_heading|R4.2 heading: a seeded 0.5 deg/s bias is removed before integration"
    )
    for entry in "${mutants[@]}"; do
        name="${entry%%|*}"
        test_case="${entry#*|}"
        mutant_out="$build/mutant-$name.txt"
        set +e
        "$build/host_tests_mutant_$name" --test-case="$test_case" >"$mutant_out" 2>&1
        mutant_status=$?
        set -e
        mutant_summary="$(grep -E '^\[doctest\] test cases:' "$mutant_out" || true)"
        mutant_cases="$(sed -E 's/^\[doctest\] test cases: *([0-9]+).*/\1/' <<<"$mutant_summary")"
        mutant_failed="$(sed -E 's/.*\| *([0-9]+) failed.*/\1/' <<<"$mutant_summary")"
        if [[ -n "$mutant_summary" && "$mutant_cases" -eq 1 && "$mutant_failed" -eq 1 && "$mutant_status" -ne 0 ]]; then
            echo "host-test selftest: PASSED, mutant $name was caught by \"$test_case\""
        else
            cat "$mutant_out" >&2
            echo "host-test selftest: FAILED, mutant $name survived (cases ${mutant_cases:-?}, failed ${mutant_failed:-?}, exit $mutant_status)" >&2
            exit 1
        fi
    done
fi
