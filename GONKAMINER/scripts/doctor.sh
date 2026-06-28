#!/usr/bin/env bash
# GONKAMINER diagnostics for HiveOS / AMD rigs.
set -uo pipefail

MINER_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAIL=0

ok()   { echo "  OK   $*"; }
bad()  { echo "  FAIL $*"; FAIL=1; }
warn() { echo "  WARN $*"; }

echo "=== GONKAMINER doctor ==="
echo "Miner root: ${MINER_ROOT}"
echo ""

echo "[1] Files"
for f in h-manifest.conf h-config.sh h-run.sh h-stats.sh vendor/pow/compute/gpu_group.py; do
  [[ -f "${MINER_ROOT}/${f}" ]] && ok "$f" || bad "$f missing"
done

echo ""
echo "[2] GPU / ROCm"
if command -v rocm-smi >/dev/null 2>&1; then
  ok "rocm-smi found"
  rocm-smi 2>/dev/null | head -5 | sed 's/^/       /'
else
  warn "rocm-smi not found"
fi
echo "       HSA_OVERRIDE_GFX_VERSION=${HSA_OVERRIDE_GFX_VERSION:-not set (use 10.3.0 for RX 6800 XT)}"

echo ""
echo "[3] Python"
if command -v python3 >/dev/null 2>&1; then
  ok "python3: $(python3 --version 2>&1)"
else
  bad "python3 not found"
fi
python3 -m pip --version >/dev/null 2>&1 && ok "pip available" || bad "pip missing — apt install python3-pip"

echo ""
echo "[4] Bootstrap state"
[[ -f "${MINER_ROOT}/.bootstrap_ok" ]] && ok "bootstrap stamp exists" || warn "bootstrap not completed"
[[ -d "${MINER_ROOT}/pydeps" ]] && ok "pydeps dir exists" || warn "pydeps missing"
[[ -x "${MINER_ROOT}/.venv/bin/python" ]] && ok ".venv exists" || warn ".venv missing"

echo ""
echo "[5] PyTorch / ROCm (if bootstrapped)"
export PYTHONPATH="${MINER_ROOT}/pydeps:${MINER_ROOT}/vendor:${PYTHONPATH:-}"
if python3 - <<'PY' 2>/dev/null
import torch
print("torch", torch.__version__, "hip", getattr(torch.version, "hip", None))
print("cuda_available", torch.cuda.is_available())
if torch.cuda.is_available():
    for i in range(torch.cuda.device_count()):
        p = torch.cuda.get_device_properties(i)
        print(f"gpu{i}", p.name, f"{p.total_memory/1024**3:.1f}GB")
PY
then
  ok "torch import"
else
  bad "torch not working — run: bash ${MINER_ROOT}/scripts/bootstrap.sh"
fi

echo ""
echo "[6] Service"
if curl -sf --max-time 3 http://127.0.0.1:8080/health >/dev/null 2>&1; then
  ok "http://127.0.0.1:8080/health"
else
  warn "PoC worker not running on port 8080"
fi

echo ""
if [[ $FAIL -eq 0 ]]; then
  echo "=== Result: PASS ==="
else
  echo "=== Result: FAIL — fix items above, then run: ==="
  echo "  cd ${MINER_ROOT} && bash scripts/bootstrap.sh"
fi
exit $FAIL
