#!/usr/bin/env bash
# Builds Extra-config arguments from the HiveOS Flight Sheet into btcmole.conf
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

extra="$(trim "${CUSTOM_USER_CONFIG:-}")"

# Default: solo Puzzle 71 on all AMD GPUs
if [[ -z "$extra" ]]; then
  extra="bf -pz 71 +amdgpu"
fi

printf '%s\n' "$extra" > "$CUSTOM_CONFIG_FILENAME"
echo "[btcmole] Config written:"
cat "$CUSTOM_CONFIG_FILENAME"
