#!/usr/bin/env bash
set -o pipefail

. h-manifest.conf
export MINER_NAME CUSTOM_NAME CUSTOM_VERSION
export GONKAMINER_HIVE=1

# RDNA2 default (RX 6000); override via flight-sheet extra config.
export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"

export LD_LIBRARY_PATH="$(pwd):${LD_LIBRARY_PATH:-}"
for rocm_lib in /opt/rocm/lib /opt/rocm-6.4.3/lib /opt/rocm-6.2.4/lib \
  /hive/drivers/amdgpu-pro/rocm/lib ./rocm/lib; do
  [[ -d $rocm_lib ]] && export LD_LIBRARY_PATH="${rocm_lib}:${LD_LIBRARY_PATH}"
done

# PyTorch ROCm uses the CUDA compatibility layer on HiveOS.
export PYTORCH_ROCM_ARCH="${PYTORCH_ROCM_ARCH:-gfx1030}"

PORT="${GONKAMINER_PORT:-8080}"
MINER_ROOT="$(pwd)"
POW_DIR="${MINER_ROOT}/vendor/gonka-pow"
VENV="${MINER_ROOT}/.venv"

if [[ ! -d $POW_DIR/pow ]]; then
  echo "ERROR: vendor/gonka-pow missing — reinstall GONKAMINER package"
  exit 1
fi

if [[ ! -x $VENV/bin/python ]]; then
  echo "Creating Python venv ..."
  python3 -m venv "$VENV" || {
    echo "ERROR: python3-venv required on HiveOS"
    exit 1
  }
  "$VENV/bin/pip" install -q -r "${MINER_ROOT}/requirements.txt"
fi

log="${CUSTOM_LOG_BASENAME}.log"
mkdir -p "$(dirname "$log")" 2>/dev/null || log="/tmp/gonkaminer.log"
: > "$log"

echo "GONKAMINER ${CUSTOM_VERSION} — Gonka PoC worker on port ${PORT}" | tee -a "$log"
echo "Register this host with your Gonka API node (see docs/ANLEITUNG-DE.md)" | tee -a "$log"
echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}" >>"$log"
echo "HSA_OVERRIDE_GFX_VERSION=${HSA_OVERRIDE_GFX_VERSION}" >>"$log"

export PYTHONPATH="$POW_DIR"
cd "$POW_DIR"

if command -v stdbuf >/dev/null 2>&1; then
  exec stdbuf -oL -eL "$VENV/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
else
  exec "$VENV/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
fi
