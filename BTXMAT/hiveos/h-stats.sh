#!/usr/bin/env bash
# BTXMAT HiveOS stats (sets $khs and $stats for agent)

MINER_PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "${MINER_PATH}/h-manifest.conf"

LOG="${CUSTOM_LOG_BASENAME:-${MINER_PATH}/h-run}.log"
HASHRATE="0"
ACCEPTED=0
REJECTED=0

if [[ -f "$LOG" ]]; then
    HASHRATE=$(grep -oP 'Benchmark:.*=>\s*\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    [[ -z "$HASHRATE" ]] && HASHRATE=$(grep -oP 'hashrate[=: ]\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    ACCEPTED=$(grep -c 'share found' "$LOG" 2>/dev/null || echo 0)
    REJECTED=$(grep -c 'share REJECTED' "$LOG" 2>/dev/null || echo 0)
fi

khs="$HASHRATE"
stats=$(cat <<EOF
{
  "hs": [$HASHRATE],
  "hs_units": "hs",
  "algo": "btx-matmul",
  "ar": [$ACCEPTED, $REJECTED],
  "ver": "${CUSTOM_VERSION:-1.0.0}"
}
EOF
)
