# Nativer EXCC Miner ohne lolMiner

`excc-native-miner` ist der native Miner in diesem Repository.

Er verwendet:

- eigene HiveOS-Hooks
- eigene Python-Stratum-Schicht
- eigenen C++ Solver-Frontend-Code
- Tromp Equihash als MIT-lizenzierte Solver-Basis
- keinen lolMiner-Download
- keinen lolMiner-Prozess

## Installation auf HiveOS

```bash
curl -fsSL https://raw.githubusercontent.com/Crypto-EU/Sample/main/install.sh | sudo bash
```

Wenn du die aktuelle Entwicklungsbranch nutzt:

```bash
curl -fsSL https://raw.githubusercontent.com/Crypto-EU/Sample/cursor/hiveos-excc-amd-miner-df98/install.sh | sudo env EXCC_BRANCH=cursor/hiveos-excc-amd-miner-df98 bash
```

## Flight Sheet

| Feld | Wert |
| --- | --- |
| Miner | `Custom` |
| Miner name | `excc-native-miner` |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` |
| Pool URL | `pplns.techminehub.com:6001` |
| Pass | `x` |
| Extra config arguments | leer lassen oder native Optionen |

Beispiele fuer Extra config arguments:

```text
--threads 8
--threads 8 --range 1
```

## Benchmark

```bash
cd /hive/miners/custom/excc-native-miner
sudo ./bin/bench_native_solver.sh
```

Mehr Threads:

```bash
sudo EXCC_NATIVE_THREADS=8 ./bin/bench_native_solver.sh
```

## Erwartung

Diese Version ist ein echter nativer Startpunkt ohne lolMiner. Sie ist jedoch noch kein optimierter AMD-OpenCL-Miner. Fuer RX 5700 XT waere der naechste grosse Schritt ein eigener OpenCL-Kernel, der die Tromp-Bucket-Runden direkt auf Navi10 ausfuehrt.

Die native Version ist deshalb vor allem:

- transparent
- auditierbar
- ohne Closed-Source-Miner
- geeignet als Basis fuer weitere GPU-Kernel-Optimierung

Sie wird nicht automatisch eine wesentlich hoehere Hashrate liefern als ausgereifte Closed-Source-Miner.
