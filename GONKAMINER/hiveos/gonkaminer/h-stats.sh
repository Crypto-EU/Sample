#!/usr/bin/env bash
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$script_dir/h-manifest.conf"

miner_ver="${CUSTOM_NAME}-${CUSTOM_VERSION}"
log="${CUSTOM_LOG_BASENAME}.log"

# PoC throughput: parse uvicorn / pow logs for batch sizes if present.
nonces=0
if [[ -f $log ]]; then
  nonces=$(grep -oE 'nonces=[0-9]+' "$log" 2>/dev/null | tail -1 | grep -oE '[0-9]+' || echo 0)
fi

# Placeholder hashrate metric for Hive dashboard (nonces processed in last log line).
khs=$(awk -v n="$nonces" 'BEGIN { printf "%.3f", n / 1000.0 }')
stats=$(jq -nc --argjson hs "[$khs]" --arg ver "$miner_ver" \
  '{hs:$hs,hs_units:"khs",total_khs:($hs|add),ver:$ver,algo:"gonka-poc"}' 2>/dev/null)
[[ -z $stats ]] && stats="null"

echo "khs=$khs"
echo "stats=$stats"
