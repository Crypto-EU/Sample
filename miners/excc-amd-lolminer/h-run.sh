#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

[[ -f /hive-config/wallet.conf ]] && . /hive-config/wallet.conf || true
. ./h-manifest.conf

if [[ ! -f "$CUSTOM_CONFIG_FILENAME" ]]; then
  echo "Custom miner config not found: $CUSTOM_CONFIG_FILENAME" >&2
  echo "HiveOS should run h-config.sh before h-run.sh." >&2
  exit 1
fi

# shellcheck source=/dev/null
. "$CUSTOM_CONFIG_FILENAME"

mkdir -p "$(dirname "$CUSTOM_LOG_BASENAME")"

./bin/install_lolminer.sh

LOL_MINER_BIN="${PWD}/lolminer/lolMiner"
if [[ ! -x "$LOL_MINER_BIN" ]]; then
  echo "lolMiner binary is missing or not executable: $LOL_MINER_BIN" >&2
  exit 1
fi

if [[ -n "${EXTRA_ARGS:-}" ]]; then
  # HiveOS custom user config is intentionally treated as additional CLI args.
  eval "MINER_ARGS+=( ${EXTRA_ARGS} )"
fi

export GPU_FORCE_64BIT_PTR="${GPU_FORCE_64BIT_PTR:-1}"
export GPU_MAX_HEAP_SIZE="${GPU_MAX_HEAP_SIZE:-100}"
export GPU_MAX_ALLOC_PERCENT="${GPU_MAX_ALLOC_PERCENT:-100}"
export GPU_SINGLE_ALLOC_PERCENT="${GPU_SINGLE_ALLOC_PERCENT:-100}"
export GPU_MAX_SINGLE_ALLOC_PERCENT="${GPU_MAX_SINGLE_ALLOC_PERCENT:-100}"
export GPU_USE_SYNC_OBJECTS="${GPU_USE_SYNC_OBJECTS:-1}"
if [[ -n "${EXCC_HSA_ENABLE_SDMA:-}" ]]; then
  export HSA_ENABLE_SDMA="$EXCC_HSA_ENABLE_SDMA"
fi

log_file="${CUSTOM_LOG_BASENAME}.log"
echo "Starting EXCC AMD miner with lolMiner..."
echo "GPU profile: ${EXCC_GPU_PROFILE:-generic-amd}"
echo "Log file: $log_file"
printf 'Command: %q' "$LOL_MINER_BIN"
printf ' %q' "${MINER_ARGS[@]}"
printf '\n'

exec "$LOL_MINER_BIN" "${MINER_ARGS[@]}"
