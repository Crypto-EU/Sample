#!/usr/bin/env bash
# This file is sourced by the HiveOS agent and must define $khs and $stats.

manifest_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
if [[ -f "$manifest_dir/h-manifest.conf" ]]; then
  . "$manifest_dir/h-manifest.conf"
else
  CUSTOM_VERSION="0.1.0"
  CUSTOM_LOG_BASENAME="/var/log/miner/excc-native-miner/excc-native-miner"
fi

log_file="${CUSTOM_LOG_BASENAME}.log"
hashrate_hs="0"
accepted="0"
rejected="0"

if [[ -f "$log_file" ]]; then
  hashrate_hs="$(awk '/native rate=/ { for (i = 1; i <= NF; i++) if ($i ~ /^rate=/) { sub(/^rate=/, "", $i); value=$i } } END { if (value == "") value=0; printf "%.6f", value }' "$log_file")"
  accepted="$(awk '/share accepted/ { for (i = 1; i <= NF; i++) if ($i ~ /^accepted=/) { sub(/^accepted=/, "", $i); value=$i } } END { if (value == "") value=0; print value }' "$log_file")"
  rejected="$(awk '/share rejected/ { for (i = 1; i <= NF; i++) if ($i ~ /^rejected=/) { sub(/^rejected=/, "", $i); value=$i } } END { if (value == "") value=0; print value }' "$log_file")"
fi

khs="$(awk -v hs="$hashrate_hs" 'BEGIN { printf "%.9f", hs / 1000 }')"
stats="$(printf '{"hs":[%.6f],"hs_units":"hs","khs":%.9f,"algo":"EQUI144_5","ver":"%s","ar":[%s,%s]}' "$hashrate_hs" "$khs" "$CUSTOM_VERSION" "$accepted" "$rejected")"
