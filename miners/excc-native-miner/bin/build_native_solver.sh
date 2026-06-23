#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"
BUILD_DIR="${MINER_DIR}/build"
SOLVER_BIN="${MINER_DIR}/excc-native-solver"
TROMP_COMMIT="${EXCC_TROMP_COMMIT:-fab686ed3fac49dfd22624851025f31856728517}"
TROMP_URL="${EXCC_TROMP_URL:-https://github.com/tromp/equihash/archive/${TROMP_COMMIT}.tar.gz}"

download() {
  local url="$1"
  local output="$2"

  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 20 -o "$output" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$output" "$url"
  else
    echo "Neither curl nor wget is installed; cannot download Tromp Equihash source." >&2
    return 1
  fi
}

if [[ -x "$SOLVER_BIN" ]]; then
  exit 0
fi

if ! command -v g++ >/dev/null 2>&1; then
  echo "g++ is required to build the native EXCC solver." >&2
  echo "Install build-essential on the HiveOS image or build the package on another Linux host." >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

mkdir -p "$BUILD_DIR"
archive="$tmp_dir/tromp-equihash.tar.gz"
echo "Downloading Tromp Equihash source: $TROMP_URL"
download "$TROMP_URL" "$archive"
tar -xzf "$archive" -C "$tmp_dir"
src_dir="$(printf '%s\n' "$tmp_dir"/equihash-* | sed -n '1p')"

if [[ ! -d "$src_dir/blake" || ! -f "$src_dir/equi_miner.h" ]]; then
  echo "Downloaded archive does not look like Tromp Equihash source." >&2
  exit 1
fi

# Newer GCC versions reject the old BLAKE2 header because packed structs contain
# 64-byte-aligned state arrays. The BLAKE2 parameter structs are already laid out
# without padding by their field order, so removing the pragma keeps the expected
# 64-byte parameter size while allowing naturally aligned runtime states.
python3 - "$src_dir/blake/blake2.h" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
text = text.replace("#pragma pack(push, 1)\n  typedef struct __blake2s_param", "  typedef struct __blake2s_param")
text = text.replace("  } blake2b_param;\n\n  ALIGN( 64 ) typedef struct __blake2b_state", "  } blake2b_param;\n\n  ALIGN( 64 ) typedef struct __blake2b_state")
text = text.replace("#pragma pack(pop)\n", "")
text = text.replace("ALIGN( 64 ) typedef struct __blake2s_state", "typedef struct __blake2s_state")
text = text.replace("ALIGN( 64 ) typedef struct __blake2b_state", "typedef struct __blake2b_state")
text = text.replace("ALIGN( 64 ) typedef struct __blake2sp_state", "typedef struct __blake2sp_state")
text = text.replace("ALIGN( 64 ) typedef struct __blake2bp_state", "typedef struct __blake2bp_state")
path.write_text(text)
PY

echo "Building native EXCC Equihash 144/5 solver..."
g++ \
  -O3 \
  -march=native \
  -m64 \
  -std=c++11 \
  -Wall \
  -Wno-deprecated-declarations \
  -D_POSIX_C_SOURCE=200112L \
  -DATOMIC \
  -DRESTBITS=4 \
  -DWN=144 \
  -DWK=5 \
  -DHEADERNONCELEN=180 \
  -pthread \
  -I"$src_dir" \
  "${MINER_DIR}/native/excc_solver.cpp" \
  "$src_dir/blake/blake2b.cpp" \
  -o "$SOLVER_BIN"

chmod +x "$SOLVER_BIN"
echo "Built $SOLVER_BIN"
