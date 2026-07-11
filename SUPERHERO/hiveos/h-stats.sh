#!/usr/bin/env bash
# SUPERHERO HiveOS stats parser

LOG="$MINER_DIR/h-run.log"
HASHRATE="0"
ACCEPTED=0
REJECTED=0

if [[ -f "$LOG" ]]; then
    HASHRATE=$(grep -oP 'Benchmark:.*=>\s*\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    [[ -z "$HASHRATE" ]] && HASHRATE=$(grep -oP 'hashrate[=: ]\K[0-9.]+' "$LOG" 2>/dev/null | tail -1)
    ACCEPTED=$(grep -c 'share OK' "$LOG" 2>/dev/null || echo 0)
    REJECTED=$(grep -c 'share REJECTED' "$LOG" 2>/dev/null || echo 0)
fi

echo "Hashrate: ${HASHRATE:-0}"
echo "Accepted: $ACCEPTED"
echo "Rejected: $REJECTED"
echo "Algo: btx-matmul"
