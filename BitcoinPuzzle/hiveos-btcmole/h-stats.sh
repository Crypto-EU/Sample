#!/usr/bin/env bash
# Minimal Hive stats: parse Keys/s / Mkey/s from recent log lines.
set -euo pipefail

miner_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "$miner_dir/h-manifest.conf" ]] && . "$miner_dir/h-manifest.conf"

log_file="${CUSTOM_LOG_BASENAME}.log"
khs=0
uptime=0
accepted=0

if [[ -f "$log_file" ]]; then
  # Match numbers near key/s style units
  line="$(tail -n 80 "$log_file" | tr '[:upper:]' '[:lower:]' | grep -E 'mkey/s|mkeys/s|key/s|keys/s|mk/s' | tail -n 1 || true)"
  if [[ -n "$line" ]]; then
    num="$(echo "$line" | grep -oE '[0-9]+([.][0-9]+)?' | tail -n 1 || true)"
    if [[ -n "${num:-}" ]]; then
      if echo "$line" | grep -qE 'mkey/s|mkeys/s|mk/s'; then
        # Mkey/s → khs (Hive expects kH/s-ish); store as Mkey*1000
        khs="$(awk -v n="$num" 'BEGIN{printf "%.0f", n*1000}')"
      else
        # key/s → /1000 for khs
        khs="$(awk -v n="$num" 'BEGIN{printf "%.3f", n/1000}')"
      fi
    fi
  fi
  if grep -qE 'TREASURE KEY FOUND|FOUND_PUZZLE|FOUND_KEY' "$log_file"; then
    accepted=1
  fi
  # rough uptime from log mtime vs first line — best effort
  if [[ -n "$(find "$log_file" -mmin -120 2>/dev/null || true)" ]]; then
    uptime=120
  fi
fi

# Hive custom miner JSON stats (simplified)
cat <<EOF
{"hs":[${khs}],"hs_units":"khs","temp":[],"fan":[],"uptime":${uptime},"ver":"${CUSTOM_VERSION:-1.0.0}","ar":[${accepted},0],"algo":"btc-puzzle"}
EOF
