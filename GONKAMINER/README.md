# GONKAMINER

**GONKAMINER** is an experimental **AMD GPU (ROCm) Proof-of-Compute (PoC) worker** for the [Gonka](https://gonka.ai) network, packaged for **HiveOS**.

Gonka does **not** use classic pool stratum mining. Hosts earn **GNK** by running:

1. **PoC Sprint** — short synchronized transformer inference races (voting weight)
2. **LLM inference** — serving large models between sprints (revenue)

This package implements the **PoC API worker** (`/api/v1/pow/*`) from [gonka-ai/gonka](https://github.com/gonka-ai/gonka) on **AMD ROCm + PyTorch**. You still need a **Gonka Network Node + API node** (official Docker stack) on a Linux server; HiveOS rigs run the ML PoC worker only.

## Quick links

| Resource | URL |
|----------|-----|
| Gonka website | https://gonka.ai |
| Official repo | https://github.com/gonka-ai/gonka |
| Host quickstart | https://gonka.ai/docs/host/quickstart/ |
| PoC design doc | https://github.com/gonka-ai/gonka/blob/main/docs/gonka_poc.md |
| Algorithm write-up | [docs/ALGORITHM.md](docs/ALGORITHM.md) |
| Research notes | [docs/RESEARCH.md](docs/RESEARCH.md) |
| German HiveOS guide | [docs/ANLEITUNG-DE.md](docs/ANLEITUNG-DE.md) |
| **Manual HiveOS shell install (DE)** | [docs/INSTALL-HIVEOS-SHELL-DE.md](docs/INSTALL-HIVEOS-SHELL-DE.md) |

## Hardware (realistic)

| Role | Official Gonka | GONKAMINER (AMD) |
|------|----------------|------------------|
| PoC Sprint (PARAMS_V1) | NVIDIA, ≥10 GB VRAM | RX 6800 XT / 7900 XTX (ROCm PyTorch) |
| PoC Sprint (PARAMS_V2) | ≥38 GB VRAM group | Not supported on consumer AMD |
| Inference (Qwen3-235B, Kimi K2.6, …) | H100/H200/B200 clusters | **Not supported** — NVIDIA only |

## HiveOS install

1. Import flight sheet: `hiveos/gonkaminer-gonka-flightsheet.json`
2. Set install URL to release tarball (after publish)
3. Extra config example: `HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080`
4. Register the rig IP + port `8080` on your Gonka API node (`node-config.json`)

## Build from source

```bash
cd GONKAMINER
./scripts/vendor-gonka-pow.sh   # refresh upstream PoC code
./scripts/package-hiveos.sh 0.1.3
```

### v0.2.0 (complete rewrite — use this)

- **No venv required** — `pip install --target pydeps/` avoids ensurepip/python3-venv errors
- `bootstrap.sh` + `doctor.sh` — one-time setup and diagnostics
- Flight sheet extra config actually applied (`conf.conf` parsing fixed)
- Auto-detect ROCm PyTorch wheel version

```bash
wget -q https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.0/install-hiveos-shell.sh -O install.sh && bash install.sh
```

### v0.1.3 (python3-venv / ensurepip fix)

- Auto-install `python3.10-venv` and recreate broken `.venv` via `setup-venv.sh`

HiveOS archive **must** be named `gonkaminer-VERSION.tar.gz` (not `gonkaminer-hiveos-…`) so `custom-get` detects miner name `gonkaminer`.

### v0.1.2 (HiveOS download fix)

- Renamed release tarball to `gonkaminer-0.1.2.tar.gz` — fixes HiveOS `custom-get` miner name mismatch

### v0.1.1 (RX 6800 XT / HiveOS fixes)

- ROCm PyTorch auto-install (replaces wrong CUDA `pip install torch`)
- VRAM gate lowered to 10 GB for PARAMS_V1 / default Params (16 GB cards accepted)
- Single-GPU fallback when accelerate dispatch fails
- PoC init errors no longer kill the worker process (`os._exit`)
- Preflight checks before uvicorn start

## License

- GONKAMINER wrapper scripts: MIT
- Vendored Gonka PoC code: upstream license — see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

**Disclaimer:** Community project, not affiliated with Gonka AI. Use at your own risk.
