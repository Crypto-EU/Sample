#!/usr/bin/env bash
# GONKAMINER bootstrap — idempotent setup for HiveOS / AMD (no fragile venv required).
set -uo pipefail

MINER_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPTS="${MINER_ROOT}/scripts"
PYDEPS="${MINER_ROOT}/pydeps"
VENV="${MINER_ROOT}/.venv"
STAMP="${MINER_ROOT}/.bootstrap_ok"
LOG="${MINER_ROOT}/bootstrap.log"

export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"
export PYTORCH_ROCM_ARCH="${PYTORCH_ROCM_ARCH:-gfx1030}"
export LD_LIBRARY_PATH="${MINER_ROOT}:${LD_LIBRARY_PATH:-}"
for rocm_lib in /opt/rocm/lib /opt/rocm-6.4.3/lib /opt/rocm-6.4/lib /opt/rocm-6.3/lib \
  /opt/rocm-6.2.4/lib /opt/rocm-6.2/lib /hive/drivers/amdgpu-pro/rocm/lib; do
  [[ -d $rocm_lib ]] && export LD_LIBRARY_PATH="${rocm_lib}:${LD_LIBRARY_PATH}"
done

if [[ -d "${MINER_ROOT}/vendor/gonka-pow/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor/gonka-pow"
elif [[ -d "${MINER_ROOT}/vendor/pow" ]]; then
  POW_DIR="${MINER_ROOT}/vendor"
else
  echo "ERROR: vendor/pow missing in ${MINER_ROOT}"
  exit 1
fi

log() { echo "[bootstrap] $*" | tee -a "$LOG"; }

install_os_packages() {
  command -v apt-get >/dev/null 2>&1 || return 0
  export DEBIAN_FRONTEND=noninteractive
  log "Installing OS packages (python3-pip) ..."
  apt-get update -qq 2>>"$LOG" || true
  apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv python3.10-venv \
    ca-certificates curl wget 2>>"$LOG" || true
}

detect_rocm_index() {
  local ver
  for ver in 6.4 6.3 6.2 6.1 6.0 5.7; do
    [[ -d "/opt/rocm-${ver}" ]] && { echo "https://download.pytorch.org/whl/rocm${ver}"; return; }
  done
  echo "https://download.pytorch.org/whl/rocm6.2"
}

imports_ready() {
  PYTHONPATH="${PYDEPS}:${POW_DIR}" python3 - <<'PY' 2>/dev/null
import sys
try:
    import torch, uvicorn, fastapi
except ImportError:
    sys.exit(1)
if not getattr(torch.version, "hip", None):
    sys.exit(1)
sys.exit(0)
PY
}

gpu_ready() {
  PYTHONPATH="${PYDEPS}:${POW_DIR}" python3 - <<'PY' 2>/dev/null
import sys
import torch
sys.exit(0 if torch.cuda.is_available() else 1)
PY
}

venv_imports_ready() {
  [[ -x "${VENV}/bin/python" ]] && "${VENV}/bin/python" - <<'PY' 2>/dev/null
import torch, uvicorn, fastapi
import sys
if not getattr(torch.version, "hip", None):
    sys.exit(1)
sys.exit(0)
PY
}

install_pydeps() {
  local rocm_index="${PYTORCH_ROCM_INDEX:-$(detect_rocm_index)}"
  mkdir -p "$PYDEPS"
  log "Installing Python deps to ${PYDEPS} ..."
  python3 -m pip install --target "$PYDEPS" \
    -r "${MINER_ROOT}/requirements.txt" 2>>"$LOG" || return 1
  log "Installing ROCm PyTorch from ${rocm_index} (5-15 min) ..."
  python3 -m pip install --target "$PYDEPS" \
    torch torchvision torchaudio --index-url "$rocm_index" 2>>"$LOG" || return 1
}

install_venv_path() {
  log "Fallback: .venv ..."
  bash "${SCRIPTS}/setup-venv.sh" "$VENV" 2>>"$LOG" || return 1
  export PYTORCH_ROCM_INDEX="${PYTORCH_ROCM_INDEX:-$(detect_rocm_index)}"
  bash "${SCRIPTS}/install-rocm-torch.sh" "$VENV" 2>>"$LOG" || return 1
  "${VENV}/bin/pip" install -q -r "${MINER_ROOT}/requirements.txt" 2>>"$LOG"
}

write_runner() {
  cat > "${MINER_ROOT}/.python_env" <<EOF
export GONKAMINER_PYMODE="${PYMODE}"
export GONKAMINER_PYDEPS="${PYDEPS}"
export GONKAMINER_VENV="${VENV}"
export GONKAMINER_POW_DIR="${POW_DIR}"
EOF
}

main() {
  : > "$LOG"
  log "bootstrap in ${MINER_ROOT} HSA=${HSA_OVERRIDE_GFX_VERSION}"

  if [[ -f $STAMP ]] && { imports_ready || venv_imports_ready; }; then
    if [[ -d $PYDEPS ]] && imports_ready; then PYMODE=pydeps
    else PYMODE=venv; fi
    write_runner
    gpu_ready && log "GPU OK" || log "WARN: GPU not visible yet (check HSA_OVERRIDE_GFX_VERSION)"
    exit 0
  fi

  install_os_packages
  rm -f "$STAMP"
  rm -rf "$PYDEPS"

  PYMODE=pydeps
  if ! install_pydeps || ! imports_ready; then
    log "pydeps failed — venv fallback"
    rm -rf "$PYDEPS"
    PYMODE=venv
    if ! install_venv_path || ! venv_imports_ready; then
      log "ERROR: bootstrap failed — see ${LOG}"
      exit 1
    fi
  fi

  write_runner
  date -Iseconds > "$STAMP"
  log "Bootstrap OK mode=${PYMODE}"
  gpu_ready && log "GPU OK" || log "WARN: GPU not visible — set HSA_OVERRIDE_GFX_VERSION=10.3.0"
}

main "$@"
