# BTXMAT v1.0.0 — AMD GPU Miner for BTX (btx-matmul)

Standalone **GPU-only** miner for **BTX** (`btx-matmul`) on **AMD OpenCL** (HiveOS).

Optimized for maximum hashrate on AMD RDNA cards (RX 5700 XT, RX 6800 XT, etc.) with global scratch buffers and rolling SHA256 to avoid GPU memory faults on HSA.

## Features

- AMD OpenCL optimized (`-cl-mad-enable`, READ_WRITE matrix buffers)
- Global per-thread scratch buffer (384 words) — stable on RX 5700 XT
- Non-blocking stratum loop with continuous GPU mining
- HiveOS custom miner package with auto `HSA_OVERRIDE_GFX_VERSION`

## Algorithm

BTX MatMul PoW: M31 field, n=512, b=16, r=8. Per nonce: sigma → noise → transcript replay hash → target check.

## Build

HiveOS releases use an old-glibc toolchain automatically:

```bash
cd BTXMAT && ./scripts/build-hiveos.sh 1.0.0
```

Local dev build:

```bash
sudo apt install build-essential cmake g++ opencl-headers ocl-icd-opencl-dev
HIVEOS_BUILD=0 ./scripts/build-release.sh 1.0.0
```

## HiveOS Setup

Download tarball from [GitHub Releases](https://github.com/Crypto-EU/Sample/releases):

```
https://github.com/Crypto-EU/Sample/releases/download/btxmat-v1.0.0/btxmat-1.0.0.tar.gz
```

### Flight Sheet

| Setting | Value |
|---------|-------|
| Miner | Custom → `btxmat` |
| URL | `stratum+tcp://stratum.minebtx.com:3333` |
| Wallet template | `%WAL%.%WORKER_NAME%` |
| Password | `x` |

### AMD GPU Override (if auto-detect fails)

| GPU | `HSA_OVERRIDE_GFX_VERSION` |
|-----|------------------------------|
| RX 5700 XT | `10.1.0` |
| RX 6800/6900 XT | `10.3.0` |
| RX 7900 XT | `11.0.0` |

Set in flight sheet **Extra config** or miner env vars.

### Tuning

| Variable | Default | Description |
|----------|---------|-------------|
| `BATCH` | 262144 | Nonces per GPU kernel launch |
| `WORKGROUP` | 256 | OpenCL workgroup size |

## CLI

```bash
./btxmat --pool stratum.minebtx.com:3333 --wallet WALLET --worker rig1
./btxmat --benchmark --batch-size 262144
./btxmat --list-gpus
```

## Pool Compatibility

| Pool | Protocol | BTXMAT |
|------|----------|--------|
| `stratum.minebtx.com:3333` | BTX stratum | ✅ Recommended |
| Ninja/lproute pools | SRBMiner ninja stratum | ❌ Use SRBMiner |

## License

MIT. BTX reference: [btxchain/btx](https://github.com/btxchain/btx).
