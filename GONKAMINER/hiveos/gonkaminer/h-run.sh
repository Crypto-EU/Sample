#!/usr/bin/env bash
# HiveOS entry — keep simple; no exec|tee (breaks on some HiveOS bash builds).
set -o pipefail

. h-manifest.conf
export MINER_NAME CUSTOM_NAME CUSTOM_VERSION GONKAMINER_HIVE=1

MINER_ROOT="$(pwd)"
SCRIPTS="${MINER_ROOT}/scripts"
log="${CUSTOM_LOG_BASENAME}.log"
mkdir -p "$(dirname "$log")" 2>/dev/null || log="/tmp/gonkaminer.log"

logmsg() { echo "$*" | tee -a "$log"; }

# Flight-sheet extra config
if [[ -f $CUSTOM_CONFIG_FILENAME ]]; then
  while IFS= read -r line || [[ -n $line ]]; do
    line="${line%%#*}"
    for kv in $line; do
      [[ $kv == *=* ]] || continue
      export "$kv"
    done
  done < "$CUSTOM_CONFIG_FILENAME"
fi

export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"
export PYTORCH_ROCM_ARCH="${PYTORCH_ROCM_ARCH:-gfx1030}"
export GONKAMINER_PORT="${GONKAMINER_PORT:-8080}"
PORT="$GONKAMINER_PORT"

export LD_LIBRARY_PATH="${MINER_ROOT}:${LD_LIBRARY_PATH:-}"
for rocm_lib in /opt/rocm/lib /opt/rocm-6.4.3/lib /opt/rocm-6.4/lib /opt/rocm-6.3/lib \
  /opt/rocm-6.2.4/lib /opt/rocm-6.2/lib /hive/drivers/amdgpu-pro/rocm/lib; do
  [[ -d $rocm_lib ]] && export LD_LIBRARY_PATH="${rocm_lib}:${LD_LIBRARY_PATH}"
done

if [[ -d "${MINER_ROOT}/vendor/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor"
elif [[ -d "${MINER_ROOT}/vendor/gonka-pow/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor/gonka-pow"
else
  logmsg "ERROR: vendor/pow missing"
  exit 1
fi

PYDEPS="${MINER_ROOT}/pydeps"
VENV="${MINER_ROOT}/.venv"

if [[ ! -f "${MINER_ROOT}/.bootstrap_ok" ]]; then
  logmsg "GONKAMINER ${CUSTOM_VERSION} — bootstrap (5-15 min first time) ..."
  bash "${SCRIPTS}/bootstrap.sh" >>"$log" 2>&1 || {
    logmsg "ERROR: bootstrap failed — see ${MINER_ROOT}/bootstrap.log"
    exit 1
  }
fi

[[ -f "${MINER_ROOT}/.python_env" ]] && source "${MINER_ROOT}/.python_env"
export PYTHONPATH="${PYDEPS}:${POW_DIR}:${PYTHONPATH:-}"

run_py() {
  if [[ "${GONKAMINER_PYMODE:-pydeps}" == "venv" && -x "${VENV}/bin/python" ]]; then
    "${VENV}/bin/python" "$@"
  else
    python3 "$@"
  fi
}

logmsg "GONKAMINER ${CUSTOM_VERSION} starting on port ${PORT}"
logmsg "HSA=${HSA_OVERRIDE_GFX_VERSION} PYMODE=${GONKAMINER_PYMODE:-pydeps}"

if [[ -f "${SCRIPTS}/gonkaminer_preflight.py" ]]; then
  run_py "${SCRIPTS}/gonkaminer_preflight.py" >>"$log" 2>&1 || \
    logmsg "WARN: preflight failed — starting worker anyway (see log)"
fi

cd "$POW_DIR" || exit 1

if [[ "${GONKAMINER_PYMODE:-pydeps}" == "venv" && -x "${VENV}/bin/uvicorn" ]]; then
  exec "${VENV}/bin/uvicorn" app:app --host 0.0.0.0 --port "$PORT" >>"$log" 2>&1
else
  exec python3 -m uvicorn app:app --host 0.0.0.0 --port "$PORT" >>"$log" 2>&1
fi
