# HiveOS Flight Sheet fuer EXCC

## Wallet

Falls EXCC in HiveOS nicht als Coin vorhanden ist:

1. Wallet anlegen.
2. Coin manuell als `ExchangeCoin` oder `EXCC` speichern.
3. EXCC-Adresse eintragen.

## Flight Sheet

| Feld | Wert |
| --- | --- |
| Coin | `ExchangeCoin` oder `EXCC` |
| Wallet | deine EXCC Wallet |
| Pool | `Configure in miner` |
| Miner | `Custom` / `excc-amd-lolminer` |

## Custom Miner Setup

| Feld | Wert |
| --- | --- |
| Miner name | `excc-amd-lolminer` |
| Installation URL | Repository-Installationsskript oder gebautes tar.gz |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` |
| Pool URL | `pplns.techminehub.com:6001` |
| Pass | `x` |
| Extra config arguments | leer lassen oder eigene lolMiner-Argumente |

Beispiele fuer Extra config arguments:

```text
--devices 0,1
--devices AMD --keepfree 128
--tls on
```

Der Wrapper setzt bereits:

```text
--coin EXCC --devices AMD --keepfree 0 --nocolor on --compactaccept on
```

Damit mined lolMiner nur auf AMD-GPUs. Es werden keine CPU-Mining-Threads gestartet; die CPU wird nur fuer normale Miner-Steuerung, Pool-Kommunikation und Logging genutzt.

Auf RX 5700 XT / Navi10 wird automatisch zusaetzlich ein spezielles Profil aktiv:

```text
EXCC_GPU_PROFILE=rx5700xt
HSA_ENABLE_SDMA=0
--statsformat compact
```

Wenn ein Extra-Argument denselben lolMiner-Parameter spaeter erneut setzt, verwendet lolMiner in der Regel den zuletzt gelesenen Wert.

## Performance

Der Miner startet lolMiner ohne zusaetzliche Shell-Pipe und nutzt lolMiner-eigene Logs. Dadurch entsteht weniger CPU-/I/O-Overhead als bei einem Wrapper, der die komplette Ausgabe ueber `tee` verarbeitet.

`--keepfree 0` ist auf dedizierten Mining-Rigs meist die schnellere Ausgangsbasis als der lolMiner-Default `5`. Wenn Karten instabil werden oder Speicherfehler zeigen, teste hoehere Werte:

```bash
cd /hive/miners/custom/excc-amd-lolminer
sudo EXCC_TUNE_KEEPFREE_VALUES="0 4 8 16 32" ./bin/tune_keepfree.sh
```

Fuer RX 5700 XT:

```bash
cd /hive/miners/custom/excc-amd-lolminer
sudo ./bin/tune_rx5700xt.sh
```

Siehe auch: [`docs/rx5700xt-tuning.md`](rx5700xt-tuning.md)

Die wichtigsten Hashrate-Optimierungen fuer AMD-GPUs bleiben Karte-spezifisch und werden in HiveOS gesetzt:

- Core Clock / Core Voltage
- Memory Clock / Memory Voltage
- Fan-Zieltemperatur
- stabile Treiber/OpenCL-Laufzeit

Nutze das Tuning-Skript nach jeder groesseren OC-Aenderung erneut, weil der beste `keepfree`-Wert vom konkreten Rig abhaengt.
