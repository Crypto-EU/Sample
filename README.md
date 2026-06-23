# EXCC Native Miner fuer HiveOS

HiveOS Custom Miner fuer ExchangeCoin (EXCC) mit Equihash 144/5.

Der empfohlene Miner in diesem Repository ist jetzt `excc-native-miner`. Er startet keinen lolMiner und laedt keinen lolMiner herunter. Stattdessen baut er einen nativen Equihash-144/5-Solver aus offenem MIT-lizenziertem Tromp-Equihash-Code und nutzt eine eigene EXCC-Stratum-Schicht.

- Coin: `EXCC`
- Algorithmus: `Equihash 144/5` / `EQUI144_5`
- Backend: eigener nativer Solver, kein lolMiner
- Stratum-Submit-Format: EXCC/gominer-kompatibel
- Standard-Pool: `65.109.139.153:3052`

Wichtig: Diese native Version ist experimentell. Sie ist offen und ohne lolMiner, aber ein performanter AMD-OpenCL-Kernel fuer Equihash 144/5 ist ein eigenstaendiges grosses Entwicklungsprojekt. Die aktuelle native Version priorisiert Korrektheit, Transparenz und Erweiterbarkeit; sie wird nicht automatisch schneller sein als ein jahrelang optimierter Closed-Source-Miner.

## Schnellinstallation auf HiveOS

Auf dem HiveOS-Rig:

```bash
curl -fsSL https://raw.githubusercontent.com/Crypto-EU/Sample/main/install.sh | sudo bash
```

Danach in HiveOS eine Flight Sheet mit Custom Miner anlegen:

| Feld | Wert |
| --- | --- |
| Miner | `excc-native-miner` |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` |
| Pool URL | `65.109.139.153:3052` |
| Pass | `x` |
| Extra config arguments | optional, z.B. `--threads 8 --range 1` |

HiveOS muss auf dem Rig `python3`, `g++`, `make`/Build-Tools und eine funktionierende Linux-Umgebung zum Kompilieren haben. Wenn `g++` fehlt, muss der Solver auf einem anderen Linux-Host gebaut oder die HiveOS-Umgebung erweitert werden.

## Native Performance-Optimierung

Der native Miner enthaelt zwei Ebenen:

1. `excc-native-solver`: nativer Equihash-144/5-Solver.
2. `excc_native_miner.py`: eigene EXCC-Stratum-Schicht.

Benchmark auf dem Rig:

```bash
cd /hive/miners/custom/excc-native-miner
sudo ./bin/bench_native_solver.sh
```

Mehr Nonces pro Testlauf:

```bash
sudo EXCC_NATIVE_BENCH_RANGE=4 ./bin/bench_native_solver.sh
```

Mehr Threads:

```bash
sudo EXCC_NATIVE_THREADS=8 ./bin/bench_native_solver.sh
```

### RX 5700 XT / Navi10

Die aktuelle native Version nutzt noch keinen AMD-OpenCL-Kernel. RX 5700 XT profitiert daher erst dann deutlich, wenn ein OpenCL-Kernel fuer Equihash 144/5 implementiert wird. Die vorhandene RX-5700-XT-Doku bleibt fuer OC/Voltage-Startwerte nuetzlich:

Details: [docs/rx5700xt-tuning.md](docs/rx5700xt-tuning.md)

Der alte `excc-amd-lolminer` Wrapper ist weiterhin im Repository vorhanden, aber er ist nicht der native Miner.

## HiveOS Paket bauen

Lokal oder auf einem Build-System:

```bash
./build-package.sh
```

Das erzeugt:

```text
dist/excc-native-miner-0.1.0.tar.gz
dist/excc-amd-lolminer-1.0.0.tar.gz
```

Dieses Archiv kann auf HiveOS mit dem Custom-Miner-Mechanismus installiert werden, weil es ein Top-Level-Verzeichnis `excc-amd-lolminer/` enthaelt.

## Konfiguration

`excc-native-miner/h-config.sh` liest die HiveOS Custom-Miner-Variablen:

- `CUSTOM_URL`: Pool, z.B. `65.109.139.153:3052` oder `stratum+ssl://host:port`
- `CUSTOM_TEMPLATE`: Wallet/Worker-Template, z.B. `%WAL%.%WORKER_NAME%`
- `CUSTOM_PASS`: Pool-Passwort, Default `x`
- `CUSTOM_USER_CONFIG`: zusaetzliche native Miner-Argumente

Optionale Umgebungsvariablen:

- `EXCC_NATIVE_THREADS`: Default `nproc`
- `EXCC_NATIVE_RANGE`: Default `1`
- `EXCC_NATIVE_SOLVER_TIMEOUT`: Default `900`
- `EXCC_TROMP_COMMIT`: pinnt den Tromp-Equihash-Commit
- `EXCC_MINER_NAME`: fuer den Installer, Default `excc-native-miner`

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
  bin/build_native_solver.sh
  bin/bench_native_solver.sh
  bin/excc_native_miner.py
install.sh              installiert den Custom Miner auf einem HiveOS-Rig
build-package.sh        baut ein HiveOS-kompatibles tar.gz
```

## Hinweis zur Veroeffentlichung

Der Download-Link funktioniert ohne Authentifizierung nur, wenn das GitHub-Repository oeffentlich ist oder die Dateien in ein oeffentliches Repository gemerged werden.
