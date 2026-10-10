#!/usr/bin/env bash
# Run idf.py for the DirecSun firmware from anywhere in the repo.
#
#   tools/idf.sh build
#   tools/idf.sh -p /dev/ttyACM0 flash monitor
#
# Environment: the EIM activation script for ESP-IDF v6.0
# (~/.espressif/tools/activate_idf_v6.0.sh; override with DIRECSUN_IDF_ACTIVATE).
# If that script does not exist but IDF_PATH is already set, for example in the
# espressif/idf CI container, the current environment is used as it is.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
activate="${DIRECSUN_IDF_ACTIVATE:-$HOME/.espressif/tools/activate_idf_v6.0.sh}"

if [[ -f "$activate" ]]; then
    # The EIM script exits when it is sourced from another script (it decides
    # whether it was sourced by looking at $0), but with -e it prints the
    # environment it would set. Export that instead of sourcing it.
    env_lines="$(sh "$activate" -e)"
    while IFS='=' read -r key value; do
        case "$key" in
            PATH) export PATH="$value:$PATH" ;;
            SYSTEM_PATH | "") ;;
            *) export "$key=$value" ;;
        esac
    done <<<"$env_lines"
elif [[ -z "${IDF_PATH:-}" ]]; then
    echo "tools/idf.sh: no ESP-IDF environment found." >&2
    echo "Install ESP-IDF v6.0 with EIM, or set DIRECSUN_IDF_ACTIVATE to its activation script." >&2
    exit 1
fi

cd "$repo_root/firmware"
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" && -x "$IDF_PYTHON_ENV_PATH/bin/python" ]]; then
    exec "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"
fi
exec idf.py "$@"
