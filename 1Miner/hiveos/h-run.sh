#!/usr/bin/env bash
# Runs 1Miner and mirrors console output into the Hive miner log.

set -o pipefail

miner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$miner_dir" || exit 1

[[ -f "$miner_dir/h-manifest.conf" ]] && . "$miner_dir/h-manifest.conf"

mkdir -p "$(dirname "$CUSTOM_LOG_BASENAME")"
log_file="${CUSTOM_LOG_BASENAME}.log"
config_file="${CUSTOM_CONFIG_FILENAME:-$miner_dir/1miner.conf}"
miner_bin="$miner_dir/1miner"

: > "$log_file"

if [[ ! -x "$miner_bin" ]]; then
  echo "ERROR: 1miner binary not found or not executable: $miner_bin" | tee -a "$log_file"
  echo "Build it with: cmake -S . -B build && cmake --build build && cp build/1miner $miner_bin" | tee -a "$log_file"
  exit 1
fi

if [[ ! -f "$config_file" ]]; then
  echo "WARN: config file not found, creating it via h-config.sh" | tee -a "$log_file"
  "$miner_dir/h-config.sh"
fi

cmdline="$(cat "$config_file" 2>/dev/null)"
export ONE_MINER_KERNEL_PATH="${ONE_MINER_KERNEL_PATH:-$miner_dir/opencl_kernels.cl}"

echo "Starting 1miner with args: $cmdline" | tee -a "$log_file"

if command -v stdbuf >/dev/null 2>&1; then
  stdbuf -oL -eL "$miner_bin" $cmdline 2>&1 | tee -a "$log_file"
else
  "$miner_bin" $cmdline 2>&1 | tee -a "$log_file"
fi
