#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
LOL_MINER_BIN="${MINER_DIR}/lolminer/lolMiner"
SECONDS_PER_TEST="${EXCC_TUNE_SECONDS:-90}"
CANDIDATES="${EXCC_TUNE_KEEPFREE_VALUES:-0 4 8 16 32}"
DEVICES="${EXCC_DEVICES:-AMD}"

parse_hashrate_hs() {
  local log_file="$1"

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
}

if ! command -v timeout >/dev/null 2>&1; then
  echo "The timeout command is required for bounded benchmark runs." >&2
  exit 1
fi

"${MINER_DIR}/bin/install_lolminer.sh"

if [[ ! -x "$LOL_MINER_BIN" ]]; then
  echo "lolMiner binary is missing or not executable: $LOL_MINER_BIN" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

best_keepfree=""
best_hashrate="0"

echo "Benchmarking lolMiner Equihash 144/5 on devices: $DEVICES"
echo "Seconds per candidate: $SECONDS_PER_TEST"
echo "Candidates: $CANDIDATES"
echo

for keepfree in $CANDIDATES; do
  log_file="${tmp_dir}/keepfree_${keepfree}.log"
  echo "Testing --keepfree ${keepfree}..."

  set +e
  timeout "$SECONDS_PER_TEST" "$LOL_MINER_BIN" \
    --benchmark EQUI144_5 \
    --devices "$DEVICES" \
    --keepfree "$keepfree" \
    --nocolor on \
    --compactaccept on \
    --shortstats 10 \
    --longstats 30 \
    --log on \
    --logfile "$log_file" \
    > "$log_file.stdout" 2>&1
  rc=$?
  set -e

  if [[ "$rc" -ne 0 && "$rc" -ne 124 ]]; then
    echo "  benchmark failed with exit code $rc"
    sed -n '1,20p' "$log_file.stdout" >&2
    continue
  fi

  if [[ ! -s "$log_file" ]]; then
    cp "$log_file.stdout" "$log_file"
  fi

  hashrate="$(parse_hashrate_hs "$log_file")"
  printf '  result: %.3f h/s\n' "$hashrate"

  if awk -v current="$hashrate" -v best="$best_hashrate" 'BEGIN { exit !(current > best) }'; then
    best_hashrate="$hashrate"
    best_keepfree="$keepfree"
  fi
done

echo
if [[ -z "$best_keepfree" ]]; then
  echo "No successful benchmark result was detected." >&2
  echo "Try increasing EXCC_TUNE_SECONDS or run lolMiner --list-devices manually." >&2
  exit 1
fi

printf 'Best candidate: EXCC_KEEPFREE=%s at %.3f h/s\n' "$best_keepfree" "$best_hashrate"
echo "Set EXCC_KEEPFREE=${best_keepfree} in the HiveOS custom miner environment, or add '--keepfree ${best_keepfree}' as an extra argument."
