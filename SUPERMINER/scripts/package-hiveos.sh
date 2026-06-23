#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="${1:-1.0.1}"
OUT="$ROOT/dist/superminer-hiveos-${VER}.tar.gz"
PKG="$ROOT/dist/pkg/superminer"

rm -rf "$ROOT/dist/pkg"
mkdir -p "$PKG"

cp -f "$ROOT/out/superminer" "$PKG/" || {
  echo "ERROR: out/superminer missing — run ./build.sh first"
  exit 1
}

missing=0
for lib in libpearl_gemm_capi.so libsuperminer_share.so libpearl_mining_capi.so libcuda.so.1; do
  if [[ -f "$ROOT/out/$lib" ]]; then
    cp -f "$ROOT/out/$lib" "$PKG/"
  elif [[ -f "$ROOT/out/lib/$lib" ]]; then
    cp -f "$ROOT/out/lib/$lib" "$PKG/"
  else
    echo "ERROR: required library missing: $lib"
    missing=1
  fi
done
if [[ $missing -ne 0 ]]; then
  exit 1
fi

cp -f "$ROOT/hiveos/superminer/"* "$PKG/"
chmod +x "$PKG/"*.sh "$PKG/superminer"
tar -C "$ROOT/dist/pkg" -czf "$OUT" superminer
echo "Created $OUT"
sha256sum "$OUT"
ls -la "$OUT" "$PKG/"
