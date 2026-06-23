# SUPERMINER — Anleitung (Pearlhash auf AMD)

SUPERMINER ist ein Open-Source-Miner für **Pearl / Pearlhash** auf **AMD-GPUs** (RDNA2/3/4, MI300X) mit **HiveOS-Integration** und **0 % Dev-Fee**.

**Aktuelle Version:** `SUPERMINER-1.1.1`  
**Release:** https://github.com/Crypto-EU/Sample/releases/tag/superminer-v1.1.1

---

## Inhaltsverzeichnis

1. [Voraussetzungen](#1-voraussetzungen)
2. [HiveOS — Schritt für Schritt](#2-hiveos--schritt-für-schritt)
3. [RX 6800 XT / RDNA2](#3-rx-6800-xt--rdna2)
4. [Linux (ohne HiveOS)](#4-linux-ohne-hiveos)
5. [Einstellungen & Tuning](#5-einstellungen--tuning)
6. [Fehlerbehebung](#6-fehlerbehebung)
7. [Downloads](#7-downloads)

---

## 1. Voraussetzungen

| Komponente | Anforderung |
|---|---|
| **GPU** | AMD RX 6000/7000/9000 oder MI300X |
| **Treiber** | `amdgpu` + ROCm (HiveOS AMD-Image) |
| **VRAM** | mind. 12 GB empfohlen (RX 6800 XT: 16 GB) |
| **Wallet** | Pearl-Adresse (`prl1…`) |
| **Pool** | z. B. `pool.pearlhash.xyz:9000` |

**Wichtig:** SUPERMINER benötigt die Datei `libpearl_gemm_capi.so` im Miner-Ordner. Ab v1.0.1 ist sie im Paket enthalten. Ohne diese Bibliothek startet der Miner nicht.

---

## 2. HiveOS — Schritt für Schritt

### Schritt 1 — Wallet anlegen

1. Im [HiveOS-Dashboard](https://the.hiveos.farm) → **Wallets** → **Add wallet**
2. Coin: **PEARL** (oder Custom mit Pearl-Adresse)
3. Adresse: deine `prl1…`-Wallet eintragen
4. Speichern

### Schritt 2 — Flight Sheet importieren

**Option A — JSON importieren (empfohlen)**

1. Flight Sheet JSON herunterladen:  
   https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.1/superminer-pearlhash-flightsheet.json
2. HiveOS → **Flight Sheets** → **Import**
3. JSON einfügen oder Datei hochladen
4. Im importierten Sheet die **Wallet** (`wal_id`) deiner Pearl-Wallet zuweisen

**Option B — Manuell anlegen**

| Feld | Wert |
|---|---|
| Coin | PEARL |
| Miner | **Custom** |
| Miner name | `superminer` |
| Installation URL | `https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.1/superminer-hiveos-1.1.1.tar.gz` |
| Pool | `pool.pearlhash.xyz:9000` |
| Wallet | `%WAL%` |
| Pass | `x` oder `x;d=65536` (feste Schwierigkeit) |
| Algo | `pearlhash` |

### Schritt 3 — Extra Config (AMD RX 6000)

Im Flight Sheet unter **Extra config arguments**:

```
HSA_OVERRIDE_GFX_VERSION=10.3.0 --devices 0 --batch 32
```

| Parameter | Bedeutung |
|---|---|
| `HSA_OVERRIDE_GFX_VERSION=10.3.0` | RX 6800 XT / RDNA2 — GPU-Architektur für ROCm |
| `--devices 0` | Nur GPU 0 (bei mehreren GPUs: `0,1`) |
| `--batch 32` | Batch-Größe (höher = weniger Overhead, mehr VRAM) |

### Schritt 4 — Miner auf dem Rig installieren

HiveOS lädt das Paket beim ersten Start automatisch. Manuell per Shell:

```bash
/hive/miners/custom/custom-get \
  https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.1/superminer-hiveos-1.1.1.tar.gz \
  -f
```

### Schritt 5 — Flight Sheet zuweisen & starten

1. Rig auswählen → **Flight Sheet** zuweisen
2. Miner starten
3. Log prüfen: **Miner log** oder Shell:

```bash
tail -f /var/log/miner/custom/superminer.log
```

### Erwartetes Log (alles OK)

```
self-test: OK
SUPERMINER-1.1.1 starting on pool.pearlhash.xyz:9000 ...
device 0: arch=gfx1030 profile M=4096 N=32768 K=4096 R=256
stratum connected to pool.pearlhash.xyz:9000 (pearl_v1=1)
stratum job gen=1 id=... m=4096 n=... k=4096 r=256 sigma=...B b_seed=32B
GPU 0: sigma installed (m=4096 n=... k=4096 r=256)
```

Ab dann sollte die Hashrate im Hive-Dashboard steigen.

---

## 3. RX 6800 XT / RDNA2

Die **RX 6800 XT** (gfx1030) braucht auf HiveOS oft diese Einstellungen:

### Rig-Variable (optional, zusätzlich zur Extra Config)

HiveOS → Rig → **Settings** → **Variables**:

```
HSA_OVERRIDE_GFX_VERSION=10.3.0
```

### VRAM & Matrixgröße

| VRAM | Typische N-Größe | Hinweis |
|---|---|---|
| 16 GB (6800 XT) | N=32768 (Standard) | stabil |
| 16 GB + Tuning | N=65536 | nur wenn Pool das verlangt und genug VRAM frei |
| 20 GB+ | N=65536 | RDNA3-Profil |

Größere N über Umgebungsvariable (nur wenn genug VRAM):

```
SUPERMINER_N=65536
```

### Mehrere RX 6800 XT

Extra Config:

```
HSA_OVERRIDE_GFX_VERSION=10.3.0 --devices 0,1 --batch 32
```

---

## 4. Linux (ohne HiveOS)

### Bauen

```bash
git clone https://github.com/Crypto-EU/Sample.git
cd Sample/SUPERMINER
git checkout cursor/superminer-amd-df71   # oder main nach Merge

# ROCm 6.x/7.x muss installiert sein (hipcc im PATH)
./build.sh
```

### Starten

```bash
cd out
export LD_LIBRARY_PATH=/opt/rocm/lib:.
export HSA_OVERRIDE_GFX_VERSION=10.3.0    # nur RX 6000

./superminer --self-test
./superminer --pearl-mine \
  --pool stratum+tcp://pool.pearlhash.xyz:9000 \
  --wallet prl1DEINE_ADRESSE \
  --worker meinrig \
  --devices 0 \
  --batch 32
```

---

## 5. Einstellungen & Tuning

### CLI-Parameter

```
superminer --pearl-mine [Optionen]

  --pool URI           stratum+tcp://host:port
  --wallet ADDR        Pearl-Adresse (pflicht)
  --worker NAME        Worker-Name
  --password PW        z. B. x oder x;d=65536
  --devices LIST       all oder 0,1,2,...
  --pearl-m/n/k/r N    Matrix-Größen überschreiben (0 = auto)
  --batch N            Batch-Größe (Standard: 32)
  --list-devices       GPUs anzeigen
  --self-test          GPU-Bibliotheken prüfen
  --version            Version anzeigen
```

### Auto-Profile pro GPU-Architektur

| Architektur | Beispiel | M | N | K | R |
|---|---|---|---|---|---|
| gfx1030 (RDNA2) | RX 6800 XT | 4096 | 32768 | 4096 | 256 |
| gfx11 (RDNA3) | RX 7900 XT | 4096 | 65536 | 4096 | 256 |
| gfx942 (CDNA3) | MI300X | 8192 | 65536 | 2048 | 256 |

Der Pool kann per `pearl.set_mining_params` andere Werte vorgeben — diese haben Vorrang.

### Hashrate-Hinweis

Auf Consumer-AMD (RX 6000/7000) ist **WildRig Multi** derzeit oft schneller als SUPERMINER, weil SUPERMINER portable Open-Source-Kernel nutzt. SUPERMINER eignet sich für:

- Open-Source / GPL-Stack
- 0 % Fee
- Volle Pearl/v1-Protokoll-Unterstützung
- HiveOS Custom-Miner-Workflow

---

## 6. Fehlerbehebung

### `unknown argument: HSA_OVERRIDE_GFX_VERSION=10.3.0`

**Ursache:** Alte `h-run.sh` (v1.1.1) hat Umgebungsvariablen als Miner-Argumente übergeben.

**Lösung:** Auf **v1.1.1** aktualisieren.

### Miner bricht sofort ab / `unbuffer: command not found`

**Ursache:** `unbuffer` fehlt auf HiveOS.

**Lösung:** v1.1.1 nutzt `stdbuf` oder direkten Start.

### `libpearl_gemm_capi.so not found`

**Ursache:** Altes Paket (v1.0.0) oder unvollständige Installation.

**Lösung:** Miner neu installieren (v1.1.1):

```bash
/hive/miners/custom/custom-get \
  https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.1/superminer-hiveos-1.1.1.tar.gz \
  -f
```

Prüfen:

```bash
ls -la /hive/miners/custom/superminer/libpearl_gemm_capi.so
```

### `libamdhip64.so not found` / HIP init failed

**Lösung:** ROCm-Pfad setzen (in `h-run.sh` bereits enthalten):

```bash
export LD_LIBRARY_PATH=/opt/rocm/lib:/hive/miners/custom/superminer:$LD_LIBRARY_PATH
```

HiveOS AMD-Image verwenden (nicht NVIDIA-Image).

### `waiting for pearl.set_mining_params from pool`

**Ursache:** Kein Job vom Pool — Verbindung, Wallet oder Autorisierung.

**Prüfen:**

- Wallet-Adresse korrekt (`prl1…`)?
- Pool erreichbar? (`pool.pearlhash.xyz:9000`)
- Im Log: `stratum connected`?
- Passwort: `x` oder `x;d=…`

### `pearl_capi_install_B rc=-100` / VRAM-Fehler

**Ursache:** Zu wenig VRAM oder Matrix zu groß.

**Lösung:**

- Andere GPUs schließen / `--devices` einschränken
- Batch reduzieren: `--batch 16`
- Kein `SUPERMINER_N=65536` auf 16-GB-Karten erzwingen

### Hashrate = 0 im Dashboard

1. Log: `sigma installed` vorhanden?
2. `h-stats.sh` / Stats-JSON: `/var/run/hive-miner-superminer.stats.json`
3. Miner-Prozess läuft? `pgrep -a superminer`

### RX 6800 XT wird nicht erkannt / Kernel-Fehler

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0
```

In HiveOS Extra Config oder als Rig-Variable setzen.

---

## 7. Downloads

| Datei | URL |
|---|---|
| **HiveOS-Paket (tar.gz)** | https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.1/superminer-hiveos-1.1.1.tar.gz |
| **Flight Sheet JSON** | https://github.com/Crypto-EU/Sample/releases/download/superminer-v1.1.1/superminer-pearlhash-flightsheet.json |
| **Quellcode** | https://github.com/Crypto-EU/Sample/tree/cursor/superminer-amd-df71/SUPERMINER |
| **Issues / Support** | https://github.com/Crypto-EU/Sample/issues |

### Paketinhalt (nach Installation)

```
/hive/miners/custom/superminer/
├── superminer              # Hauptprogramm
├── libpearl_gemm_capi.so   # GPU-Kernel (ROCm/HIP) — Pflicht!
├── libsuperminer_share.so
├── libpearl_mining_capi.so
├── libcuda.so.1            # HIP-Shim
├── h-run.sh                # Startskript
├── h-stats.sh              # Hashrate für HiveOS
├── h-config.sh
└── h-manifest.conf
```

---

## Kurz-Checkliste

- [ ] Pearl-Wallet (`prl1…`) in HiveOS angelegt
- [ ] Flight Sheet mit v1.1.1 Install-URL
- [ ] `HSA_OVERRIDE_GFX_VERSION=10.3.0` für RX 6800 XT
- [ ] `--devices 0 --batch 32` in Extra Config
- [ ] Miner-Log: `self-test: OK` → `stratum connected` → `sigma installed`
- [ ] `libpearl_gemm_capi.so` im Miner-Ordner vorhanden

Bei Problemen: die **letzten 40 Zeilen** aus `/var/log/miner/custom/superminer.log` sichern und als GitHub-Issue einreichen.
