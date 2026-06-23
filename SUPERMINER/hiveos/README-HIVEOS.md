# HiveOS — SUPERMINER Pearlhash

Deutsche Vollanleitung: **[docs/ANLEITUNG-DE.md](../docs/ANLEITUNG-DE.md)**

## Schnellstart (v1.1.0)

### 1. Flight Sheet importieren

https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.0/superminer-pearlhash-flightsheet.json

HiveOS → **Flight Sheets** → **Import** → Wallet zuweisen.

### 2. Extra Config (RX 6800 XT)

```
HSA_OVERRIDE_GFX_VERSION=10.3.0 --devices 0 --batch 32
```

### 3. Miner-Paket

Automatisch via Flight Sheet, oder manuell:

```bash
/hive/miners/custom/custom-get \
  https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.0/superminer-hiveos-1.1.0.tar.gz \
  -f
```

### 4. Log prüfen

```bash
tail -f /var/log/miner/custom/superminer.log
```

Erwartung: `self-test: OK` → `sigma installed` → Hashrate > 0
