#!/usr/bin/env bash
# SUPERMINER build — AMD HIP/ROCm Pearl (PEARL/Pearlhash) GPU miner
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

OUT="${OUT:-$ROOT/out}"
CONFIG="${CONFIG:-Release}"
ARCH="${ARCH:-auto}"   # auto | gfx1030 | gfx1100 | gfx1101 | gfx1200 | gfx942 | fat

echo "==> SUPERMINER build (config=$CONFIG out=$OUT arch=$ARCH)"

# ── 1. Rust: pearl-blake3 + pearl-mining-capi + superminer-share ───────────
echo "==> building Rust native libraries"
cargo build --release --manifest-path "$ROOT/native/Cargo.toml"
mkdir -p "$OUT/lib"
cp -f "$ROOT/native/target/release/libpearl_mining_capi.so" "$OUT/lib/"
cp -f "$ROOT/native/target/release/libsuperminer_share.so" "$OUT/lib/" 2>/dev/null || true

# ── 2. HIP/ROCm kernels ────────────────────────────────────────────────────
if command -v hipcc >/dev/null 2>&1; then
  echo "==> building HIP kernels"
  ROCM_DIR="${ROCM:-/opt/rocm}"
  MAKE_ARCH="$ARCH"
  if [[ $ARCH == auto ]]; then
    MAKE_ARCH="fat"
  fi
  make -C "$ROOT/native/pearl-gemm/csrc/rocm/host" \
    ARCH="$MAKE_ARCH" ROCM="$ROCM_DIR" HIPCC="${HIPCC:-hipcc}" \
    OUT_DIR="$OUT/lib"
  cp -f "$ROOT/native/pearl-gemm/csrc/rocm/host/libcuda.so.1" "$OUT/lib/" 2>/dev/null || true
else
  echo "!! hipcc not found — skipping GPU kernel build (install ROCm 6.x/7.x)"
  echo "   On HiveOS AMD rigs: apt install rocm-hip-sdk or use bundled libs"
fi

# ── 3. C++ host miner ──────────────────────────────────────────────────────
echo "==> building SUPERMINER host"
mkdir -p "$OUT/build"
cmake -S "$ROOT" -B "$OUT/build" \
  -DCMAKE_BUILD_TYPE="$CONFIG" \
  -DCMAKE_INSTALL_PREFIX="$OUT"
cmake --build "$OUT/build" -j"$(nproc)"
cmake --install "$OUT/build"

# Stage libs next to binary for dlopen
cp -f "$OUT/lib/"*.so "$OUT/" 2>/dev/null || true

echo ""
echo "==> SUPERMINER ready: $OUT/superminer"
"$OUT/superminer" --version 2>/dev/null || true
