#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REQUESTED_MINER="${EXCC_MINER_NAME:-}"
DIST_DIR="${ROOT_DIR}/dist"

mkdir -p "$DIST_DIR"

package_miner() {
  local miner_name="$1"
  local miner_dir="${ROOT_DIR}/miners/${miner_name}"

  if [[ ! -d "$miner_dir" ]]; then
    echo "Miner directory not found: $miner_dir" >&2
    exit 1
  fi

  # shellcheck source=/dev/null
  . "${miner_dir}/h-manifest.conf"
  local version="${CUSTOM_VERSION:-1.0.0}"
  local package="${DIST_DIR}/${miner_name}-${version}.tar.gz"

  chmod +x "${miner_dir}"/h-*.sh "${miner_dir}"/bin/*.sh

  tar \
    --exclude="${miner_name}/lolminer" \
    --exclude="${miner_name}/build" \
    --exclude="${miner_name}/excc-native-solver" \
    --exclude="${miner_name}/*.log" \
    -C "${ROOT_DIR}/miners" \
    -czf "$package" \
    "$miner_name"

  echo "Created $package"
}

if [[ -n "$REQUESTED_MINER" ]]; then
  package_miner "$REQUESTED_MINER"
else
  for manifest in "${ROOT_DIR}"/miners/*/h-manifest.conf; do
    miner_name="$(basename "$(dirname "$manifest")")"
    package_miner "$miner_name"
  done
fi
