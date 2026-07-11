#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-1.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BTXMAT_BUILD_DIR:-$ROOT/build}"
PKG="$ROOT/dist"

if [[ "${HIVEOS_BUILD:-1}" == "1" ]]; then
    exec "$ROOT/scripts/build-hiveos.sh" "$VERSION"
fi

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

mkdir -p "$PKG/btxmat"
rm -rf "$PKG/btxmat"
mkdir -p "$PKG/btxmat/opencl"
cp "$BUILD/btxmat" "$PKG/btxmat/"
cp "$ROOT/opencl/btxmat.cl" "$PKG/btxmat/opencl/"
cp "$ROOT/hiveos/"* "$PKG/btxmat/"
chmod +x "$PKG/btxmat/"*.sh "$PKG/btxmat/btxmat"

mkdir -p "$PKG"
tar -C "$PKG" -czf "$PKG/btxmat-${VERSION}.tar.gz" btxmat
sha256sum "$PKG/btxmat-${VERSION}.tar.gz"

echo "Built $PKG/btxmat-${VERSION}.tar.gz"
