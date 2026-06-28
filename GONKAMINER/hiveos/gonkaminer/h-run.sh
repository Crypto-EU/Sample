#!/usr/bin/env bash
set -o pipefail

. h-manifest.conf
export MINER_NAME CUSTOM_NAME CUSTOM_VERSION
export GONKAMINER_HIVE=1

# RDNA2 default (RX 6800 XT); override via flight-sheet extra config.
export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"

export LD_LIBRARY_PATH="$(pwd):${LD_LIBRARY_PATH:-}"
for rocm_lib in /opt/rocm/lib /opt/rocm-6.4.3/lib /opt/rocm-6.2.4/lib \
  /hive/drivers/amdgpu-pro/rocm/lib ./rocm/lib; do
  [[ -d $rocm_lib ]] && export LD_LIBRARY_PATH="${rocm_lib}:${LD_LIBRARY_PATH}"
done

export PYTORCH_ROCM_ARCH="${PYTORCH_ROCM_ARCH:-gfx1030}"

PORT="${GONKAMINER_PORT:-8080}"
MINER_ROOT="$(pwd)"
if [[ -d "${MINER_ROOT}/vendor/gonka-pow/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor/gonka-pow"
elif [[ -d "${MINER_ROOT}/vendor/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor"
else
  echo "ERROR: vendored PoC code missing — reinstall GONKAMINER package"
  exit 1
fi
VENV="${MINER_ROOT}/.venv"
SCRIPTS="${MINER_ROOT}/scripts"

venv_ready() {
  [[ -x $VENV/bin/python ]] && "$VENV/bin/python" -c "import pip" 2>/dev/null
}

if ! venv_ready; then
  if [[ -x $SCRIPTS/setup-venv.sh ]]; then
    bash "$SCRIPTS/setup-venv.sh" "$VENV" || exit 1
  else
    echo "Creating Python venv ..."
    if [[ -d $VENV ]]; then rm -rf "$VENV"; fi
    if ! python3 -m venv "$VENV" 2>/dev/null; then
      echo "ERROR: python3-venv required. Run on HiveOS shell:"
      echo "  apt-get update && apt-get install -y python3.10-venv python3-pip"
      echo "  rm -rf $VENV && python3 -m venv $VENV"
      exit 1
    fi
  fi
fi

if [[ -x $SCRIPTS/install-rocm-torch.sh ]]; then
  bash "$SCRIPTS/install-rocm-torch.sh" "$VENV" || exit 1
else
  echo "WARN: install-rocm-torch.sh missing — pip may install wrong CUDA torch"
  "$VENV/bin/pip" install -q -r "${MINER_ROOT}/requirements.txt"
fi

"$VENV/bin/pip" install -q -r "${MINER_ROOT}/requirements.txt"

log="${CUSTOM_LOG_BASENAME}.log"
mkdir -p "$(dirname "$log")" 2>/dev/null || log="/tmp/gonkaminer.log"
: > "$log"

echo "GONKAMINER ${CUSTOM_VERSION} — Gonka PoC worker on port ${PORT}" | tee -a "$log"
echo "Register this host with your Gonka API node (see docs/ANLEITUNG-DE.md)" | tee -a "$log"
echo "LD_LIBRARY_PATH=${LD_LIBRARY_PATH}" >>"$log"
echo "HSA_OVERRIDE_GFX_VERSION=${HSA_OVERRIDE_GFX_VERSION}" >>"$log"

export PYTHONPATH="$POW_DIR"

if [[ -f $SCRIPTS/gonkaminer_preflight.py ]]; then
  if ! "$VENV/bin/python" "$SCRIPTS/gonkaminer_preflight.py" 2>&1 | tee -a "$log"; then
    echo "ERROR: preflight failed — see log above"
    exit 1
  fi
fi

cd "$POW_DIR"

if command -v stdbuf >/dev/null 2>&1; then
  exec stdbuf -oL -eL "$VENV/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
else
  exec "$VENV/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
fi
