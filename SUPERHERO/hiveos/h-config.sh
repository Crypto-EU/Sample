#!/usr/bin/env bash
# SUPERHERO HiveOS config generator (sourced by /hive/bin/custom)

set -euo pipefail

MINER_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "${MINER_PATH}/h-manifest.conf"

config_file="${CUSTOM_CONFIG_FILENAME:-config.conf}"
[[ "$config_file" = /* ]] || config_file="${MINER_PATH}/${config_file}"

POOL_HOST=""
POOL_PORT=""
WALLET=""
WORKER=""
PASS="${CUSTOM_PASS:-}"
BATCH=""
EXTRA_ARGS="${CUSTOM_USER_CONFIG:-}"

if [[ -n "${CUSTOM_URL:-}" ]]; then
    pool_url="${CUSTOM_URL#stratum+tcp://}"
    pool_url="${pool_url#stratum+ssl://}"
    pool_url="${pool_url#stratum://}"
    pool_url="${pool_url#tcp://}"
    IFS=':' read -r POOL_HOST POOL_PORT <<< "${pool_url%%/*}"
fi

if [[ -n "${CUSTOM_TEMPLATE:-}" ]]; then
    WALLET="${CUSTOM_TEMPLATE%%.*}"
    if [[ "${CUSTOM_TEMPLATE}" == *.* ]]; then
        WORKER="${CUSTOM_TEMPLATE#*.}"
    fi
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pool) IFS=':' read -r POOL_HOST POOL_PORT <<< "$2"; shift 2 ;;
        --wallet) WALLET="$2"; shift 2 ;;
        --worker) WORKER="$2"; shift 2 ;;
        --pass) PASS="$2"; shift 2 ;;
        --batch-size) BATCH="$2"; shift 2 ;;
        *) EXTRA_ARGS+="$1 "; shift ;;
    esac
done

[[ -z "$WORKER" ]] && WORKER="${WORKER_NAME:-$(hostname -s)}"

# Strip accidental wallet/worker text from extra args (Hive flight sheet mistake)
EXTRA_ARGS="$(echo "$EXTRA_ARGS" | tr '\n' ' ' | sed -E 's/btx1[a-z0-9]{20,}//g' | xargs || true)"

mkdir -p "$(dirname "$config_file")"
{
    echo "# SUPERHERO flight sheet config — do not edit by hand"
    printf 'POOL=%q\n' "${POOL_HOST}:${POOL_PORT}"
    printf 'WALLET=%q\n' "$WALLET"
    printf 'WORKER=%q\n' "$WORKER"
    printf 'PASS=%q\n' "$PASS"
    [[ -n "$BATCH" ]] && printf 'BATCH=%q\n' "$BATCH"
    [[ -n "$EXTRA_ARGS" ]] && printf 'EXTRA_ARGS=%q\n' "$EXTRA_ARGS"
} > "$config_file"

echo "Generated $config_file (pool=${POOL_HOST}:${POOL_PORT} wallet=$WALLET worker=$WORKER)"
