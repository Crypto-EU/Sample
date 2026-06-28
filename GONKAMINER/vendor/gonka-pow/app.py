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
