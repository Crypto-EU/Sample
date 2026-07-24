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
# → 1Miner/releases/1miner-hive-1.0.7.tar.gz
```

## Beispiele

```bash
./1miner --pool nl.rabbitminer.cc:1901 --wallet WALLET.worker
./1miner --pool nl.rabbitminer.cc:1931 --wallet WALLET.worker
./1miner --nats nats://nats.saseulpool.com:4222 --wallet WALLET --id rig1
```

## HiveOS Flight Sheet

- **Miner:** Custom  
- **Name:** `1miner-hive`  
- **Installation URL:**  
  `https://raw.githubusercontent.com/Crypto-EU/Sample/cursor/1miner-amd-hiveos-3705/1Miner/releases/1miner-hive-1.0.7.tar.gz`  
- **Pool URL:** z. B. `nl.rabbitminer.cc:1901`  
- **Wallet template:** `%WAL%.%WORKER_NAME%`  
- **Extra config:** leer lassen, oder z. B. `--device 0,1` / `--nonce-mode latehex`  
  (nicht `--use-cpu` / `--cuda` / `--nvidia-ocl`)

| Port | Modus |
|------|--------|
| 1901 | SSL Standard |
| 1921 | SSL Sync |
| 1911 | TCP Standard |
| 1931 | TCP Sync (kein TLS!) |

## Hinweis

1Miner v1.0.7 folgt dem hasher 5.2 PoW/Submit-Pfad (Header-Hash mit Timestamp, Root = sha256(prev+header_hash+nonce), Blockhash = hextime+sha256(root_hex), Submit als JSON-Array).

Ohne AMD-OpenCL-Gerät beendet 1Miner mit Fehler. NVIDIA- und CPU-Backends sind absichtlich deaktiviert.
