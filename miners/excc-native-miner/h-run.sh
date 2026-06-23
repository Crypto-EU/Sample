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

./bin/build_native_solver.sh

SOLVER_BIN="${PWD}/excc-native-solver"
if [[ ! -x "$SOLVER_BIN" ]]; then
  echo "Native solver is missing or not executable: $SOLVER_BIN" >&2
  exit 1
fi

export GPU_FORCE_64BIT_PTR="${GPU_FORCE_64BIT_PTR:-1}"
export GPU_MAX_HEAP_SIZE="${GPU_MAX_HEAP_SIZE:-100}"
export GPU_MAX_ALLOC_PERCENT="${GPU_MAX_ALLOC_PERCENT:-100}"
export GPU_SINGLE_ALLOC_PERCENT="${GPU_SINGLE_ALLOC_PERCENT:-100}"
export GPU_MAX_SINGLE_ALLOC_PERCENT="${GPU_MAX_SINGLE_ALLOC_PERCENT:-100}"
export GPU_USE_SYNC_OBJECTS="${GPU_USE_SYNC_OBJECTS:-1}"
export HSA_ENABLE_SDMA="${HSA_ENABLE_SDMA:-0}"

log_file="${CUSTOM_LOG_BASENAME}.log"
args=(
  --pool "$EXCC_NATIVE_POOL"
  --user "$EXCC_NATIVE_USER"
  --password "$EXCC_NATIVE_PASS"
  --solver "$SOLVER_BIN"
  --threads "$EXCC_NATIVE_THREADS"
  --range "$EXCC_NATIVE_RANGE"
  --solver-timeout "$EXCC_NATIVE_SOLVER_TIMEOUT"
)

if [[ -n "${EXTRA_ARGS:-}" ]]; then
  # HiveOS custom user config is intentionally treated as additional CLI args.
  eval "args+=( ${EXTRA_ARGS} )"
fi

echo "Starting native EXCC miner..."
echo "Backend: native Tromp Equihash 144/5 solver, no lolMiner"
echo "Log file: $log_file"
printf 'Command: python3 ./bin/excc_native_miner.py'
printf ' %q' "${args[@]}"
printf '\n'

exec python3 ./bin/excc_native_miner.py "${args[@]}" 2>&1 | tee -a "$log_file"
