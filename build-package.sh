#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
MINER_NAME="excc-amd-lolminer"
MINER_DIR="${ROOT_DIR}/miners/${MINER_NAME}"
DIST_DIR="${ROOT_DIR}/dist"

if [[ ! -d "$MINER_DIR" ]]; then
  echo "Miner directory not found: $MINER_DIR" >&2
  exit 1
fi

# shellcheck source=/dev/null
. "${MINER_DIR}/h-manifest.conf"
VERSION="${CUSTOM_VERSION:-1.0.0}"
PACKAGE="${DIST_DIR}/${MINER_NAME}-${VERSION}.tar.gz"

mkdir -p "$DIST_DIR"
chmod +x "${MINER_DIR}"/h-*.sh "${MINER_DIR}"/bin/*.sh

tar \
  --exclude="${MINER_NAME}/lolminer" \
  --exclude="${MINER_NAME}/*.log" \
  -C "${ROOT_DIR}/miners" \
  -czf "$PACKAGE" \
  "$MINER_NAME"

echo "Created $PACKAGE"
