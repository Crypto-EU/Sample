#!/usr/bin/env bash
# SUPERHERO HiveOS run script (GPU-only)

MINER_DIR="${MINER_DIR:-$(cd "$(dirname "$0")" && pwd)}"
cd "$MINER_DIR" || exit 1

CONF="$MINER_DIR/config.conf"
POOL="${POOL:-stratum.minebtx.com:3333}"
WALLET="${WALLET:-}"
WORKER="${WORKER:-$(hostname -s)}"
PASS="${PASS:-x}"
BATCH="${BATCH:-262144}"
WORKGROUP="${WORKGROUP:-256}"
EXTRA_ARGS=""

if [[ -f "$CONF" ]]; then
    # shellcheck disable=SC1090
    source "$CONF"
fi

IFS=':' read -r POOL_HOST POOL_PORT <<< "$POOL"

if [[ -z "$WALLET" ]]; then
    echo "SUPERHERO FATAL: wallet is empty - set wallet in Hive flight sheet (CUSTOM_TEMPLATE)"
    exit 1
fi

if [[ -z "$POOL_HOST" || -z "$POOL_PORT" ]]; then
    echo "SUPERHERO FATAL: pool is empty or invalid ($POOL) - check CUSTOM_URL in flight sheet"
    exit 1
fi

# AMD OpenCL runtime on HiveOS
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

echo "SUPERHERO starting pool=${POOL_HOST}:${POOL_PORT} wallet=$WALLET worker=$WORKER"

exec ./superhero "${ARGS[@]}" 2>&1 | tee -a "$MINER_DIR/h-run.log"
