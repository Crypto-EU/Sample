#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
GPU_SOLVER_BIN="${MINER_DIR}/excc-gpu-solver"
THREADS="${EXCC_NATIVE_THREADS:-$(nproc 2>/dev/null || echo 1)}"
RANGE="${EXCC_NATIVE_BENCH_RANGE:-1}"

"${MINER_DIR}/bin/build_gpu_solver.sh"

if [[ ! -x "$GPU_SOLVER_BIN" ]]; then
  echo "GPU solver is missing or not executable: $GPU_SOLVER_BIN" >&2
  exit 1
fi

# 180-byte synthetic EXCC Equihash header. The solver overwrites bytes 140..143
# with the nonce for each round.
HEADER_HEX="$(python3 - <<'PY'
print("00" * 180)
PY
)"

echo "Benchmarking GPU EXCC solver"
echo "Backend: ${EXCC_GPU_BACKEND:-cuda}"
echo "Threads argument: $THREADS (ignored by CUDA backend)"
echo "Range: $RANGE nonce(s)"
"$GPU_SOLVER_BIN" -x "$HEADER_HEX" -n 0 -r "$RANGE" -t "$THREADS"
