#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="${1:-1.0.0}"
OUT="$ROOT/dist/superminer-hiveos-${VER}.tar.gz"
mkdir -p "$ROOT/dist/pkg/superminer"
cp -f "$ROOT/out/superminer" "$ROOT/dist/pkg/superminer/" 2>/dev/null || \
  cp -f "$ROOT/out/superminer" "$ROOT/dist/pkg/superminer/superminer" 2>/dev/null || true
cp -f "$ROOT/out/"*.so "$ROOT/dist/pkg/superminer/" 2>/dev/null || true
cp -f "$ROOT/hiveos/superminer/"* "$ROOT/dist/pkg/superminer/"
chmod +x "$ROOT/dist/pkg/superminer/"*.sh
chmod +x "$ROOT/dist/pkg/superminer/superminer" 2>/dev/null || true
tar -C "$ROOT/dist/pkg" -czf "$OUT" superminer
echo "Created $OUT"
ls -la "$OUT"
