#!/usr/bin/env python3
"""Pre-flight checks before starting GONKAMINER on HiveOS / AMD."""
from __future__ import annotations

import os
import sys


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []

    try:
        import torch
    except ImportError:
        print("FAIL: PyTorch not installed — run scripts/install-rocm-torch.sh")
        return 1

    hip = getattr(torch.version, "hip", None)
    cuda_ver = getattr(torch.version, "cuda", None)
    if cuda_ver and not hip:
        errors.append(
            "PyTorch is a CUDA (NVIDIA) build — reinstall with scripts/install-rocm-torch.sh"
        )
    elif not hip:
        warnings.append("torch.version.hip not set — verify this is a ROCm wheel")

    if not torch.cuda.is_available():
        errors.append(
            "torch.cuda.is_available() is False — ROCm driver or HSA_OVERRIDE_GFX_VERSION missing"
        )
    else:
        count = torch.cuda.device_count()
        print(f"GPUs detected: {count}")
        for i in range(count):
            props = torch.cuda.get_device_properties(i)
            vram_gb = props.total_memory / (1024**3)
            name = props.name
            print(f"  [{i}] {name} — {vram_gb:.1f} GB VRAM")
            if vram_gb < 10.0:
                warnings.append(
                    f"GPU {i} has {vram_gb:.1f} GB — PoC PARAMS_V1 needs ~10 GB free"
                )

    hsa = os.environ.get("HSA_OVERRIDE_GFX_VERSION", "")
    if not hsa:
        warnings.append(
            "HSA_OVERRIDE_GFX_VERSION unset — set 10.3.0 for RX 6800 XT (gfx1030)"
        )
    else:
        print(f"HSA_OVERRIDE_GFX_VERSION={hsa}")

    for w in warnings:
        print(f"WARN: {w}")
    for e in errors:
        print(f"FAIL: {e}")

    if errors:
        print("\nPreflight failed. Fix the issues above before mining.")
        return 1

    print("Preflight OK — starting PoC worker")
    return 0


if __name__ == "__main__":
    sys.exit(main())
