#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
SOLVER_BIN="${MINER_DIR}/excc-gpu-solver"
GOMINER_COMMIT="${EXCC_GOMINER_COMMIT:-d06cdec4289435067009b424868200499c9c0ce2}"
GOMINER_URL="${EXCC_GOMINER_URL:-https://github.com/EXCCoin/gominer/archive/${GOMINER_COMMIT}.tar.gz}"
CUDA_ARCH="${EXCC_CUDA_ARCH:-sm_61}"

download() {
  local url="$1"
  local output="$2"

  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 20 -o "$output" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$output" "$url"
  else
    echo "Neither curl nor wget is installed; cannot download EXCC GPU source." >&2
    return 1
  fi
}

if [[ -x "$SOLVER_BIN" ]]; then
  exit 0
fi

if ! command -v nvcc >/dev/null 2>&1; then
  echo "GPU backend requires CUDA nvcc, but nvcc was not found." >&2
  echo "This miner is GPU-only now and will not fall back to CPU mining." >&2
  echo "For AMD RX 5700 XT, a dedicated OpenCL/HIP Equihash 144/5 backend is still required." >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

archive="$tmp_dir/gominer.tar.gz"
echo "Downloading EXCC gominer GPU solver source: $GOMINER_URL"
download "$GOMINER_URL" "$archive"
tar -xzf "$archive" -C "$tmp_dir"
src_dir="$(printf '%s\n' "$tmp_dir"/gominer-* | sed -n '1p')"
cuda_dir="$src_dir/eqcuda1445"

if [[ ! -d "$cuda_dir" || ! -f "$cuda_dir/solver.cu" ]]; then
  echo "Downloaded archive does not contain eqcuda1445 GPU solver source." >&2
  exit 1
fi

cat > "$tmp_dir/excc_cuda_solver_cli.cpp" <<'CPP'
#include "eqcuda1445.cuh"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

static uint32_t g_nonce = 0;

static int hex_to_byte(const char *x) {
  int b = 0;
  for (int i = 0; i < 2; i++) {
    unsigned char c = (unsigned char)tolower(x[i]);
    if (!isxdigit(c)) {
      return -1;
    }
    b = (b << 4) | (c - (c >= '0' && c <= '9' ? '0' : ('a' - 10)));
  }
  return b;
}

static bool decode_hex(const char *hex, std::vector<unsigned char> &out) {
  const size_t hex_len = strlen(hex);
  if (hex_len == 0 || (hex_len & 1)) {
    return false;
  }

  out.resize(hex_len / 2);
  for (size_t i = 0; i < out.size(); i++) {
    const int b = hex_to_byte(hex + 2 * i);
    if (b < 0) {
      return false;
    }
    out[i] = (unsigned char)b;
  }

  return true;
}

static void on_solution(const cproof solution) {
  printf("solution %02x%02x%02x%02x ",
         (unsigned)(g_nonce & 0xff),
         (unsigned)((g_nonce >> 8) & 0xff),
         (unsigned)((g_nonce >> 16) & 0xff),
         (unsigned)((g_nonce >> 24) & 0xff));
  for (uint32_t i = 0; i < COMPRESSED_SOL_SIZE; i++) {
    printf("%02x", solution[i]);
  }
  printf("\n");
  fflush(stdout);
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s -x HEX_HEADER -n NONCE -r RANGE [-t IGNORED]\n", argv0);
}

int main(int argc, char **argv) {
  const char *hex_header = nullptr;
  uint32_t nonce = 0;
  uint32_t range = 1;
  int c;

  while ((c = getopt(argc, argv, "x:n:r:t:")) != -1) {
    switch (c) {
      case 'x':
        hex_header = optarg;
        break;
      case 'n':
        nonce = (uint32_t)strtoul(optarg, nullptr, 0);
        break;
      case 'r':
        range = (uint32_t)strtoul(optarg, nullptr, 0);
        break;
      case 't':
        break;
      default:
        usage(argv[0]);
        return 2;
    }
  }

  if (!hex_header || range < 1) {
    usage(argv[0]);
    return 2;
  }

  std::vector<unsigned char> header;
  if (!decode_hex(hex_header, header)) {
    fprintf(stderr, "Invalid hex header.\n");
    return 2;
  }

  if (header.size() != 180) {
    fprintf(stderr, "EXCC Equihash GPU header must be 180 bytes, got %zu.\n", header.size());
    return 2;
  }

  for (uint32_t r = 0; r < range; r++) {
    g_nonce = nonce + r;
    const int rc = equihash_solve((const char *)header.data(), (u32)header.size(), g_nonce, on_solution);
    if (rc != 0) {
      return rc;
    }
  }

  return 0;
}
CPP

echo "Building CUDA EXCC Equihash 144/5 GPU solver for ${CUDA_ARCH}..."
nvcc \
  -O3 \
  -std=c++14 \
  -arch="${CUDA_ARCH}" \
  -I"$cuda_dir" \
  "$tmp_dir/excc_cuda_solver_cli.cpp" \
  "$cuda_dir/solver.cu" \
  "$cuda_dir/blake/blake2b.cpp" \
  -o "$SOLVER_BIN"

chmod +x "$SOLVER_BIN"
echo "Built $SOLVER_BIN"
echo "Note: this GPU backend is built from EXCC gominer GPLv3 CUDA solver source."
