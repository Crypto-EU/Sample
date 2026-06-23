#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
TARGET_DIR="${MINER_DIR}/lolminer"
TARGET_BIN="${TARGET_DIR}/lolMiner"

DEFAULT_LOLMINER_VERSION="1.98a"
LOLMINER_VERSION="${LOLMINER_VERSION:-$DEFAULT_LOLMINER_VERSION}"
LOLMINER_REPO="${LOLMINER_REPO:-Lolliedieb/lolMiner-releases}"

download() {
  local url="$1"
  local output="$2"

  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 20 -o "$output" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$output" "$url"
  else
    echo "Neither curl nor wget is installed; cannot download lolMiner." >&2
    return 1
  fi
}

latest_linux_url() {
  local tmp_json="$1"
  local api_url="https://api.github.com/repos/${LOLMINER_REPO}/releases/latest"

  download "$api_url" "$tmp_json" >/dev/null
  sed -n 's/.*"browser_download_url"[[:space:]]*:[[:space:]]*"\([^"]*Lin64\.tar\.gz\)".*/\1/p' "$tmp_json" | sed -n '1p'
}

version_linux_url() {
  local version="$1"
  printf 'https://github.com/%s/releases/download/%s/lolMiner_v%s_Lin64.tar.gz\n' "$LOLMINER_REPO" "$version" "$version"
}

if [[ -x "$TARGET_BIN" ]]; then
  exit 0
fi

case "$(uname -m)" in
  x86_64|amd64)
    ;;
  *)
    echo "lolMiner Linux binary is only supported on x86_64/amd64 HiveOS systems." >&2
    exit 1
    ;;
esac

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$LOLMINER_VERSION" == "latest" ]]; then
  download_url="$(latest_linux_url "$tmp_dir/latest.json")"
  if [[ -z "$download_url" ]]; then
    echo "Could not discover latest lolMiner Linux release from GitHub." >&2
    exit 1
  fi
else
  download_url="$(version_linux_url "$LOLMINER_VERSION")"
fi

archive="$tmp_dir/lolminer.tar.gz"
echo "Downloading lolMiner from: $download_url"
download "$download_url" "$archive"

if [[ -n "${LOLMINER_SHA256:-}" ]]; then
  actual_sha256="$(sha256sum "$archive" | awk '{print $1}')"
  if [[ "$actual_sha256" != "$LOLMINER_SHA256" ]]; then
    echo "lolMiner checksum mismatch: expected $LOLMINER_SHA256, got $actual_sha256" >&2
    exit 1
  fi
fi

extract_dir="$tmp_dir/extract"
mkdir -p "$extract_dir"
tar -xzf "$archive" -C "$extract_dir"

miner_bin="$(find "$extract_dir" -type f -name lolMiner | sed -n '1p')"
if [[ -z "$miner_bin" ]]; then
  echo "Downloaded lolMiner archive did not contain a lolMiner binary." >&2
  exit 1
fi

rm -rf "$TARGET_DIR"
mkdir -p "$TARGET_DIR"
cp -a "$(dirname "$miner_bin")/." "$TARGET_DIR/"
chmod +x "$TARGET_BIN"

echo "lolMiner installed to $TARGET_DIR"
