#!/usr/bin/env bash
# Builds command-line arguments for 1Miner from Hive OS Flight Sheet fields.
# Pool URL              -> --pool or --nats (auto-detected)
# Wallet/work template  -> --wallet
# Extra config args     -> optional, e.g. --device 0,1 --nonce-mode latehex
# Backend is always AMD OpenCL (CPU/NVIDIA disabled).

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

# Reject legacy CPU/NVIDIA flags if pasted into Extra config.
if [[ "$extra" == *"--use-cpu"* || "$extra" == *"--cuda"* || "$extra" == *"--nvidia-ocl"* ]]; then
  echo "[1miner] ERROR: CPU/NVIDIA flags are not supported (AMD-only)" >&2
  exit 1
fi

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

# Always AMD OpenCL. Autotune runs by default in the binary (per GPU).
cmd+=" --amd-ocl"

if [[ -n "$extra" ]]; then
  cmd+=" $extra"
fi

printf '%s\n' "${cmd# }" > "$CUSTOM_CONFIG_FILENAME"
echo "[1miner] Config written (AMD-only):"
cat "$CUSTOM_CONFIG_FILENAME"
