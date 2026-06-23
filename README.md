# EXCC AMD Miner fuer HiveOS

HiveOS Custom Miner fuer ExchangeCoin (EXCC) mit Equihash 144/5 auf AMD-Grafikkarten, insbesondere RX 5700 XT / Navi10.

Der empfohlene und standardmaessig installierte Miner ist wieder `excc-amd-lolminer`, weil dieser Pfad auf HiveOS mit AMD OpenCL-GPUs tatsaechlich mined. Er nutzt lolMiner als GPU-Backend und startet keinen CPU-Solver.

- Coin: `EXCC`
- Algorithmus: `Equihash 144/5` / `EQUI144_5`
- Backend: lolMiner OpenCL
- GPU-Auswahl: `--devices AMD`
- RX-5700-XT-Profil: automatische Navi10-Erkennung, `HSA_ENABLE_SDMA=0`, compact stats
- CPU-Mining: nicht verwendet
- Standard-Pool: `pplns.techminehub.com:6001`

Der experimentelle `excc-native-miner` bleibt im Repository, ist aber nicht fuer AMD RX 5700 XT geeignet, solange kein eigener AMD OpenCL/HIP-Kernel vorhanden ist.

## Schnellinstallation auf HiveOS

Auf dem HiveOS-Rig:

```bash
curl -fsSL https://raw.githubusercontent.com/Crypto-EU/Sample/main/install.sh | sudo bash
```

Danach in HiveOS eine Flight Sheet mit Custom Miner anlegen:

| Feld | Wert |
| --- | --- |
| Miner | `excc-amd-lolminer` |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` |
| Pool URL | `pplns.techminehub.com:6001` |
| Pass | `x` |
| Extra config arguments | leer lassen oder z.B. `--devices AMD --keepfree 0` |

HiveOS muss die AMD OpenCL-Treiber fuer deine RX 5700 XT korrekt geladen haben. Der Miner nutzt nur GPUs, keine CPU-Mining-Threads.

## RX 5700 XT Performance-Optimierung

Der Miner setzt automatisch:

- `--coin EXCC`
- `--devices AMD`
- `--keepfree 0`
- `--nocolor on`
- `--compactaccept on`
- bei RX 5700 XT: `HSA_ENABLE_SDMA=0` und `--statsformat compact`

Keepfree-Tuning auf dem Rig:

```bash
cd /hive/miners/custom/excc-amd-lolminer
sudo ./bin/tune_rx5700xt.sh
```

Nur OC-Empfehlungen anzeigen:

```bash
./bin/tune_rx5700xt.sh --print-only
```

Details: [docs/rx5700xt-tuning.md](docs/rx5700xt-tuning.md)

## HiveOS Paket bauen

Lokal oder auf einem Build-System:

```bash
./build-package.sh
```

Das erzeugt:

```text
dist/excc-amd-lolminer-1.0.0.tar.gz
dist/excc-native-miner-0.2.0.tar.gz
```

Das AMD-Paket kann auf HiveOS mit dem Custom-Miner-Mechanismus installiert werden, weil es ein Top-Level-Verzeichnis `excc-amd-lolminer/` enthaelt.

## Konfiguration

`excc-amd-lolminer/h-config.sh` liest die HiveOS Custom-Miner-Variablen:

- `CUSTOM_URL`: Pool, z.B. `pplns.techminehub.com:6001` oder `stratum+ssl://host:port`
- `CUSTOM_TEMPLATE`: Wallet/Worker-Template, z.B. `%WAL%.%WORKER_NAME%`
- `CUSTOM_PASS`: Pool-Passwort, Default `x`
- `CUSTOM_USER_CONFIG`: zusaetzliche lolMiner-Argumente

Optionale Umgebungsvariablen:

- `EXCC_DEVICES`: Default `AMD`
- `EXCC_GPU_PROFILE`: Default `auto`; erkennt RX 5700 XT/Navi10 automatisch, alternativ `rx5700xt`
- `EXCC_API_PORT`: Default `8020`
- `EXCC_API_HOST`: Default `127.0.0.1`
- `EXCC_KEEPFREE`: Default `0`
- `EXCC_SHORTSTATS`: Default `30`, bei RX 5700 XT `15`
- `EXCC_LONGSTATS`: Default `120`, bei RX 5700 XT `60`
- `EXCC_STATSFORMAT`: Default `default`, bei RX 5700 XT `compact`
- `EXCC_HSA_ENABLE_SDMA`: bei RX 5700 XT Default `0`
- `LOLMINER_VERSION`: Default `1.98a`; setze `latest`, um beim Rig-Setup den neuesten GitHub-Release zu laden
- `EXCC_MINER_NAME`: fuer den Installer, Default `excc-amd-lolminer`

## Dateien

```text
miners/excc-amd-lolminer/
  h-manifest.conf       HiveOS Manifest
  h-config.sh           erzeugt die lolMiner CLI-Konfiguration
  h-run.sh              installiert/startet lolMiner
  h-stats.sh            liefert einfache HiveOS Stats aus dem Miner-Log
  bin/install_lolminer.sh
  bin/tune_keepfree.sh
  bin/tune_rx5700xt.sh
miners/excc-native-miner/
  h-manifest.conf
  h-config.sh
  h-run.sh
  h-stats.sh
  native/excc_solver.cpp
  bin/build_gpu_solver.sh
  bin/build_native_solver.sh
  bin/bench_native_solver.sh
  bin/excc_native_miner.py
install.sh              installiert den Custom Miner auf einem HiveOS-Rig
build-package.sh        baut ein HiveOS-kompatibles tar.gz
```

## Hinweis zur Veroeffentlichung

Der Download-Link funktioniert ohne Authentifizierung nur, wenn das GitHub-Repository oeffentlich ist oder die Dateien in ein oeffentliches Repository gemerged werden.
