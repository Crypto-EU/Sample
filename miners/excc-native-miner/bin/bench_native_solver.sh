#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
SOLVER_BIN="${MINER_DIR}/excc-native-solver"
THREADS="${EXCC_NATIVE_THREADS:-$(nproc 2>/dev/null || echo 1)}"
RANGE="${EXCC_NATIVE_BENCH_RANGE:-1}"

"${MINER_DIR}/bin/build_native_solver.sh"

if [[ ! -x "$SOLVER_BIN" ]]; then
  echo "Native solver is missing or not executable: $SOLVER_BIN" >&2
  exit 1
fi

# 192-byte synthetic EXCC-style header. The solver overwrites bytes 140..143
# with the nonce for each round.
HEADER_HEX="$(python3 - <<'PY'
print("00" * 192)
PY
)"

echo "Benchmarking native EXCC solver"
echo "Threads: $THREADS"
echo "Range: $RANGE nonce(s)"
"$SOLVER_BIN" -x "$HEADER_HEX" -n 0 -r "$RANGE" -t "$THREADS"
