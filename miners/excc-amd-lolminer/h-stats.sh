#!/usr/bin/env bash
# This file is sourced by the HiveOS agent and must define $khs and $stats.

manifest_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
if [[ -f "$manifest_dir/h-manifest.conf" ]]; then
  . "$manifest_dir/h-manifest.conf"
else
  CUSTOM_VERSION="1.0.0"
  CUSTOM_LOG_BASENAME="/var/log/miner/excc-amd-lolminer/excc-amd-lolminer"
fi

log_file="${CUSTOM_LOG_BASENAME}.log"
hashrate_hs="0"

if [[ -f "$log_file" ]]; then
  hashrate_hs="$(
    awk '
      function normalize(value, unit) {
        unit = tolower(unit)
        gsub(/[^a-z0-9\/]/, "", unit)

        if (unit ~ /^(ksol\/s|kh\/s|khs)$/) return value * 1000
        if (unit ~ /^(msol\/s|mh\/s|mhs)$/) return value * 1000000
        if (unit ~ /^(gsol\/s|gh\/s|ghs)$/) return value * 1000000000
        return value
      }
      {
        line = tolower($0)
        if (line !~ /(total|speed|average)/ || line !~ /(sol\/s|h\/s|khs|mhs|ghs)/) next

        for (i = 1; i < NF; i++) {
          token = $i
          gsub(/[:,]/, "", token)
          if (token ~ /^[0-9]+(\.[0-9]+)?$/ && $(i + 1) ~ /(sol\/s|h\/s|khs|mhs|ghs)/) {
            value = normalize(token + 0, $(i + 1))
          }
        }
      }
      END {
        if (value == "") value = 0
        printf "%.3f", value
      }
    ' "$log_file"
  )"
fi

khs="$(awk -v hs="$hashrate_hs" 'BEGIN { printf "%.6f", hs / 1000 }')"
stats="$(printf '{"hs":[%.3f],"hs_units":"hs","khs":%.6f,"algo":"EQUI144_5","ver":"%s"}' "$hashrate_hs" "$khs" "$CUSTOM_VERSION")"
