#!/usr/bin/env bash
# AMD / HiveOS fixes applied on top of vendored gonka-ai/gonka PoC sources.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/vendor/gonka-pow"

if [[ ! -d $DEST/pow ]]; then
  echo "ERROR: $DEST/pow missing — run vendor-gonka-pow.sh first"
  exit 1
fi

python3 - <<PY
from pathlib import Path

root = Path("${DEST}")

# gpu_group.py — allow 16 GB consumer cards (RX 6800 XT)
p = root / "pow/compute/gpu_group.py"
text = p.read_text()
old = """def get_min_group_vram(params: Params) -> float:
    if params == PARAMS_V1:
        return 10.0
    elif params == PARAMS_V2:
        return 38.0
    else:
        return 38.0"""
new = """def get_min_group_vram(params: Params) -> float:
    if params == PARAMS_V2:
        return 38.0
    # PARAMS_V1 and default Params() fit on 16 GB consumer cards (e.g. RX 6800 XT).
    return 10.0"""
if old not in text:
    raise SystemExit(f"gpu_group.py patch mismatch in {p}")
p.write_text(text.replace(old, new, 1))

# model_init.py — real single-GPU fallback
p = root / "pow/compute/model_init.py"
text = p.read_text()
old = """            except Exception as e:
                logger.error(f"Multi-GPU distribution failed: {e}")
                logger.error("Falling back to single GPU")
                raise e"""
new = """            except Exception as e:
                logger.error(f"Multi-GPU distribution failed: {e}")
                logger.warning(f"Falling back to single GPU: {primary_device}")
                try:
                    model = model.to(primary_device)
                    logger.info("Single-GPU placement successful")
                except Exception as fallback_err:
                    logger.error(f"Single-GPU fallback failed: {fallback_err}")
                    raise fallback_err"""
if old not in text:
    raise SystemExit(f"model_init.py patch mismatch in {p}")
p.write_text(text.replace(old, new, 1))

# autobs.py — unused sympy import crashes on minimal venv
p = root / "pow/compute/autobs.py"
text = p.read_text()
if text.startswith("from sympy import mobius\n"):
    p.write_text(text.replace("from sympy import mobius\n", "", 1))

# routes.py — do not kill worker on GPU resource errors
p = root / "pow/service/routes.py"
text = p.read_text()
text = text.replace("import os\n\n", "", 1)
text = text.replace("from starlette.background import BackgroundTask\n", "", 1)
old_block = """    except NotEnoughGPUResources as e:
        logger.critical(f"GPU resources unavailable: {e}. Shutting down.")
        return JSONResponse(
            status_code=503,
            content={"detail": str(e)},
            background=BackgroundTask(os._exit, 1),
        )"""
new_block = """    except NotEnoughGPUResources as e:
        logger.error(f"GPU resources unavailable: {e}")
        return JSONResponse(status_code=503, content={"detail": str(e)})"""
count = text.count(old_block)
if count != 3:
    raise SystemExit(f"routes.py: expected 3 os._exit blocks, found {count}")
p.write_text(text.replace(old_block, new_block))

print("AMD patches applied to vendor/gonka-pow")
PY
