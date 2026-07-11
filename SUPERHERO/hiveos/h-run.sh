#!/usr/bin/env bash
# SUPERHERO HiveOS run script (GPU-only)

set -euo pipefail

MINER_PATH="$(cd "$(dirname "$0")" && pwd)"
cd "$MINER_PATH" || exit 1

# shellcheck source=/dev/null
. "${MINER_PATH}/h-manifest.conf"

config_file="${CUSTOM_CONFIG_FILENAME:-config.conf}"
[[ "$config_file" = /* ]] || config_file="${MINER_PATH}/${config_file}"

POOL="${POOL:-stratum.minebtx.com:3333}"
WALLET="${WALLET:-}"
WORKER="${WORKER:-$(hostname -s)}"
PASS="${PASS:-x}"
BATCH="${BATCH:-262144}"
WORKGROUP="${WORKGROUP:-256}"
EXTRA_ARGS=""

load_config() {
    [[ -f "$config_file" ]] || return 0
    local line key
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "${line//[[:space:]]/}" ]] && continue
        key="${line%%=*}"
        case "$key" in
            POOL|WALLET|WORKER|PASS|BATCH|WORKGROUP|EXTRA_ARGS)
                # Values written with printf %q — safe to eval
                eval "$line"
                ;;
        esac
    done < "$config_file"
}

load_config

IFS=':' read -r POOL_HOST POOL_PORT <<< "$POOL"

if [[ -z "$WALLET" ]]; then
    echo "SUPERHERO FATAL: wallet is empty - set CUSTOM_TEMPLATE to %WAL%.%WORKER_NAME% in flight sheet"
    exit 1
fi

if [[ -z "$POOL_HOST" || -z "$POOL_PORT" ]]; then
    echo "SUPERHERO FATAL: pool is empty or invalid ($POOL) - set CUSTOM_URL in flight sheet"
    exit 1
fi

for libdir in \
    /opt/amdgpu/lib64 \
    /opt/amdgpu-pro/lib/x86_64-linux-gnu \
    /hive/lib; do
    if [[ -d "$libdir" ]]; then
        export LD_LIBRARY_PATH="${libdir}:${LD_LIBRARY_PATH:-}"
    fi
done

export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"
export GPU_MAX_ALLOC_PERCENT="${GPU_MAX_ALLOC_PERCENT:-100}"
export GPU_MAX_HEAP_SIZE="${GPU_MAX_HEAP_SIZE:-100}"
export GPU_FORCE_64BIT_PTR="${GPU_FORCE_64BIT_PTR:-1}"
export GPU_USE_SYNC_OBJECTS="${GPU_USE_SYNC_OBJECTS:-1}"

ARGS=(
    --pool "${POOL_HOST}:${POOL_PORT}"
    --wallet "$WALLET"
    --worker "$WORKER"
    --password "$PASS"
    --batch-size "$BATCH"
    --workgroup "$WORKGROUP"
)

if [[ -n "$EXTRA_ARGS" ]]; then
    # shellcheck disable=SC2206
    EXTRA_ARR=($EXTRA_ARGS)
    ARGS+=("${EXTRA_ARR[@]}")
fi

LOG="${CUSTOM_LOG_BASENAME:-${MINER_PATH}/h-run}.log"
mkdir -p "$(dirname "$LOG")"

echo "SUPERHERO starting pool=${POOL_HOST}:${POOL_PORT} wallet=$WALLET worker=$WORKER"

exec ./"${CUSTOM_MINERBIN:-superhero}" "${ARGS[@]}" 2>&1 | tee -a "$LOG"
