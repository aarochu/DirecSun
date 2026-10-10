#!/usr/bin/env bash
# direcsun_core must compile on a desktop (FD section 7.1, principle 3), so it
# may not include ESP-IDF, FreeRTOS, or driver headers.
#
#   tools/check-core-purity.sh             check firmware/components/direcsun_core
#   tools/check-core-purity.sh --selftest  positive control: a clean copy passes and
#                                          a copy with an injected IDF include fails
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
core="$repo_root/firmware/components/direcsun_core"
forbidden='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](esp_[^>"]*|freertos/[^>"]*|driver/[^>"]*|hal/[^>"]*|soc/[^>"]*|rom/[^>"]*|sdkconfig\.h)[>"]'

# Prints offending lines and returns 1 on a violation; returns 2 if there is
# nothing to scan, so a wrong path cannot pass silently.
check_dir() {
    local dir="$1"
    local files
    files="$(find "$dir" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.hh' \) | wc -l)"
    if [[ "$files" -eq 0 ]]; then
        echo "check-core-purity: no source files under $dir" >&2
        return 2
    fi
    local hits
    hits="$(grep -rnE --include='*.c' --include='*.h' --include='*.cpp' --include='*.hpp' \
        --include='*.cc' --include='*.hh' "$forbidden" "$dir" || true)"
    if [[ -n "$hits" ]]; then
        echo "check-core-purity: forbidden includes in $dir:" >&2
        echo "$hits" >&2
        return 1
    fi
    echo "check-core-purity: $files file(s) under $dir, no hardware includes"
    return 0
}

if [[ "${1:-}" == "--selftest" ]]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    cp -r "$core" "$tmp/core"
    check_dir "$tmp/core" >/dev/null || { echo "check-core-purity selftest: FAILED, the clean copy did not pass" >&2; exit 1; }
    printf '#pragma once\n#include "esp_log.h"\n' >"$tmp/core/include/injected.hpp"
    set +e
    check_dir "$tmp/core" >/dev/null 2>&1
    status=$?
    set -e
    if [[ "$status" -eq 1 ]]; then
        echo "check-core-purity selftest: PASSED, the injected esp_log.h include was detected"
    else
        echo "check-core-purity selftest: FAILED, the injected include was not detected (status $status)" >&2
        exit 1
    fi
    exit 0
fi

check_dir "$core"
