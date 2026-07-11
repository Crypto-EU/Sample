#!/usr/bin/env bash
# SUPERHERO HiveOS run script (GPU-only)

cd "$MINER_DIR" || exit 1

CONF="$MINER_DIR/config.conf"
POOL="${POOL:-stratum.minebtx.com:3333}"
WALLET="${WALLET:-}"
WORKER="${WORKER:-$(hostname -s)}"
PASS="${PASS:-x}"
BATCH="${BATCH:-262144}"
WORKGROUP="${WORKGROUP:-256}"

if [[ -f "$CONF" ]]; then
    # shellcheck disable=SC1090
    source "$CONF"
fi

IFS=':' read -r POOL_HOST POOL_PORT <<< "$POOL"

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

exec ./superhero "${ARGS[@]}" 2>&1 | tee -a "$MINER_DIR/h-run.log"
