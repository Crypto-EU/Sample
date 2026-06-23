# SUPERMINER

**SUPERMINER** is an open-source, **0% dev fee** GPU miner for **Pearl (PEARL / Pearlhash)** on **AMD** graphics cards, with first-class **HiveOS** integration.

It implements Pearl's Proof-of-Useful-Work (low-rank-noised integer GEMM + BLAKE3 transcript hashing) on **ROCm/HIP**, targeting:

| Architecture | Example GPUs | LLVM target |
|---|---|---|
| RDNA2 | RX 6000 series | `gfx1030` |
| RDNA3 | RX 7000 series | `gfx1100` / `gfx1101` |
| RDNA4 | RX 9000 series | `gfx1200` |
| CDNA3 | MI300X | `gfx942` |

## Why SUPERMINER?

SUPERMINER was designed after studying existing Pearl miners (**WildRig Multi**, **ARC-miner**, **alpha-miner**, **lpminer**) and applying their best techniques in one AMD-focused package:

| Optimization | Source inspiration | SUPERMINER |
|---|---|---|
| Hand-tuned MFMA GEMM + split PoW kernel | ARC-miner (MI300X) | ✅ `k_tgemm_pow_pp` fast path (K=2048) |
| Parallel Merkle `tensor_hash` | ARC-miner ROCm | ✅ GPU leaf + reduce pipeline |
| Ping-pong streams / batch iter | ARC-miner, lpminer | ✅ Configurable batch size |
| Per-arch matrix shapes (Infinity Cache) | ARC-miner, alpha-miner | ✅ Auto M/N/K per `gfx*` |
| Multi-arch fat HIP binary | alpha-miner | ✅ gfx1030–gfx1200 + gfx942 |
| `pearl/v1` connection challenge | Pearlhash / AlphaPool | ✅ Multi-thread BLAKE3 solver |
| Native HiveOS stats JSON | lpminer | ✅ `/var/run/hive-miner-superminer.stats.json` |
| 0% fee | Community miners | ✅ Always |

## Quick start (Linux + ROCm)

```bash
git clone https://github.com/Crypto-EU/Sample.git
cd Sample/SUPERMINER
./build.sh
./out/superminer --pearl-mine \
  --pool stratum+tcp://pool.pearlhash.xyz:9000 \
  --wallet prl1pYOUR_ADDRESS \
  --worker myrig
```

### Requirements

- Linux with **amdgpu** driver (`/dev/kfd`, `/dev/dri/renderD*`)
- **ROCm 6.x or 7.x** with `hipcc` (or use a HiveOS AMD image)
- AMD GPU from the table above

## HiveOS setup

1. Create a **Wallet** with coin **PEARL** and your `prl1…` address.
2. Create a **Flight Sheet** → Miner: **Custom**
3. Fill in:

| Field | Value |
|---|---|
| **Miner name** | `superminer` |
| **Installation URL** | `https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.0.0/superminer-hiveos-1.0.0.tar.gz` |
| **Pool URL** | `pool.pearlhash.xyz:9000` |
| **Wallet template** | `%WAL%` |
| **Pass** | `x` or `x;d=65536` for static difficulty |

4. Apply the flight sheet to your AMD rigs and start mining.

Example flight sheet JSON (Pearlhash pool):

```json
{"name":"superminer-pearlhash","items":[{"coin":"PEARL","miner":"custom","miner_alt":"superminer","miner_config":{"url":"pool.pearlhash.xyz:9000","algo":"pearlhash","miner":"superminer","template":"%WAL%","install_url":"https://github.com/Crypto-EU/SUPERMINER/releases/download/v1.0.0/superminer-hiveos-1.0.0.tar.gz","user_config":""}}]}
```

## CLI reference

```
superminer --pearl-mine [options]

  --pool URI           stratum+tcp://host:port
  --wallet ADDR        Pearl payout address (required)
  --worker NAME        Worker name
  --password PW        Stratum password (use x;d=N for static diff)
  --devices LIST       all or 0,1,2,...
  --pearl-m/n/k/r N    Override auto-tuned matrix shape
  --batch N            GPU batch size (default 8)
  --list-devices       Show detected AMD GPUs
```

## Build options

```bash
ARCH=fat ./build.sh          # multi-arch HIP (default)
ARCH=gfx1100 ./build.sh      # single arch
OUT=/opt/superminer ./build.sh
```

## Architecture

```
SUPERMINER/
├── native/pearl-gemm/csrc/rocm/   # HIP kernels (GEMM, BLAKE3, PoW)
├── native/pearl-mining-capi/      # Rust BLAKE3 Merkle (host proofs)
├── native/superminer-share/       # Share plain_proof builder + challenge
├── src/                           # C++ host (stratum, GPU workers)
└── hiveos/superminer/             # HiveOS integration scripts
```

## License

GPL-3.0-or-later — see [LICENSE](LICENSE). GPU kernel code is derived from [ARC-miner](https://github.com/jbman2025/ARC-miner) (GPL-3.0) with substantial AMD RDNA extensions.

## Pools

Compatible with **Pearlhash** (`pool.pearlhash.xyz:9000`), AlphaPool-style `pearl/v1` stratum, and other Pearl stratum pools.

## Support

Open an issue: https://github.com/Crypto-EU/Sample/issues
