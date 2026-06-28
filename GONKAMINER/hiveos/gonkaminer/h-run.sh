#!/usr/bin/env bash
set -o pipefail

. h-manifest.conf
export MINER_NAME CUSTOM_NAME CUSTOM_VERSION
export GONKAMINER_HIVE=1

MINER_ROOT="$(pwd)"
SCRIPTS="${MINER_ROOT}/scripts"

# Load flight-sheet extra config (KEY=VAL pairs, space or newline separated).
if [[ -f $CUSTOM_CONFIG_FILENAME ]]; then
  while IFS= read -r line || [[ -n $line ]]; do
    line="${line%%#*}"
    for kv in $line; do
      [[ $kv == *=* ]] || continue
      export "$kv"
    done
  done < "$CUSTOM_CONFIG_FILENAME"
fi

# RX 6800 XT defaults (RDNA2 gfx1030).
export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"
export PYTORCH_ROCM_ARCH="${PYTORCH_ROCM_ARCH:-gfx1030}"
export GONKAMINER_PORT="${GONKAMINER_PORT:-8080}"
PORT="$GONKAMINER_PORT"

export LD_LIBRARY_PATH="$(pwd):${LD_LIBRARY_PATH:-}"
for rocm_lib in /opt/rocm/lib /opt/rocm-6.4.3/lib /opt/rocm-6.4/lib /opt/rocm-6.3/lib \
  /opt/rocm-6.2.4/lib /opt/rocm-6.2/lib /opt/rocm-6.1/lib \
  /hive/drivers/amdgpu-pro/rocm/lib ./rocm/lib; do
  [[ -d $rocm_lib ]] && export LD_LIBRARY_PATH="${rocm_lib}:${LD_LIBRARY_PATH}"
done

if [[ -d "${MINER_ROOT}/vendor/gonka-pow/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor/gonka-pow"
elif [[ -d "${MINER_ROOT}/vendor/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor"
else
  echo "ERROR: vendor/pow missing — run: bash ${SCRIPTS}/install-hiveos-shell.sh"
  exit 1
fi

log="${CUSTOM_LOG_BASENAME}.log"
mkdir -p "$(dirname "$log")" 2>/dev/null || log="/tmp/gonkaminer.log"

# Bootstrap once (pydeps or venv). Re-run if broken.
if [[ ! -f "${MINER_ROOT}/.bootstrap_ok" ]] || [[ "${GONKAMINER_FORCE_BOOTSTRAP:-0}" == "1" ]]; then
  echo "GONKAMINER ${CUSTOM_VERSION} — first-time setup (5-15 min possible) ..." | tee "$log"
  if ! bash "${SCRIPTS}/bootstrap.sh" 2>&1 | tee -a "$log"; then
    echo "ERROR: bootstrap failed — run on rig: bash ${SCRIPTS}/doctor.sh" | tee -a "$log"
    exit 1
  fi
fi

[[ -f "${MINER_ROOT}/.python_env" ]] && source "${MINER_ROOT}/.python_env"

PYDEPS="${MINER_ROOT}/pydeps"
VENV="${MINER_ROOT}/.venv"
export PYTHONPATH="${PYDEPS}:${POW_DIR}:${PYTHONPATH:-}"

run_py() {
  if [[ "${GONKAMINER_PYMODE:-pydeps}" == "venv" && -x "${VENV}/bin/python" ]]; then
    "${VENV}/bin/python" "$@"
  else
    python3 "$@"
  fi
}

echo "GONKAMINER ${CUSTOM_VERSION} — PoC worker port ${PORT}" | tee -a "$log"
echo "HSA=${HSA_OVERRIDE_GFX_VERSION} ARCH=${PYTORCH_ROCM_ARCH}" | tee -a "$log"

if [[ -f "${SCRIPTS}/gonkaminer_preflight.py" ]]; then
  if ! run_py "${SCRIPTS}/gonkaminer_preflight.py" 2>&1 | tee -a "$log"; then
    echo "WARN: preflight failed — trying bootstrap repair ..." | tee -a "$log"
    rm -f "${MINER_ROOT}/.bootstrap_ok"
    GONKAMINER_FORCE_BOOTSTRAP=1 bash "${SCRIPTS}/bootstrap.sh" 2>&1 | tee -a "$log" || exit 1
    [[ -f "${MINER_ROOT}/.python_env" ]] && source "${MINER_ROOT}/.python_env"
    run_py "${SCRIPTS}/gonkaminer_preflight.py" 2>&1 | tee -a "$log" || {
      echo "ERROR: GPU/ROCm not ready — bash ${SCRIPTS}/doctor.sh" | tee -a "$log"
      exit 1
    }
  fi
fi

cd "$POW_DIR"
export PYTHONPATH="${PYDEPS}:${POW_DIR}:${PYTHONPATH:-}"

if command -v stdbuf >/dev/null 2>&1; then
  if [[ "${GONKAMINER_PYMODE:-pydeps}" == "venv" && -x "${VENV}/bin/uvicorn" ]]; then
    exec stdbuf -oL -eL "${VENV}/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
  else
    exec stdbuf -oL -eL python3 -m uvicorn app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
  fi
else
  if [[ "${GONKAMINER_PYMODE:-pydeps}" == "venv" && -x "${VENV}/bin/uvicorn" ]]; then
    exec "${VENV}/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
  else
    exec python3 -m uvicorn app:app --host 0.0.0.0 --port "$PORT" 2>&1 | tee -a "$log"
  fi
fi
