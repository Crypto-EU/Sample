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
# → 1Miner/releases/1miner-hive-1.0.17.tar.gz
```

## Beispiele

```bash
./1miner --pool nl.rabbitminer.cc:1901 --wallet WALLET.worker
./1miner --pool nl.rabbitminer.cc:1931 --wallet WALLET.worker
./1miner --nats nats://nats.saseulpool.com:4222 --wallet WALLET --id rig1
```

## Autotune (pro GPU)

Beim Start misst 1Miner **jede AMD-Karte parallel** auf **maximale MH/s** (raw throughput): local, unroll (1/2=ilp2/4/8), Intensitäten 1–512 + WPI, multi-chunk, null-local, Batches 64M–1G. Median aus 3 Messungen, Long-Verify 512M–1G. Cache: `/tmp/1miner-autotune-1.0.17.json`.

Navi10-Klasse liegt derzeit typisch bei **~3 GH/s/Karte**; 1.0.17 spart Round-5-RSTEP und W16–19-SSIG auf der GPU. **2.9→5 GH/s ist nicht garantiert** (~70% weniger Arbeit/Hash oder deutlich stärkere GPU nötig).

| Flag | Wirkung |
|------|---------|
| `--autotune` | an (Default) |
| `--no-autotune` | aus (Defaults / Cache) |
| `--autotune-force` | neu messen, Cache ignorieren |
| `--autotune-cache PATH` | Cache-Datei |

HiveOS Extra config zum Retune: `--autotune-force`  
(Autotune dauert länger als zuvor — dafür genauer.)

## HiveOS Flight Sheet

- **Miner:** Custom  
- **Name:** `1miner-hive`  
- **Installation URL:**  
  `https://raw.githubusercontent.com/Crypto-EU/Sample/cursor/1miner-amd-hiveos-3705/1Miner/releases/1miner-hive-1.0.17.tar.gz`  
- **Pool URL:** z. B. `nl.rabbitminer.cc:1901`  
- **Wallet template:** `%WAL%.%WORKER_NAME%`  
- **Extra config:** `--autotune-force` (einmalig nach Update)

| Port | Modus |
|------|--------|
| 1901 | SSL Standard |
| 1921 | SSL Sync |
| 1911 | TCP Standard |
| 1931 | TCP Sync (kein TLS!) |

## Hinweis

1Miner v1.0.17: round-5 closed form (A5c/E5c), host W16–19 precompute, incremental lo32 hex (u4/u8/ilp2), autotune cache v17, prefer raw MH/s.

Ohne AMD-OpenCL-Gerät beendet 1Miner mit Fehler. NVIDIA- und CPU-Backends sind absichtlich deaktiviert.
