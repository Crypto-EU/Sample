#!/usr/bin/env bash
# Build HiveOS custom-miner archive: 1miner-hive-<version>.tar.gz
# HiveOS derives the unpack dir from the archive name, so the tarball
# MUST contain top-level folder "1miner-hive/" (not "1miner/").
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ver=$(grep '^CUSTOM_VERSION=' "$root/hiveos/h-manifest.conf" | cut -d= -f2)
pkg_name="1miner-hive"
build="$root/build"
stage="$root/dist/${pkg_name}"
rm -rf "$root/dist"
mkdir -p "$stage" "$build"

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc)"

cp -a "$build/1miner" "$stage/1miner"
cp -a "$root/src/opencl_kernels.cl" "$stage/opencl_kernels.cl"
cp -a "$root/hiveos/"* "$stage/"
chmod +x "$stage/1miner" "$stage"/h-*.sh

outdir="$root/dist"
mkdir -p "$root/releases"
tar -C "$outdir" -czf "$outdir/${pkg_name}-${ver}.tar.gz" "$pkg_name"
cp -f "$outdir/${pkg_name}-${ver}.tar.gz" "$root/releases/${pkg_name}-${ver}.tar.gz"
echo "Wrote $outdir/${pkg_name}-${ver}.tar.gz"
tar -tzf "$outdir/${pkg_name}-${ver}.tar.gz" | head -20
ls -lah "$outdir/${pkg_name}-${ver}.tar.gz" "$root/releases/${pkg_name}-${ver}.tar.gz"
