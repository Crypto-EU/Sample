#!/usr/bin/env bash
# BTXMAT HiveOS run script (GPU-only AMD)

set -euo pipefail

_script="${BASH_SOURCE[0]}"
while [[ -L "$_script" ]]; do
    _link_dir="$(cd "$(dirname "$_script")" && pwd)"
    _script="$(readlink "$_script")"
    [[ "$_script" != /* ]] && _script="$_link_dir/$_script"
done
cd / 2>/dev/null || cd /tmp
MINER_PATH="$(cd "$(dirname "$_script")" && pwd)"
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
                eval "$line"
                ;;
        esac
    done < "$config_file"
}

load_config

IFS=':' read -r POOL_HOST POOL_PORT <<< "$POOL"

if [[ -z "$WALLET" ]]; then
    echo "BTXMAT FATAL: wallet is empty - set CUSTOM_TEMPLATE to %WAL%.%WORKER_NAME% in flight sheet"
    exit 1
fi

if [[ -z "$POOL_HOST" || -z "$POOL_PORT" ]]; then
    echo "BTXMAT FATAL: pool is empty or invalid ($POOL) - check CUSTOM_URL in flight sheet"
    exit 1
fi

# Auto-detect AMD gfx override when not set in flight sheet
if [[ -z "${HSA_OVERRIDE_GFX_VERSION:-}" ]]; then
    if lspci 2>/dev/null | grep -qiE '5700|5600|5500|Navi 10'; then
        export HSA_OVERRIDE_GFX_VERSION=10.1.0
    elif lspci 2>/dev/null | grep -qiE '6800|6900|6700|6600|Navi 2[0-9]'; then
        export HSA_OVERRIDE_GFX_VERSION=10.3.0
    elif lspci 2>/dev/null | grep -qiE '7900|7800|7700|7600|Navi 3'; then
        export HSA_OVERRIDE_GFX_VERSION=11.0.0
    fi
fi

export OPENCL_VENDOR_PATH="${OPENCL_VENDOR_PATH:-/etc/OpenCL/vendors}"

for amdcl in \
    /opt/amdgpu/lib64/libamdocl64.so \
    /opt/amdgpu-pro/lib/x86_64-linux-gnu/libamdocl64.so \
    /usr/lib/x86_64-linux-gnu/libamdocl64.so; do
    if [[ -f "$amdcl" ]]; then
        export LD_LIBRARY_PATH="$(dirname "$amdcl"):${LD_LIBRARY_PATH:-}"
        break
    fi
done

for libdir in \
    /opt/amdgpu/lib64 \
    /opt/rocm/opencl/lib \
    /opt/rocm/lib \
    /opt/amdgpu-pro/lib/x86_64-linux-gnu \
    /usr/lib/x86_64-linux-gnu \
    /hive/lib; do
    if [[ -d "$libdir" ]]; then
        export LD_LIBRARY_PATH="${libdir}:${LD_LIBRARY_PATH:-}"
    fi
done

export GPU_MAX_ALLOC_PERCENT="${GPU_MAX_ALLOC_PERCENT:-100}"
export GPU_MAX_HEAP_SIZE="${GPU_MAX_HEAP_SIZE:-100}"

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

MINER_BIN="${MINER_PATH}/${CUSTOM_MINERBIN:-btxmat}"

echo "BTXMAT v${CUSTOM_VERSION:-1.0.0} starting pool=${POOL_HOST}:${POOL_PORT} wallet=$WALLET worker=$WORKER"
echo "BTXMAT dir=${MINER_PATH}"
if [[ -n "${HSA_OVERRIDE_GFX_VERSION:-}" ]]; then
    echo "BTXMAT HSA_OVERRIDE_GFX_VERSION=${HSA_OVERRIDE_GFX_VERSION}"
fi

exec "$MINER_BIN" "${ARGS[@]}" 2>&1 | tee -a "$LOG"
