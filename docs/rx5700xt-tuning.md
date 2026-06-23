# RX 5700 XT Tuning fuer EXCC / Equihash 144/5

Diese Hinweise sind speziell fuer AMD RX 5700 XT / Navi10 unter HiveOS gedacht.

## Was der Miner automatisch macht

Wenn `h-config.sh` eine RX 5700 XT bzw. Navi10-Karte erkennt, wird automatisch das Profil `rx5700xt` aktiv. Alternativ kannst du es erzwingen:

```text
EXCC_GPU_PROFILE=rx5700xt
```

Das Profil setzt:

- `--devices AMD`
- `--keepfree 0`, falls nicht ueberschrieben
- `--statsformat compact`
- kuerzere Stats-Intervalle fuer schnelleres Tuning
- `HSA_ENABLE_SDMA=0` fuer die lolMiner/OpenCL-Umgebung
- AMD-OpenCL-Variablen fuer volle GPU-Speicherallokation

## Warum keine automatische OC-Aenderung?

RX 5700 XT Karten unterscheiden sich stark nach Hersteller, Speicherchips, BIOS und Kuehlung. Automatisches Uebertakten im Miner waere riskant. HiveOS sollte Core, VDD, Memory, VDDCI, MVDD und Fan pro Karte setzen.

Der Miner optimiert deshalb die Startparameter und liefert ein Benchmark-Skript. Die OC-Werte setzt du danach in HiveOS.

## Keepfree-Benchmark

Auf dem Rig:

```bash
cd /hive/miners/custom/excc-amd-lolminer
sudo ./bin/tune_rx5700xt.sh
```

Das testet standardmaessig:

```text
0 -8 -16 -32 8 16 32
```

Eigene Werte:

```bash
sudo EXCC_TUNE_KEEPFREE_VALUES="0 -8 -16 -24 -32 8 16" ./bin/tune_rx5700xt.sh
```

Den besten Wert danach in HiveOS als Extra-Argument setzen, z.B.:

```text
--keepfree -16
```

Wenn der Miner nicht startet oder Karten haengen bleiben, gehe wieder hoeher:

```text
--keepfree 0
--keepfree 8
--keepfree 16
```

## HiveOS OC-Startwerte

### Effizienzprofil

| Feld | Startwert |
| --- | --- |
| Core Clock | `1360-1380` |
| VDD | `760-780` |
| Memory Clock | `880-900` |
| VDDCI | `800-840` |
| MVDD | `1350` |

### Performanceprofil

| Feld | Startwert |
| --- | --- |
| Core Clock | `1410-1430` |
| VDD | `790-810` |
| Memory Clock | `900-930` |
| VDDCI | `840-880` |
| MVDD | `1350-1365` |

## Tuning-Reihenfolge

1. Mit dem Effizienzprofil starten.
2. `./bin/tune_rx5700xt.sh` laufen lassen.
3. Besten `--keepfree`-Wert setzen.
4. Core Clock in 10-MHz-Schritten erhoehen.
5. Wenn stabil, Memory Clock in 5- bis 10-MHz-Schritten erhoehen.
6. Bei Rejects, GPU-Fehlern oder Hangs zuerst Memory senken.
7. Nach jeder Aenderung accepted shares und Pool-Hashrate vergleichen.

## Worauf du achten musst

- Die angezeigte Hashrate ist nicht alles. Entscheidend sind accepted shares.
- Zu aggressiver Speicher kann hohe Anzeige-Hashrate, aber schlechtere echte Pool-Leistung erzeugen.
- Equihash 144/5 zeigt in lolMiner `it/s`; nutze diesen Wert fuer OC-Vergleiche.
- BIOS-Mods koennen mehr Leistung bringen, sind aber riskant und werden von diesem Miner nicht automatisch gemacht.
