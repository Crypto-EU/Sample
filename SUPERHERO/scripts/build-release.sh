#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-0.1.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
PKG="$ROOT/dist"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

mkdir -p "$PKG/superhero"
rm -rf "$PKG/superhero"
mkdir -p "$PKG/superhero/opencl"
cp "$BUILD/superhero" "$PKG/superhero/"
cp "$ROOT/opencl/superhero.cl" "$PKG/superhero/opencl/"
cp "$ROOT/hiveos/"* "$PKG/superhero/"
chmod +x "$PKG/superhero/"*.sh "$PKG/superhero/superhero"

mkdir -p "$PKG"
tar -C "$PKG" -czf "$PKG/superhero-${VERSION}.tar.gz" superhero
sha256sum "$PKG/superhero-${VERSION}.tar.gz"

echo "Built $PKG/superhero-${VERSION}.tar.gz"
