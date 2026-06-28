#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="${1:-0.1.0}"
OUT="$ROOT/dist/gonkaminer-hiveos-${VER}.tar.gz"
PKG="$ROOT/dist/pkg/gonkaminer"

"$ROOT/scripts/vendor-gonka-pow.sh"

rm -rf "$ROOT/dist/pkg"
mkdir -p "$PKG"

cp -a "$ROOT/vendor/gonka-pow" "$PKG/vendor/"
cp -f "$ROOT/requirements.txt" "$PKG/"
cp -f "$ROOT/hiveos/gonkaminer/"* "$PKG/"
chmod +x "$PKG/"*.sh 2>/dev/null || true

tar -C "$ROOT/dist/pkg" -czf "$OUT" gonkaminer
echo "Created $OUT"
sha256sum "$OUT"
