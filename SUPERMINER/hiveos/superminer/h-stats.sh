#!/usr/bin/env bash
# HiveOS stats callback — sources khs + stats for the agent

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$script_dir/h-manifest.conf"

miner_ver="${CUSTOM_NAME}-${CUSTOM_VERSION}"
if [[ -x "$script_dir/superminer" ]]; then
  bin_ver=$("$script_dir/superminer" --version 2>/dev/null | head -1 | tr -d '\r')
  [[ $bin_ver == SUPERMINER-* ]] && miner_ver="$bin_ver"
fi

amd_bus_numbers_json() {
  local out="["
  local first=1
  local line bus
  while IFS= read -r line; do
    bus=$(awk '{print $NF}' <<< "$line" | tr -d ',')
    [[ -z $bus ]] && continue
    if [[ $first -eq 0 ]]; then out+=","; fi
    if [[ $bus =~ ^[0-9A-Fa-f:]+$ ]]; then
      bus=$(awk -F: '{print $2}' <<< "$bus")
    fi
    if [[ $bus =~ ^[0-9A-Fa-f]+$ ]]; then
      out+="$((16#$bus))"
    else
      out+="0"
    fi
    first=0
  done < <(rocm-smi --showbus 2>/dev/null | grep -i bus || true)
  out+="]"
  echo "$out"
}

native_ok=0
native_stats_file="/var/run/hive-miner-${CUSTOM_NAME}.stats.json"
if [[ -s $native_stats_file ]]; then
  native_stats=$(cat "$native_stats_file" 2>/dev/null)
  if jq -e . >/dev/null 2>&1 <<< "$native_stats"; then
    bus=$(amd_bus_numbers_json)
    stats=$(jq -c --arg ver "$miner_ver" --argjson bus "$bus" \
      '.ver = $ver | .bus_numbers = $bus' <<< "$native_stats" 2>/dev/null)
    [[ -z $stats ]] && stats="$native_stats"
    khs=$(jq -r '
      ([.hs[]?] | add // 0) as $sum |
      if (.hs_units // "hs") == "mhs" then $sum * 1000
      elif (.hs_units // "hs") == "khs" then $sum
      else $sum / 1000 end
    ' <<< "$stats" 2>/dev/null)
    khs=${khs:-0}
    native_ok=1
  fi
fi

if [[ $native_ok -eq 0 ]]; then
  log="${CUSTOM_LOG_BASENAME}.log"
  line=$(perl -pe 's/\e\[[0-9;]*[A-Za-z]//g; s/\r$//' "$log" 2>/dev/null \
    | grep 'stratum stats:' | tail -1)
  if [[ -n $line ]]; then
    tmac=$(sed -n 's/.*kernel_tmac_s=\([0-9.]*\).*/\1/p' <<< "$line")
  accepted=$(sed -n 's/.* accepted=\([0-9]*\) .*/\1/p' <<< "$line")
  rejected=$(sed -n 's/.* rejected=\([0-9]*\) .*/\1/p' <<< "$line")
    [[ -z $tmac ]] && tmac=0
    khs=$(awk -v x="$tmac" 'BEGIN { printf "%.3f", x * 1000000000.0 }')
    stats=$(jq -nc --argjson hs "[$khs]" --arg ver "$miner_ver" \
      '{hs:$hs,hs_units:"khs",total_khs:($hs|add),ver:$ver,algo:"pearl"}' 2>/dev/null)
    [[ -z $stats ]] && stats="null"
  else
    khs=0
    stats="null"
  fi
fi
