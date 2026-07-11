#!/usr/bin/env bash
# SUPERHERO HiveOS configuration generator

POOL_HOST=""
POOL_PORT=""
WALLET=""
WORKER=""
PASS="x"
THREADS=""
BATCH=""
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pool) IFS=':' read -r POOL_HOST POOL_PORT <<< "$2"; shift 2 ;;
        --wallet) WALLET="$2"; shift 2 ;;
        --worker) WORKER="$2"; shift 2 ;;
        --pass) PASS="$2"; shift 2 ;;
        --threads) THREADS="$2"; shift 2 ;;
        --batch-size) BATCH="$2"; shift 2 ;;
        *) EXTRA_ARGS+="$1 "; shift ;;
    esac
done

[[ -z "$WORKER" ]] && WORKER="$(hostname -s)"

mkdir -p "$MINER_DIR"
CONF="$MINER_DIR/config.conf"
{
    echo "# SUPERHERO flight sheet config"
    echo "POOL=$POOL_HOST:$POOL_PORT"
    echo "WALLET=$WALLET"
    echo "WORKER=$WORKER"
    echo "PASS=$PASS"
    [[ -n "$THREADS" ]] && echo "THREADS=$THREADS"
    [[ -n "$BATCH" ]] && echo "BATCH=$BATCH"
} > "$CONF"

echo "Generated $CONF"
