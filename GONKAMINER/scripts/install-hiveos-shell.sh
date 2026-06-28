#!/usr/bin/env bash
# GONKAMINER — manuelle Installation auf HiveOS (Shell)
# Auf der Rig als root ausführen: bash install-hiveos-shell.sh
set -euo pipefail

VERSION="${GONKAMINER_VERSION:-0.1.3}"
MINER_NAME="gonkaminer"
ARCHIVE="${MINER_NAME}-${VERSION}.tar.gz"
URL="https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v${VERSION}/${ARCHIVE}"
CUSTOM_DIR="/hive/miners/custom"
DOWNLOAD_DIR="${CUSTOM_DIR}/downloads"
TARGET="${CUSTOM_DIR}/${MINER_NAME}"

echo "=== GONKAMINER ${VERSION} — HiveOS Shell-Installation ==="
echo "Ziel: ${TARGET}"
echo ""

if [[ $EUID -ne 0 ]]; then
  echo "Bitte als root ausführen (HiveOS Shell: du bist normalerweise root)."
  exit 1
fi

mkdir -p "${DOWNLOAD_DIR}"
cd "${DOWNLOAD_DIR}"

if [[ -f "${ARCHIVE}" ]]; then
  echo "> Vorhandenes Archiv: ${DOWNLOAD_DIR}/${ARCHIVE}"
else
  echo "> Lade ${URL}"
  if command -v wget >/dev/null 2>&1; then
    wget -c --timeout=60 --tries=3 "${URL}" -O "${ARCHIVE}"
  elif command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --connect-timeout 60 -o "${ARCHIVE}" "${URL}"
  else
    echo "FEHLER: weder wget noch curl gefunden."
    exit 1
  fi
fi

if [[ ! -s "${ARCHIVE}" ]]; then
  echo "FEHLER: Archiv leer oder nicht vorhanden."
  exit 1
fi

echo "> Archivgröße: $(du -h "${ARCHIVE}" | awk '{print $1}')"

if ! tar -tzf "${ARCHIVE}" | head -1 | grep -q "^${MINER_NAME}/"; then
  echo "FEHLER: Archiv enthält keinen Ordner '${MINER_NAME}/'"
  tar -tzf "${ARCHIVE}" | head -5
  exit 1
fi

echo "> Entferne alte Installation (falls vorhanden)"
rm -rf "${TARGET}"

echo "> Entpacke nach ${CUSTOM_DIR}"
cd "${CUSTOM_DIR}"
tar -xzf "${DOWNLOAD_DIR}/${ARCHIVE}"

echo "> Python venv Abhängigkeit (python3.10-venv)"
if command -v apt-get >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq || true
  apt-get install -y --no-install-recommends python3.10-venv python3-pip python3-venv 2>/dev/null \
    || apt-get install -y --no-install-recommends python3-venv python3-pip || true
fi

find "${TARGET}" -name '*.sh' -exec chmod +x {} +
chown -R user:user "${TARGET}" 2>/dev/null || true

# HiveOS-Pfad-Fixes (wie custom-get)
for f in h-manifest.conf h-config.sh h-run.sh h-stats.sh; do
  [[ -f "${TARGET}/${f}" ]] && sed -i 's|/hive/custom|/hive/miners/custom|g' "${TARGET}/${f}"
done

echo ""
echo "=== Prüfung ==="
for f in h-manifest.conf h-config.sh h-run.sh h-stats.sh; do
  if [[ -f "${TARGET}/${f}" ]]; then
    echo "  OK  ${TARGET}/${f}"
  else
    echo "  FEHLT  ${TARGET}/${f}"
    exit 1
  fi
done

echo ""
echo "=== Installation erfolgreich ==="
echo ""
echo "Flight Sheet (Custom Miner) einstellen:"
echo "  Miner name:        ${MINER_NAME}"
echo "  Installation URL:  ${URL}"
echo "  Extra config:      HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080"
echo ""
echo "Manuell testen:"
echo "  export HSA_OVERRIDE_GFX_VERSION=10.3.0"
echo "  cd ${TARGET} && ./h-run.sh"
echo ""
echo "Log: /var/log/miner/custom/gonkaminer.log"
