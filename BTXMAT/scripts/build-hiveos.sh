#!/usr/bin/env bash
# Build BTXMAT with old glibc (2.17) for HiveOS compatibility.
set -euo pipefail

VERSION="${1:-1.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-hiveos"
PKG="$ROOT/dist"
MAMBA_ROOT="${MAMBA_ROOT_PREFIX:-$HOME/mamba}"

if command -v micromamba >/dev/null 2>&1; then
    MAMBA=micromamba
elif [[ -x /tmp/bin/micromamba ]]; then
    MAMBA=/tmp/bin/micromamba
else
    echo "Installing micromamba..."
    curl -Ls https://micro.mamba.pm/api/micromamba/linux-64/latest \
        | tar -xvj -C /tmp bin/micromamba
    MAMBA=/tmp/bin/micromamba
    export MAMBA_ROOT_PREFIX="$MAMBA_ROOT"
    "$MAMBA" shell init -s bash -r "$MAMBA_ROOT" >/dev/null 2>&1 || true
fi

export MAMBA_ROOT_PREFIX="$MAMBA_ROOT"

if ! "$MAMBA" env list | grep -q '^hiveos '; then
    echo "Creating hiveos build environment (g++ + glibc 2.17 sysroot)..."
    "$MAMBA" create -y -n hiveos -c conda-forge \
        gxx_linux-64=12 sysroot_linux-64=2.17 cmake make opencl-headers ocl-icd
fi

echo "=== BTXMAT HiveOS-compatible build (glibc <= 2.17) ==="

"$MAMBA" run -n hiveos bash -c "
set -euo pipefail
ROOT='$ROOT'
BUILD='$BUILD'
PKG='$PKG'
VERSION='$VERSION'
rm -rf \"\$BUILD\"
cmake -S \"\$ROOT\" -B \"\$BUILD\" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=\"\${CONDA_PREFIX}/bin/x86_64-conda-linux-gnu-g++\" \
    -DCMAKE_SYSROOT=\"\$CONDA_BUILD_SYSROOT\"
cmake --build \"\$BUILD\" -j\$(nproc) --target btxmat
strip \"\$BUILD/btxmat\"
echo '=== Library requirements ==='
objdump -T \"\$BUILD/btxmat\" | grep -oE 'GLIBC_[0-9.]+|GLIBCXX_[0-9.]+' | sort -Vu || true
rm -rf \"\$PKG/btxmat\"
mkdir -p \"\$PKG/btxmat/opencl\"
cp \"\$BUILD/btxmat\" \"\$PKG/btxmat/\"
cp \"\$ROOT/opencl/btxmat.cl\" \"\$PKG/btxmat/opencl/\"
cp \"\$ROOT/hiveos/\"* \"\$PKG/btxmat/\"
chmod +x \"\$PKG/btxmat/\"*.sh \"\$PKG/btxmat/btxmat\"
tar -C \"\$PKG\" -czf \"\$PKG/btxmat-\${VERSION}.tar.gz\" btxmat
sha256sum \"\$PKG/btxmat-\${VERSION}.tar.gz\"
echo \"Built \$PKG/btxmat-\${VERSION}.tar.gz (HiveOS compatible)\"
"
