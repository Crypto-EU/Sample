# SUPERHERO v0.2.0 — GPU-Only BTX Miner

Standalone **GPU-only** miner for **BTX** (`btx-matmul`) on **AMD OpenCL** (HiveOS / RX 6800 XT).

## GPU-Only

- **No CPU mining** — all hashing runs on the AMD GPU via OpenCL
- Job setup (matrix A/B, clean block products) runs on GPU kernels
- Stratum/network uses minimal CPU only for I/O

## Algorithm

BTX MatMul PoW: M31 field, n=512, b=16, r=8. Per nonce: sigma → noise → transcript replay hash → target check.

## Build

```bash
sudo apt install build-essential cmake g++ opencl-headers ocl-icd-opencl-dev
cd SUPERHERO && ./scripts/build-release.sh 0.2.0
```

## HiveOS (RX 6800 XT)

Tarball: `superhero-0.2.0.tar.gz` from [GitHub Releases](https://github.com/Crypto-EU/Sample/releases)

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0
export GPU_MAX_ALLOC_PERCENT=100
```

Flight sheet custom miner URL:
```
https://github.com/Crypto-EU/Sample/releases/download/superhero-v0.2.0/superhero-0.2.0.tar.gz
```

### Tuning

| Variable | Default | Description |
|----------|---------|-------------|
| `BATCH` | 262144 | Nonces per GPU kernel launch |
| `WORKGROUP` | 256 | OpenCL workgroup size |

Pools: `stratum.minebtx.com:3333` | `btx-eu.lproute.com:8660`

## CLI

```bash
./superhero --pool stratum.minebtx.com:3333 --wallet WALLET --worker rig1
./superhero --benchmark --batch-size 65536
```

## License

MIT. BTX reference: [btxchain/btx](https://github.com/btxchain/btx).
