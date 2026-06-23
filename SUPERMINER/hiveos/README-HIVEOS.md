# HiveOS — SUPERMINER Pearlhash

## Schnellstart

### 1. Flight Sheet importieren

Lade die JSON-Datei herunter und importiere sie in HiveOS:

**Download:**
https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.0.0/superminer-pearlhash-flightsheet.json

Im Hive-Dashboard: **Flight Sheets** → **Import** → JSON einfügen oder Datei hochladen.

> Nach dem Import: Wallet (`wal_id`) im Flight Sheet deiner Pearl-Wallet (`prl1…`) zuweisen.

### 2. Miner-Paket (automatisch)

HiveOS lädt den Miner von dieser URL:

https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.0.0/superminer-hiveos-1.0.0.tar.gz

Manuell per Shell:

```bash
/hive/miners/custom/custom-get https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.0.0/superminer-hiveos-1.0.0.tar.gz -f
```

### 3. RX 6800 XT / RDNA2

Optional in **Extra config arguments** (bereits in der JSON für eine GPU):

```
--devices 0 --batch 8
```

Bei ROCm-Erkennungsproblemen Rig-Variable setzen:

```
HSA_OVERRIDE_GFX_VERSION=10.3.0
```

## Flight Sheet JSON (Kopieren)

Siehe `superminer-pearlhash-flightsheet.json` in diesem Ordner.
