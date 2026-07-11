# SUPERHERO

Standalone BTX (`btx-matmul`) miner for **AMD GPUs on HiveOS**, inspired by SRBMiner and [minebtx](https://github.com/dexbtx/minebtx) protocol research.

## Algorithm

BTX MatMul PoW (M31 field, n=512, b=16, r=8):

1. Derive `sigma` from block header (SHA-256 of version, prevhash, merkle, time, bits, nonce, dim, seeds)
2. Generate low-rank noise matrices E, F from sigma
3. Compute transcript hash of `(A+E)(B+F)` with block compression
4. Compare digest to target

SUPERHERO uses the optimized **replay path** (`PrecomputeCleanBlockProducts` + noise replay) for higher CPU hashrate.

## Build

```bash
sudo apt install build-essential cmake libopencl-dev
cd SUPERHERO
./scripts/build-release.sh 0.1.0
```

## HiveOS install

1. Flight sheet → custom miner URL:
   `https://github.com/Crypto-EU/Sample/releases/download/superhero-v0.1.0/superhero-0.1.0.tar.gz`
2. Install path: `/hive/miners/custom/superhero/`
3. Pool examples:
   - **minebtx**: `stratum.minebtx.com:3333`
   - **SRBMiner-style**: `btx-eu.lproute.com:8660`

### RX 6800 XT tuning

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0
export GPU_MAX_ALLOC_PERCENT=100
```

Optional env in `h-run.sh` or miner config:

- `THREADS` – CPU threads (default: all cores)
- `BATCH` – nonces per round (default: 500000)
- `SUPERHERO_NO_OPENCL=1` – CPU-only mode

## CLI

```bash
./superhero --pool stratum.minebtx.com:3333 --wallet YOUR_WALLET --worker rig1
./superhero --benchmark --threads 8
```

## Pools & SRBMiner notes

SRBMiner-Multi lists algorithm `btx` (1% dev fee, closed source). SUPERHERO is fully open source and speaks the extended matmul stratum used by minebtx pools. Ninja-style pools used by SRBMiner may require the same extended `mining.notify` metadata (`seed_a`, `seed_b`, `block_height`).

## License

MIT. BTX matmul algorithm reference: [btxchain/btx](https://github.com/btxchain/btx).
