#!/usr/bin/env bash
set -o pipefail

. h-manifest.conf
export MINER_NAME CUSTOM_NAME CUSTOM_VERSION
export SUPERMINER_HIVE=1

# Defaults for RX 6000 (gfx1030); may be overridden via flight-sheet extra config.
export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"

# Bundled miner + ROCm/HIP libs must resolve from the install directory.
export LD_LIBRARY_PATH="$(pwd):${LD_LIBRARY_PATH:-}"
if [[ -d /opt/rocm/lib ]]; then
  export LD_LIBRARY_PATH="/opt/rocm/lib:${LD_LIBRARY_PATH}"
fi
if [[ -d ./rocm/lib ]]; then
  export LD_LIBRARY_PATH="./rocm/lib:${LD_LIBRARY_PATH}"
  export PATH="./rocm/bin:${PATH}"
fi

if [[ -x ./superminer ]]; then
  ver=$(./superminer --version 2>/dev/null | head -1 | tr -d '\r')
  [[ $ver == SUPERMINER-* ]] && export MINER_VERSION="$ver"
fi

if [[ ! -f ./libpearl_gemm_capi.so ]]; then
  echo "ERROR: libpearl_gemm_capi.so missing — reinstall SUPERMINER 1.1.1+ package"
  exit 1
fi

if pgrep -x superminer > /dev/null 2>&1; then
  echo "superminer is already running"
  exit 1
fi

# Split flight-sheet extra config into shell exports (VAR=val) and miner CLI flags (--foo).
parse_extra_config() {
  local raw="$1"
  local token miner_flags=""
  raw="${raw//;/ }"
  for token in $raw; do
    [[ -z $token ]] && continue
    if [[ $token =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
      export "$token"
    else
      miner_flags+=" $token"
    fi
  done
  printf '%s' "$miner_flags"
}

conf=""
if [[ -f $CUSTOM_CONFIG_FILENAME ]]; then
  conf=$(cat "$CUSTOM_CONFIG_FILENAME" 2>/dev/null || true)
fi
conf="${conf//$'\r'/}"
conf="${conf//$'\n'/ }"

user_cfg="${CUSTOM_USER_CONFIG:-$conf}"
user_cfg="${user_cfg//$'\r'/}"
user_cfg="${user_cfg//$'\n'/ }"
extra_args=$(parse_extra_config "$user_cfg")

pool="${CUSTOM_URL:-pool.pearlhash.xyz:9000}"
wallet="${CUSTOM_TEMPLATE:-}"
worker="${WORKER_NAME:-$(hostname)}"

if [[ -z $wallet || $wallet == "%WAL%" ]]; then
  echo "ERROR: Pearl wallet missing — assign a wallet (prl1…) in the flight sheet"
  exit 1
fi

args="--pearl-mine --pool stratum+tcp://${pool} --wallet ${wallet} --worker ${worker}"

if [[ -n ${CUSTOM_PASS:-} ]]; then
  args+=" --password ${CUSTOM_PASS}"
fi

if [[ -n $extra_args ]]; then
  args+=" ${extra_args}"
fi

log="${CUSTOM_LOG_BASENAME}.log"
if ! mkdir -p "$(dirname "$log")" 2>/dev/null; then
  log="/tmp/${CUSTOM_NAME:-superminer}.log"
  mkdir -p "$(dirname "$log")" 2>/dev/null || true
fi
: > "$log"

echo "Starting SUPERMINER: ${args}" | tee -a "$log"
echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}" >>"$log"
echo "HSA_OVERRIDE_GFX_VERSION=${HSA_OVERRIDE_GFX_VERSION:-}" >>"$log"

if ! ./superminer --self-test >>"$log" 2>&1; then
  echo "WARN: self-test reported issues — starting miner anyway" | tee -a "$log"
fi

run_miner() {
  ./superminer ${args} 2>&1 | tee -a "$log"
}

if command -v unbuffer >/dev/null 2>&1; then
  unbuffer ./superminer ${args} 2>&1 | tee -a "$log"
elif command -v stdbuf >/dev/null 2>&1; then
  stdbuf -oL -eL ./superminer ${args} 2>&1 | tee -a "$log"
else
  run_miner
fi
