# 1Miner

Open-Source **Saseul**-Miner für **AMD (OpenCL)** und **CPU**, mit optionalem NVIDIA-OpenCL.
HiveOS-Custom-Miner-Paket inklusive.

## Warum 1Miner?

| | saseul-miner (saseulpool) | hasher (RabbitMiner) | **1Miner** |
|--|--|--|--|
| Protokoll | NATS | login/getjob (TLS/TCP) | **beides** |
| AMD | nein (nur CUDA) | OpenCL | **OpenCL** |
| NVIDIA | CUDA | CUDA/OpenCL | OpenCL-ICD / CPU |
| Quellcode | geschlossen | geschlossen | **offen (MIT)** |
| HiveOS | ja | ja | **ja** |

Architektur angelehnt an hasher (OpenCL-Backend, HiveOS-Wrapper, GPU-Status) und saseul-miner (NATS, Stats-JSON), aber als **Clean-Room**-Implementierung ohne Übernahme proprietärer Kernel.

## Build

```bash
sudo apt-get install -y build-essential cmake libssl-dev ocl-icd-opencl-dev
cmake -S 1Miner -B 1Miner/build -DCMAKE_BUILD_TYPE=Release
cmake --build 1Miner/build -j$(nproc)
```

HiveOS-Archiv bauen:

```bash
1Miner/scripts/package-hive.sh
# → 1Miner/dist/1miner-hive-1.0.0.tar.gz
```

## Beispiele

RabbitMiner (SSL-Standardport 1901 — TLS automatisch):

```bash
./1miner --pool nl.rabbitminer.cc:1901 --wallet WALLET.worker --amd-ocl --use-cpu
```

TCP Sync (Port 1931 — **ohne** TLS, Fix für den bekannten `TLS connect failed`-Fehler):

```bash
./1miner --pool nl.rabbitminer.cc:1931 --wallet WALLET.worker --amd-ocl
```

saseulpool.com (NATS):

```bash
./1miner --nats nats://nats.saseulpool.com:4222 --wallet WALLET --id rig1 --use-cpu
```

## HiveOS Flight Sheet

- **Miner:** Custom  
- **Name:** `1miner`  
- **Installation URL:** Release-URL von `1miner-hive-1.0.0.tar.gz`  
- **Pool URL:** z. B. `nl.rabbitminer.cc:1901` oder `nats://nats.saseulpool.com:4222`  
- **Wallet template:** `%WAL%.%WORKER_NAME%`  
- **Extra config:** `--amd-ocl --use-cpu` (oder `--nonce-mode latehex`)

Port-Zuordnung RabbitMiner/Saseul:

| Port | Modus |
|------|--------|
| 1901 | SSL Standard |
| 1921 | SSL Sync |
| 1911 | TCP Standard |
| 1931 | TCP Sync (kein TLS!) |

## PoW

Klassisch: `SHA256(ASCII(previous_blockhash[78] + digest[64] + nonce[16]))`, Target = `2^256 / share_difficulty`.
Latehex: Nonce 40 Hex (`24` Prefix + `16` Counter).

## Hinweis

1Miner ist eine unabhängige Open-Source-Neuimplementierung. Hashrate und Share-Akzeptanz können von den Closed-Source-Minern abweichen, solange Kernel weiter optimiert werden. Beiträge willkommen.
