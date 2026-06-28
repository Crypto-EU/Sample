#!/usr/bin/env bash
# Install or repair PyTorch ROCm wheels (HiveOS / AMD). Default pip torch is CUDA-only.
set -euo pipefail

VENV="${1:?usage: install-rocm-torch.sh /path/to/.venv}"
PIP="${VENV}/bin/pip"
PYTHON="${VENV}/bin/python"

if [[ ! -x $PYTHON ]]; then
  echo "ERROR: venv python not found: $PYTHON"
  exit 1
fi

ROCM_INDEX="${PYTORCH_ROCM_INDEX:-https://download.pytorch.org/whl/rocm6.2}"

rocm_torch_ok() {
  "$PYTHON" - <<'PY'
import sys

try:
    import torch
except ImportError:
    sys.exit(1)

hip = getattr(torch.version, "hip", None)
cuda = getattr(torch.version, "cuda", None)

# ROCm builds expose hip; CUDA-only wheels must not be used on AMD hosts.
if hip and torch.cuda.is_available():
    sys.exit(0)

# Wrong wheel: NVIDIA CUDA build without HIP on a machine where we expect ROCm.
if cuda and not hip:
    sys.exit(1)

sys.exit(1)
PY
}

if rocm_torch_ok; then
  ver="$("$PYTHON" -c 'import torch; print(torch.__version__)')"
  hip_ver="$("$PYTHON" -c 'import torch; print(torch.version.hip or "n/a")')"
  echo "ROCm PyTorch OK: ${ver} (HIP ${hip_ver})"
  exit 0
fi

echo "Installing PyTorch for ROCm (${ROCM_INDEX}) ..."
"$PIP" uninstall -y torch torchvision torchaudio 2>/dev/null || true
"$PIP" install --upgrade pip
"$PIP" install torch torchvision torchaudio --index-url "$ROCM_INDEX"

if ! rocm_torch_ok; then
  echo "ERROR: ROCm PyTorch install failed — torch.cuda.is_available() is False"
  echo "Check: amdgpu driver, /opt/rocm, HSA_OVERRIDE_GFX_VERSION (RX 6800 XT: 10.3.0)"
  exit 1
fi

echo "ROCm PyTorch installed: $("$PYTHON" -c 'import torch; print(torch.__version__)')"
