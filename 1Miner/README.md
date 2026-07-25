# 1Miner

Open-Source **Saseul**-Miner **nur für AMD-GPUs** (OpenCL).
Kein NVIDIA, kein CPU-Mining.
HiveOS-Custom-Miner-Paket inklusive.

## Warum 1Miner?

| | saseul-miner (saseulpool) | hasher (RabbitMiner) | **1Miner** |
|--|--|--|--|
| Protokoll | NATS | login/getjob (TLS/TCP) | **beides** |
| AMD | nein (nur CUDA) | OpenCL | **OpenCL only** |
| NVIDIA | CUDA | CUDA/OpenCL | **nein** |
| CPU | — | optional | **nein** |
| Quellcode | geschlossen | geschlossen | **offen (MIT)** |
| HiveOS | ja | ja | **ja** |

## Build

```bash
sudo apt-get install -y build-essential cmake libssl-dev ocl-icd-opencl-dev
cmake -S 1Miner -B 1Miner/build -DCMAKE_BUILD_TYPE=Release
cmake --build 1Miner/build -j$(nproc)
```

HiveOS-Archiv:

```bash
1Miner/scripts/package-hive.sh
# → 1Miner/releases/1miner-hive-1.0.21.tar.gz
```

## Autotune — hasher-Style (Ziel ~3.4 GH/s)

hasher 5.2 tuned mit **`threads × blocks × batch × ocl_variant`** (Default threads=256, blocks=1024).
1Miner v1.0.21 spiegelt das:

- Kernel **`mine_classic_hi32_c_u32`**: `__constant` Blob, uint-Loop, HEX-LUT, WG-Hint 256 (wie hasher `tail14_hi32_r5_lut_u32`)
- Autotune-Grid: threads 64–512, CU-Mult 2…64, abs. blocks 256–4096, Batches 16M–1G
- Maj/Ch wie hasher (`bitselect`), Found-Poll alle 256
- Cache: `/tmp/1miner-autotune-1.0.21.json`

Extra config: `--autotune-force`

## HiveOS Flight Sheet

- **Installation URL:**  
  `https://raw.githubusercontent.com/Crypto-EU/Sample/cursor/1miner-amd-hiveos-3705/1Miner/releases/1miner-hive-1.0.21.tar.gz`  
- **Extra config:** `--autotune-force`

## Hinweis

1Miner v1.0.21: fix false OpenCL shares (c_u32 now scalar-arg u32 loop; CPU-only accept). hasher-style tune ~3.4 GH/s.
