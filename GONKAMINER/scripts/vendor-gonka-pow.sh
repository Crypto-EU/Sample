#!/usr/bin/env bash
# Vendors Gonka Proof-of-Compute (PoC) Python sources from gonka-ai/gonka.
# License: see THIRD_PARTY_NOTICES.md and upstream repository.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/vendor/gonka-pow"
REF="${GONKA_VENDOR_REF:-main}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Cloning gonka-ai/gonka @ ${REF} ..."
git clone --depth 1 --branch "$REF" https://github.com/gonka-ai/gonka.git "$TMP/gonka"

rm -rf "$DEST"
mkdir -p "$DEST"
cp -a "$TMP/gonka/mlnode/packages/pow/src/pow" "$DEST/pow"
cp -a "$TMP/gonka/mlnode/packages/common/src/common" "$DEST/common"

# Minimal FastAPI entry (upstream mlnode api is larger; PoC routes only).
cat > "$DEST/app.py" << 'PY'
"""GONKAMINER — AMD-compatible Gonka PoC API (subset of product-science mlnode)."""
from contextlib import asynccontextmanager

from fastapi import FastAPI
from pow.service.manager import PowManager
from pow.service.routes import router as pow_router


@asynccontextmanager
async def lifespan(app: FastAPI):
    app.state.pow_manager = PowManager()
    yield


app = FastAPI(title="GONKAMINER PoC", version="0.1.0", lifespan=lifespan)
app.include_router(pow_router, prefix="/api/v1")


@app.get("/health")
def health():
    return {"status": "ok", "service": "gonkaminer-poc", "backend": "gonka-pow"}
PY

echo "Vendored PoC stack to $DEST"
