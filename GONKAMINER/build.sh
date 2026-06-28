#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

./scripts/vendor-gonka-pow.sh

if [[ ! -d vendor/gonka-pow/pow ]]; then
  echo "ERROR: vendor step failed"
  exit 1
fi

echo "GONKAMINER vendor ready. On HiveOS with ROCm PyTorch, run:"
echo "  PYTHONPATH=vendor/gonka-pow uvicorn app:app --host 0.0.0.0 --port 8080"
echo "  (from vendor/gonka-pow directory)"
