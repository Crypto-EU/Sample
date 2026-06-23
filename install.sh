#!/usr/bin/env bash
set -euo pipefail

MINER_NAME="${EXCC_MINER_NAME:-excc-amd-lolminer}"
DEFAULT_REPO="Crypto-EU/Sample"
DEFAULT_BRANCH="main"

REPO_SLUG="${EXCC_REPO:-${EXCC_AMD_REPO:-$DEFAULT_REPO}}"
BRANCH="${EXCC_BRANCH:-${EXCC_AMD_BRANCH:-$DEFAULT_BRANCH}}"
INSTALL_ROOT="${EXCC_INSTALL_ROOT:-${EXCC_AMD_INSTALL_ROOT:-/hive/miners/custom}}"
INSTALL_DIR="${INSTALL_ROOT}/${MINER_NAME}"

download() {
  local url="$1"
  local output="$2"

  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 20 -o "$output" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$output" "$url"
  else
    echo "Neither curl nor wget is installed; cannot download repository archive." >&2
    return 1
  fi
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
local_source="${script_dir}/miners/${MINER_NAME}"
tmp_dir=""

cleanup() {
  if [[ -n "$tmp_dir" ]]; then
    rm -rf "$tmp_dir"
  fi
}
trap cleanup EXIT

if [[ -d "$local_source" ]]; then
  source_dir="$local_source"
else
  tmp_dir="$(mktemp -d)"
  archive="$tmp_dir/repository.tar.gz"
  archive_url="https://github.com/${REPO_SLUG}/archive/refs/heads/${BRANCH}.tar.gz"

  echo "Downloading ${REPO_SLUG}@${BRANCH} from GitHub..."
  download "$archive_url" "$archive"
  tar -xzf "$archive" -C "$tmp_dir"

  source_dir=""
  for candidate in "$tmp_dir"/*/miners/"$MINER_NAME"; do
    if [[ -d "$candidate" ]]; then
      source_dir="$candidate"
      break
    fi
  done
  if [[ -z "$source_dir" ]]; then
    echo "Could not find miners/${MINER_NAME} in downloaded repository archive." >&2
    exit 1
  fi
fi

if [[ ! -d "$INSTALL_ROOT" ]]; then
  echo "HiveOS custom miner directory does not exist: $INSTALL_ROOT" >&2
  echo "Run this installer on a HiveOS rig, or set EXCC_INSTALL_ROOT for testing." >&2
  exit 1
fi

echo "Installing ${MINER_NAME} to ${INSTALL_DIR}..."
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
cp -a "$source_dir/." "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR"/h-*.sh "$INSTALL_DIR"/bin/*.sh

echo "Installed ${MINER_NAME}."
echo "Create a HiveOS custom miner flight sheet with:"
echo "  Miner name: ${MINER_NAME}"
echo "  Wallet template: %WAL%.%WORKER_NAME%"
echo "  Pool: pplns.techminehub.com:6001"
echo "  Extra args: optional; AMD GPU options, e.g. --devices AMD --keepfree 0"
