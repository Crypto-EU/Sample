# SUPERHERO v0.2.0 — GPU-Only BTX Miner

Standalone **GPU-only** miner for **BTX** (`btx-matmul`) on **AMD OpenCL** (HiveOS / RX 6800 XT).

## GPU-Only

- **No CPU mining** — all hashing runs on the AMD GPU via OpenCL
- Job setup (matrix A/B, clean block products) runs on GPU kernels
- Stratum/network uses minimal CPU only for I/O

## Algorithm

BTX MatMul PoW: M31 field, n=512, b=16, r=8. Per nonce: sigma → noise → transcript replay hash → target check.

## Build

Requires OpenCL headers. **HiveOS releases** use an old-glibc toolchain automatically:

```bash
cd SUPERHERO && ./scripts/build-hiveos.sh 0.2.4
```

Local dev build (needs GPU/OpenCL at runtime):

```bash
sudo apt install build-essential cmake g++ opencl-headers ocl-icd-opencl-dev
HIVEOS_BUILD=0 ./scripts/build-release.sh
```

## HiveOS (AMD RX 5700 XT / 6800 XT)

Tarball: `superhero-0.2.4.tar.gz` from [GitHub Releases](https://github.com/Crypto-EU/Sample/releases)

```
https://github.com/Crypto-EU/Sample/releases/download/superhero-v0.2.4/superhero-0.2.4.tar.gz
```

Flight sheet: miner name `superhero`, wallet template `%WAL%.%WORKER_NAME%`, extra args empty.

`HSA_OVERRIDE_GFX_VERSION=10.3.0` is set automatically for RDNA2 in `h-run.sh`.

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
