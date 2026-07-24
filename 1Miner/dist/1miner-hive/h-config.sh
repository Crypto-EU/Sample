#!/usr/bin/env bash
# Builds command-line arguments for 1Miner from Hive OS Flight Sheet fields.
# Pool URL              -> --pool or --nats (auto-detected)
# Wallet/work template  -> --wallet
# Extra config args     -> appended as-is, e.g. --amd-ocl --use-cpu

set -euo pipefail

miner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "$miner_dir/h-manifest.conf" ]] && . "$miner_dir/h-manifest.conf"

mkdir -p "$(dirname "$CUSTOM_CONFIG_FILENAME")"
mkdir -p "$(dirname "$CUSTOM_LOG_BASENAME")"

trim() {
  local s="${1:-}"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

pool="$(trim "${CUSTOM_URL:-}")"
wallet="$(trim "${CUSTOM_TEMPLATE:-}")"
extra="$(trim "${CUSTOM_USER_CONFIG:-}")"

cmd=""

if [[ -n "$pool" ]]; then
  if [[ "$pool" == nats://* || "$pool" == *":4222"* || "$pool" == *":4223"* ]]; then
    cmd+=" --nats $(printf '%q' "$pool")"
  else
    cmd+=" --pool $(printf '%q' "$pool")"
  fi
fi

if [[ -n "$wallet" ]]; then
  cmd+=" --wallet $(printf '%q' "$wallet")"
fi

# Default AMD/OpenCL-friendly flags if user did not pass a backend.
if [[ -n "$extra" ]]; then
  cmd+=" $extra"
elif [[ "$cmd" != *"--use-cpu"* && "$cmd" != *"--opencl"* && "$cmd" != *"--amd-ocl"* ]]; then
  cmd+=" --amd-ocl --use-cpu"
fi

printf '%s\n' "${cmd# }" > "$CUSTOM_CONFIG_FILENAME"
echo "[1miner] Config written:"
cat "$CUSTOM_CONFIG_FILENAME"
