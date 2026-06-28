#!/usr/bin/env bash
# GONKAMINER — complete HiveOS shell install (one command).
set -euo pipefail

VERSION="${GONKAMINER_VERSION:-0.2.0}"
MINER_NAME="gonkaminer"
ARCHIVE="${MINER_NAME}-${VERSION}.tar.gz"
URL="https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v${VERSION}/${ARCHIVE}"
CUSTOM_DIR="/hive/miners/custom"
DOWNLOAD_DIR="${CUSTOM_DIR}/downloads"
TARGET="${CUSTOM_DIR}/${MINER_NAME}"

echo "=============================================="
echo " GONKAMINER ${VERSION} — HiveOS Installation"
echo "=============================================="

[[ $EUID -eq 0 ]] || { echo "Bitte als root ausführen."; exit 1; }

mkdir -p "${DOWNLOAD_DIR}"
cd "${DOWNLOAD_DIR}"

if [[ ! -s "${ARCHIVE}" ]]; then
  echo "> Download ${URL}"
  wget -c --timeout=120 --tries=5 "${URL}" -O "${ARCHIVE}" \
    || curl -fL --retry 5 -o "${ARCHIVE}" "${URL}"
fi

[[ -s "${ARCHIVE}" ]] || { echo "FEHLER: Download leer"; exit 1; }
tar -tzf "${ARCHIVE}" | head -1 | grep -q "^${MINER_NAME}/" || { echo "FEHLER: falsches Archiv"; exit 1; }

echo "> Entpacken"
cd "${CUSTOM_DIR}"
rm -rf "${TARGET}"
tar -xzf "${DOWNLOAD_DIR}/${ARCHIVE}"
find "${TARGET}" -name '*.sh' -exec chmod +x {} +

for f in h-manifest.conf h-config.sh h-run.sh h-stats.sh; do
  [[ -f "${TARGET}/${f}" ]] && sed -i 's|/hive/custom|/hive/miners/custom|g' "${TARGET}/${f}"
done

echo "> Bootstrap (Python + ROCm PyTorch — kann 5-15 Min dauern)"
cd "${TARGET}"
export HSA_OVERRIDE_GFX_VERSION="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"
export PYTORCH_ROCM_ARCH="${PYTORCH_ROCM_ARCH:-gfx1030}"
bash scripts/bootstrap.sh

echo ""
bash scripts/doctor.sh || true

echo ""
echo "=============================================="
echo " INSTALLATION FERTIG"
echo "=============================================="
echo ""
echo "Flight Sheet:"
echo "  Miner name:       ${MINER_NAME}"
echo "  Install URL:      ${URL}"
echo "  Extra config:     HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080"
echo ""
echo "Test:"
echo "  cd ${TARGET} && ./h-run.sh"
echo "  curl http://127.0.0.1:8080/health"
echo "  tail -f /var/log/miner/custom/gonkaminer.log"
