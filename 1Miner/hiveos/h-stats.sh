#!/usr/bin/env bash
# Parses 1Miner stats JSON and/or GPU status table for Hive OS.

miner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "$miner_dir/h-manifest.conf" ]] && . "$miner_dir/h-manifest.conf"

STATS_FILE="${ONE_MINER_STATS_FILE:-/tmp/saseul-miner-stats.json}"
log_name="${CUSTOM_LOG_BASENAME:-/var/log/miner/1miner/1miner}.log"

khs=0
stats='{"temp":[],"fan":[],"hs":[],"hs_units":"mhs","ar":[0,0],"bus_numbers":[]}'

if [[ -s "$STATS_FILE" ]] && command -v jq >/dev/null 2>&1; then
  age=$(( $(date +%s) - $(stat -c %Y "$STATS_FILE" 2>/dev/null || echo 0) ))
  if (( age <= 90 )); then
    total_mhs=$(jq -r '.hashrate_mhs // 0' "$STATS_FILE")
    accepted=$(jq -r '.accepted // .shares_session // 0' "$STATS_FILE")
    rejected=$(jq -r '.rejected // 0' "$STATS_FILE")
    ver=$(jq -r '.version // "1.0.0"' "$STATS_FILE")
    hs=$(jq -c '[.gpus[]?.hashrate_mhs // 0]' "$STATS_FILE" 2>/dev/null || echo '[]')
    khs=$(awk -v m="$total_mhs" 'BEGIN { printf "%.0f", m*1000 }')

    # temperatures / fans from nvidia-smi or amd-info when available
    temp_json='[]'
    fan_json='[]'
    if command -v nvidia-smi >/dev/null 2>&1; then
      mapfile -t temps < <(nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null)
      mapfile -t fans < <(nvidia-smi --query-gpu=fan.speed --format=csv,noheader,nounits 2>/dev/null)
      temp_json=$(printf '%s\n' "${temps[@]}" | jq -R 'tonumber? // 0' | jq -s '.')
      fan_json=$(printf '%s\n' "${fans[@]}" | jq -R 'tonumber? // 0' | jq -s '.')
    fi

    stats=$(jq -nc \
      --argjson hs "$hs" \
      --argjson temp "$temp_json" \
      --argjson fan "$fan_json" \
      --argjson acc "$accepted" \
      --argjson rej "$rejected" \
      --arg ver "$ver" \
      --arg algo "${CUSTOM_ALGO:-saseul}" \
      '{hs:$hs,hs_units:"mhs",temp:$temp,fan:$fan,ar:[$acc,$rej],ver:$ver,algo:$algo}')
  fi
fi

if [[ "${1:-}" == "--print" || "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "khs=$khs"
  echo "stats=$stats"
fi
