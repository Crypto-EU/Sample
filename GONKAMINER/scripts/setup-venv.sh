#!/usr/bin/env bash
# Create or repair GONKAMINER Python venv on HiveOS (needs python3-venv / ensurepip).
set -euo pipefail

VENV="${1:?usage: setup-venv.sh /path/to/.venv}"
MINER_ROOT="$(cd "$(dirname "$VENV")" && pwd)"

venv_ok() {
  [[ -x "$VENV/bin/python" ]] && "$VENV/bin/python" -c "import pip" 2>/dev/null
}

install_venv_packages() {
  local pyver majmin pkg
  pyver="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' 2>/dev/null || echo "")"
  echo "Installing python3-venv for Python ${pyver:-unknown} ..."
  if command -v apt-get >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq || true
    if [[ -n $pyver ]]; then
      majmin="${pyver%.*}.${pyver#*.}"
      apt-get install -y --no-install-recommends "python${majmin}-venv" 2>/dev/null \
        || apt-get install -y --no-install-recommends python3-venv \
        || apt-get install -y --no-install-recommends python3.10-venv python3-venv
    else
      apt-get install -y --no-install-recommends python3-venv python3-pip \
        || apt-get install -y --no-install-recommends python3.10-venv
    fi
    return 0
  fi
  if command -v yum >/dev/null 2>&1; then
    yum install -y python3-virtualenv || return 1
    return 0
  fi
  return 1
}

if venv_ok; then
  exit 0
fi

if [[ -d $VENV ]]; then
  echo "Removing broken venv: $VENV"
  rm -rf "$VENV"
fi

echo "Creating Python venv at $VENV ..."
if ! python3 -m venv "$VENV" 2>/dev/null; then
  install_venv_packages || {
    echo "ERROR: python3-venv missing. On HiveOS shell run:"
    echo "  apt-get update && apt-get install -y python3.10-venv python3-pip"
    echo "  rm -rf $VENV"
    echo "  python3 -m venv $VENV"
    exit 1
  }
  rm -rf "$VENV"
  python3 -m venv "$VENV" || {
    echo "ERROR: venv creation failed after installing python3-venv"
    exit 1
  }
fi

"$VENV/bin/python" -m ensurepip --upgrade 2>/dev/null \
  || "$VENV/bin/python" -m pip --version >/dev/null 2>&1 \
  || {
    echo "ERROR: pip not available in venv"
    exit 1
  }

"$VENV/bin/pip" install -q --upgrade pip wheel
echo "Python venv ready: $("$VENV/bin/python" --version) at $VENV"
