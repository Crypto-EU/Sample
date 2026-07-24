#!/usr/bin/env bash
# Build HiveOS custom-miner archive: 1miner-hive-<version>.tar.gz
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ver=$(grep '^CUSTOM_VERSION=' "$root/hiveos/h-manifest.conf" | cut -d= -f2)
build="$root/build"
stage="$root/dist/1miner"
rm -rf "$root/dist"
mkdir -p "$stage" "$build"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc)"

cp -a "$build/1miner" "$stage/1miner"
cp -a "$root/src/opencl_kernels.cl" "$stage/opencl_kernels.cl"
cp -a "$root/hiveos/"* "$stage/"
chmod +x "$stage/1miner" "$stage"/h-*.sh

# Hive custom miner expects folder name = miner name
outdir="$root/dist"
mkdir -p "$outdir"
tar -C "$outdir" -czf "$outdir/1miner-hive-${ver}.tar.gz" 1miner
echo "Wrote $outdir/1miner-hive-${ver}.tar.gz"
ls -lah "$outdir/1miner-hive-${ver}.tar.gz"
