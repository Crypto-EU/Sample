#!/usr/bin/env bash
# GONKAMINER — complete HiveOS shell install (one command).
set -euo pipefail

VERSION="${GONKAMINER_VERSION:-0.2.2}"
MINER_NAME="gonkaminer"
ARCHIVE="${MINER_NAME}-${VERSION}.tar.gz"
URL="https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v${VERSION}/${ARCHIVE}"
CUSTOM_DIR="/hive/miners/custom"
DOWNLOAD_DIR="${CUSTOM_DIR}/downloads"
TARGET="${CUSTOM_DIR}/${MINER_NAME}"
ARCHIVE_PATH="${DOWNLOAD_DIR}/${ARCHIVE}"
FORCE="${GONKAMINER_FORCE:-0}"

echo "=============================================="
echo " GONKAMINER ${VERSION} — HiveOS Installation"
echo "=============================================="

[[ $EUID -eq 0 ]] || { echo "Bitte als root ausführen."; exit 1; }

validate_archive() {
  local f="$1"
  [[ -s "$f" ]] || { echo "  leer oder fehlt: $f"; return 1; }
  # Do not use 'file' — broken/missing on many HiveOS images.
  if ! tar -tzf "$f" >/dev/null 2>&1; then
    echo "  tar kann Archiv nicht lesen"
    head -c 80 "$f" 2>/dev/null | sed 's/^/    /' || true
    return 1
  fi
  if ! tar -tzf "$f" 2>/dev/null | grep -q "^${MINER_NAME}/"; then
    echo "  kein ${MINER_NAME}/ Ordner im Archiv"
    tar -tzf "$f" 2>/dev/null | head -5 | sed 's/^/    /' || true
    return 1
  fi
  return 0
}

download_archive() {
  echo "> Download ${URL}"
  rm -f "${ARCHIVE_PATH}"
  if command -v wget >/dev/null 2>&1; then
    wget --timeout=120 --tries=5 "${URL}" -O "${ARCHIVE_PATH}"
  elif command -v curl >/dev/null 2>&1; then
    curl -fL --retry 5 --connect-timeout 120 -o "${ARCHIVE_PATH}" "${URL}"
  else
    echo "FEHLER: wget oder curl benötigt"
    exit 1
  fi
}

mkdir -p "${DOWNLOAD_DIR}"

if [[ "$FORCE" == "1" ]] || [[ ! -f "${ARCHIVE_PATH}" ]] || ! validate_archive "${ARCHIVE_PATH}"; then
  [[ -f "${ARCHIVE_PATH}" ]] && echo "> Altes/kaputtes Archiv wird ersetzt"
  download_archive
fi

if ! validate_archive "${ARCHIVE_PATH}"; then
  echo ""
  echo "FEHLER: Archiv ungültig nach Download."
  echo "  Pfad: ${ARCHIVE_PATH}"
  echo "  Größe: $(ls -lh "${ARCHIVE_PATH}" 2>/dev/null | awk '{print $5}' || echo 0)"
  echo ""
  echo "Manuell testen:"
  echo "  tar -tzf ${ARCHIVE_PATH} | head"
  exit 1
fi

echo "> Archiv OK ($(du -h "${ARCHIVE_PATH}" | awk '{print $1}'))"

echo "> Entpacken nach ${CUSTOM_DIR}"
cd "${CUSTOM_DIR}"
rm -rf "${TARGET}"
tar -xzf "${ARCHIVE_PATH}"
find "${TARGET}" -name '*.sh' -exec chmod +x {} +

for f in h-manifest.conf h-config.sh h-run.sh h-stats.sh; do
  [[ -f "${TARGET}/${f}" ]] && sed -i 's|/hive/custom|/hive/miners/custom|g' "${TARGET}/${f}"
done

[[ -f "${TARGET}/scripts/bootstrap.sh" ]] || {
  echo "FEHLER: Entpacken fehlgeschlagen — ${TARGET}/scripts/bootstrap.sh fehlt"
  exit 1
}

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
