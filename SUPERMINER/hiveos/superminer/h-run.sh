#!/usr/bin/env bash
set -o pipefail

. h-manifest.conf
export MINER_NAME CUSTOM_NAME CUSTOM_VERSION
export SUPERMINER_HIVE=1
export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-}"

# Prefer bundled ROCm user-space when shipped in the tarball
if [[ -d ./rocm/lib ]]; then
  export LD_LIBRARY_PATH="./rocm/lib:${LD_LIBRARY_PATH:-}"
  export PATH="./rocm/bin:${PATH}"
fi

if [[ -x ./superminer ]]; then
  ver=$(./superminer --version 2>/dev/null | head -1 | tr -d '\r')
  [[ $ver == SUPERMINER-* ]] && export MINER_VERSION="$ver"
fi

if pgrep -x superminer > /dev/null 2>&1; then
  echo "superminer is already running"
  exit 1
fi

conf=$(cat "$CUSTOM_CONFIG_FILENAME")
[[ $conf =~ ';' ]] && conf=$(echo "$conf" | tr -d '\')

pool="${CUSTOM_URL:-pool.pearlhash.xyz:9000}"
wallet="${CUSTOM_TEMPLATE:-}"
worker="${WORKER_NAME:-$(hostname)}"

args="--pearl-mine --pool stratum+tcp://${pool} --wallet ${wallet} --worker ${worker}"

if [[ -n ${CUSTOM_PASS:-} ]]; then
  args+=" --password ${CUSTOM_PASS}"
fi

# Extra user args from flight sheet (e.g. --devices 0,1 --pearl-k 4096)
if [[ -n ${CUSTOM_USER_CONFIG:-} && ${CUSTOM_USER_CONFIG} != "$conf" ]]; then
  args+=" ${CUSTOM_USER_CONFIG}"
fi

log="${CUSTOM_LOG_BASENAME}.log"
: > "$log"

echo "Starting SUPERMINER: ${args}"
unbuffer ./superminer ${args//;/'\;'} 2>&1 | tee -a "$log"
