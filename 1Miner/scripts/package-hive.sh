#!/usr/bin/env bash
# Build HiveOS custom-miner archive: 1miner-hive-<version>.tar.gz
# HiveOS derives the unpack dir from the archive name, so the tarball
# MUST contain top-level folder "1miner-hive/" (not "1miner/").
#
# Prefer Ubuntu 20.04 (Focal) chroot when present so the binary links
# against GLIBC 2.31 / older libstdc++ (HiveOS-compatible). Building on
# Ubuntu 24.04 host produces GLIBC_2.38 / GLIBCXX_3.4.32 and will not run.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ver=$(grep '^CUSTOM_VERSION=' "$root/hiveos/h-manifest.conf" | cut -d= -f2)
pkg_name="1miner-hive"
build="$root/build-hive"
stage="$root/dist/${pkg_name}"
rm -rf "$root/dist"
mkdir -p "$stage" "$build"

focal_root="${ONE_MINER_FOCAL_ROOT:-/tmp/focal}"
build_cmd() {
  cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$build" -j"$(nproc)"
}

if [[ -x "$focal_root/usr/bin/cmake" && -x "$focal_root/usr/bin/g++" ]]; then
  echo "Building inside Ubuntu 20.04 chroot ($focal_root) for HiveOS glibc compatibility"
  sudo mkdir -p "$focal_root/workspace"
  # Bind-mount workspace so sources/build stay on host FS.
  if ! findmnt -rno TARGET "$focal_root/workspace" >/dev/null 2>&1; then
    sudo mount --bind /workspace "$focal_root/workspace"
  fi
  sudo cp /etc/resolv.conf "$focal_root/etc/resolv.conf" 2>/dev/null || true
  sudo chroot "$focal_root" bash -lc "
    set -euo pipefail
    cd /workspace/1Miner
    rm -rf build-hive
    cmake -S . -B build-hive -DCMAKE_BUILD_TYPE=Release
    cmake --build build-hive -j\$(nproc)
  "
  # Leave mount for reuse; unmount only if ONE_MINER_UMOUNT_FOCAL=1
  if [[ "${ONE_MINER_UMOUNT_FOCAL:-0}" == "1" ]]; then
    sudo umount "$focal_root/workspace" || true
  fi
else
  echo "WARNING: Focal chroot not found at $focal_root — building on host (may be too new for HiveOS)" >&2
  build_cmd
fi

cp -a "$build/1miner" "$stage/1miner"
cp -a "$root/src/opencl_kernels.cl" "$stage/opencl_kernels.cl"
cp -a "$root/hiveos/"* "$stage/"
chmod +x "$stage/1miner" "$stage"/h-*.sh

# Sanity: refuse shipping a binary that needs GLIBC > 2.31 / very new GLIBCXX
if command -v objdump >/dev/null; then
  bad=$(objdump -T "$stage/1miner" 2>/dev/null | awk '
    /\(GLIBC_2\.(3[2-9]|[4-9][0-9])\)/ {print}
    /\(GLIBCXX_3\.4\.(3[0-9]|[4-9][0-9])\)/ {print}
  ' || true)
  if [[ -n "${bad}" ]]; then
    echo "ERROR: binary requires too-new glibc/libstdc++ for HiveOS:" >&2
    echo "$bad" >&2
    exit 1
  fi
  echo "glibc/libstdc++ symbol check OK (HiveOS-compatible)"
fi

outdir="$root/dist"
mkdir -p "$root/releases"
tar -C "$outdir" -czf "$outdir/${pkg_name}-${ver}.tar.gz" "$pkg_name"
cp -f "$outdir/${pkg_name}-${ver}.tar.gz" "$root/releases/${pkg_name}-${ver}.tar.gz"
echo "Wrote $outdir/${pkg_name}-${ver}.tar.gz"
tar -tzf "$outdir/${pkg_name}-${ver}.tar.gz" | head -20
ls -lah "$outdir/${pkg_name}-${ver}.tar.gz" "$root/releases/${pkg_name}-${ver}.tar.gz"
ldd "$stage/1miner" || true
