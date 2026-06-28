#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="${1:-0.1.3}"
# HiveOS custom-get parses miner name from archive: gonkaminer-VERSION.tar.gz → miner "gonkaminer"
# Do NOT use gonkaminer-hiveos-VERSION (detected name becomes "gonkaminer-hiveos").
OUT="$ROOT/dist/gonkaminer-${VER}.tar.gz"
PKG="$ROOT/dist/pkg/gonkaminer"

"$ROOT/scripts/vendor-gonka-pow.sh"

rm -rf "$ROOT/dist/pkg"
mkdir -p "$PKG"

cp -a "$ROOT/vendor/gonka-pow" "$PKG/vendor/"
cp -a "$ROOT/scripts" "$PKG/"
cp -f "$ROOT/requirements.txt" "$PKG/"
cp -f "$ROOT/hiveos/gonkaminer/"* "$PKG/"
find "$PKG" -name '*.sh' -exec chmod +x {} +
chmod +x "$ROOT/scripts/install-hiveos-shell.sh" 2>/dev/null || true

tar -C "$ROOT/dist/pkg" -czf "$OUT" gonkaminer
echo "Created $OUT"
sha256sum "$OUT"
