#!/usr/bin/env bash
# Starts BtcMole and mirrors output into the Hive miner log.
set -o pipefail

miner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$miner_dir" || exit 1

[[ -f "$miner_dir/h-manifest.conf" ]] && . "$miner_dir/h-manifest.conf"

mkdir -p "$(dirname "$CUSTOM_LOG_BASENAME")"
log_file="${CUSTOM_LOG_BASENAME}.log"
config_file="${CUSTOM_CONFIG_FILENAME:-$miner_dir/btcmole.conf}"

# Prefer versioned bmXXX binary, fall back to btcmole
bin=""
for candidate in "$miner_dir"/bm[0-9][0-9][0-9] "$miner_dir/btcmole"; do
  if [[ -x "$candidate" ]]; then
    bin="$candidate"
    break
  fi
done

: > "$log_file"

if [[ -z "$bin" ]]; then
  echo "ERROR: no btcmole/bmXXX binary found in $miner_dir" | tee -a "$log_file"
  echo "Place linux amdgpu build here (chmod +x) and repack the Hive tarball." | tee -a "$log_file"
  exit 1
fi

if [[ ! -f "$config_file" ]]; then
  echo "WARN: config missing, running h-config.sh" | tee -a "$log_file"
  "$miner_dir/h-config.sh"
fi

cmdline="$(cat "$config_file" 2>/dev/null || true)"

# Optional GFX override from environment (set in Extra config as KEY=VAL args parsed below)
# Extra line may start with env assignments, e.g.:
#   HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu
env_prefix=""
args=()
# shellcheck disable=SC2086
set -- $cmdline
while [[ $# -gt 0 ]]; do
  case "$1" in
    *=*)
      export "$1"
      env_prefix+="$1 "
      shift
      ;;
    *)
      args+=("$@")
      break
      ;;
  esac
done

echo "Starting $bin ${args[*]}" | tee -a "$log_file"
echo "env: HSA_OVERRIDE_GFX_VERSION=${HSA_OVERRIDE_GFX_VERSION:-<unset>}" | tee -a "$log_file"

if command -v stdbuf >/dev/null 2>&1; then
  stdbuf -oL -eL "$bin" "${args[@]}" 2>&1 | tee -a "$log_file"
else
  "$bin" "${args[@]}" 2>&1 | tee -a "$log_file"
fi
