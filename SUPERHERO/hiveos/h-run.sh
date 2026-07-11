#!/usr/bin/env bash
# SUPERHERO HiveOS run script

cd "$MINER_DIR" || exit 1

CONF="$MINER_DIR/config.conf"
POOL="${POOL:-stratum.minebtx.com:3333}"
WALLET="${WALLET:-}"
WORKER="${WORKER:-$(hostname -s)}"
PASS="${PASS:-x}"
THREADS="${THREADS:-0}"
BATCH="${BATCH:-500000}"

if [[ -f "$CONF" ]]; then
    # shellcheck disable=SC1090
    source "$CONF"
fi

IFS=':' read -r POOL_HOST POOL_PORT <<< "$POOL"

export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"
export GPU_MAX_ALLOC_PERCENT="${GPU_MAX_ALLOC_PERCENT:-100}"
export GPU_MAX_HEAP_SIZE="${GPU_MAX_HEAP_SIZE:-100}"

ARGS=(
    --pool "${POOL_HOST}:${POOL_PORT}"
    --wallet "$WALLET"
    --worker "$WORKER"
    --password "$PASS"
    --batch-size "$BATCH"
)

[[ "$THREADS" != "0" && -n "$THREADS" ]] && ARGS+=(--threads "$THREADS")
[[ "${SUPERHERO_NO_OPENCL:-0}" == "1" ]] && ARGS+=(--no-opencl)

# HiveOS custom miner convention: log to h-run.log
exec ./superhero "${ARGS[@]}" 2>&1 | tee -a "$MINER_DIR/h-run.log"
