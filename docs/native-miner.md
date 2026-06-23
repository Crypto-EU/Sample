# Nativer EXCC Miner ohne lolMiner

`excc-native-miner` ist ein experimenteller nativer Prototyp. Er ist nicht der empfohlene Miner fuer AMD RX 5700 XT. Fuer HiveOS + AMD RX 5700 XT wird standardmaessig `excc-amd-lolminer` installiert, weil dieser Pfad AMD OpenCL-GPU-Mining nutzt und keine CPU-Solver startet.

Er verwendet:

- eigene HiveOS-Hooks
- eigene Python-Stratum-Schicht
- GPU-Solver-Backend
- aktuell CUDA mit EXCC-gominer GPU-Solver-Quelle zur Buildzeit
- keinen lolMiner-Download
- keinen lolMiner-Prozess
- keinen CPU-Fallback

Wichtig: Fuer AMD RX 5700 XT wird noch ein eigener OpenCL/HIP-Backend-Kernel
benoetigt. Der aktuelle GPU-Backend-Pfad ist CUDA.

## Installation auf HiveOS

Der normale Installer installiert `excc-amd-lolminer`. Wenn du den experimentellen nativen Prototyp trotzdem installieren willst, musst du ihn explizit angeben:

```bash
curl -fsSL https://raw.githubusercontent.com/Crypto-EU/Sample/main/install.sh | sudo env EXCC_MINER_NAME=excc-native-miner bash
```

Aktuelle Entwicklungsbranch:

```bash
curl -fsSL https://raw.githubusercontent.com/Crypto-EU/Sample/cursor/hiveos-excc-amd-miner-df98/install.sh | sudo env EXCC_BRANCH=cursor/hiveos-excc-amd-miner-df98 EXCC_MINER_NAME=excc-native-miner bash
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

## GPU Benchmark

```bash
cd /hive/miners/custom/excc-native-miner
sudo ./bin/bench_native_solver.sh
```

CUDA-Architektur fuer den Build anpassen:

```bash
sudo EXCC_CUDA_ARCH=sm_75 ./bin/bench_native_solver.sh
```

## Fehler `invalid solution`

EXCC Equihash nutzt fuer aktuelle Mainnet-Jobs den 180-Byte-Header ohne die
100-Byte-Equihash-Solution. Der native Miner baut genau diesen Header und
filtert gefundene Equihash-Loesungen lokal gegen das Pool-Difficulty-Target,
bevor `mining.submit` gesendet wird.

Wenn trotzdem Rejects auftreten, pruefe zuerst:

- ob der Worker wirklich `excc-native-miner` nutzt
- ob nach einem Update `miner restart` ausgefuehrt wurde
- ob im Log `skipped low-difficulty solution` erscheint; das ist normal und
  verhindert ungueltige Low-difficulty-Submits
- ob Pool, Wallet und Workername korrekt sind

## Erwartung

Diese Version ist ein echter GPU-Startpunkt ohne lolMiner. Sie ist jedoch noch kein optimierter AMD-OpenCL-Miner. Fuer RX 5700 XT waere der naechste grosse Schritt ein eigener OpenCL/HIP-Kernel, der die Equihash-Bucket-Runden direkt auf Navi10 ausfuehrt.

Die native Version ist deshalb vor allem:

- transparent
- auditierbar
- ohne Closed-Source-Miner
- GPU-only im normalen Startpfad
- geeignet als Basis fuer weitere AMD-GPU-Kernel-Optimierung

Sie wird auf AMD erst dann eine sinnvolle Hashrate liefern, wenn der OpenCL/HIP-Backend-Kernel implementiert ist. Auf Systemen ohne unterstuetztes GPU-Backend bricht sie ab, statt CPU zu minen.
