# EXCC AMD lolMiner fuer HiveOS

HiveOS Custom Miner fuer ExchangeCoin (EXCC) mit Equihash 144/5 auf AMD-Grafikkarten.

Dieses Repository baut keinen eigenen GPU-Miner von Grund auf. Es liefert eine HiveOS-Integration, die den offiziellen Linux-Release von [lolMiner](https://github.com/Lolliedieb/lolMiner-releases) installiert und mit diesen sicheren Defaults startet:

- Coin: `EXCC`
- Algorithmus: `Equihash 144/5` / `EQUI144_5`
- GPU-Auswahl: `--devices AMD`
- High-Hashrate-Default: `--keepfree 0`
- Weniger Wrapper-Overhead: direkter `exec` von lolMiner, lolMiner-eigene Logdatei statt Shell-`tee`
- Standard-Pool: `65.109.139.153:3052`

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
| Pool URL | `65.109.139.153:3052` |
| Pass | `x` |
| Extra config arguments | optional, z.B. `--devices 0,1` |

Der Miner nutzt AMD-GPUs per Default. Wenn du einzelne Karten auswaehlen willst, ueberschreibe die Auswahl in den Extra-Argumenten, z.B. `--devices 0,2`.

## Performance-Optimierung

lolMiner ist ein geschlossener, bereits optimierter GPU-Miner. Dieser Wrapper kann den internen Equihash-Kernel nicht schneller machen als lolMiner selbst. Die Optimierung in diesem Repository zielt deshalb auf die reale Rig-Hashrate:

- `--keepfree 0` statt lolMiner-Default `5`, damit der Miner auf Mining-Rigs weniger VRAM ungenutzt laesst.
- `--nocolor on` und `--compactaccept on`, damit Logs schlanker bleiben und Stats stabiler geparst werden.
- Direkter Prozessstart per `exec`, keine Shell-Pipe ueber `tee`.
- API nur lokal per `--apihost 127.0.0.1`.
- Extra-Argumente werden am Ende angehaengt und koennen Defaults ueberschreiben.

Wenn ein Rig mit `--keepfree 0` instabil wird, setze z.B. `EXCC_KEEPFREE=8` oder `EXCC_KEEPFREE=16`.

### Keepfree-Tuning auf dem Rig

Nach der Installation:

```bash
cd /hive/miners/custom/excc-amd-lolminer
sudo EXCC_TUNE_SECONDS=90 ./bin/tune_keepfree.sh
```

Optional eigene Kandidaten testen:

```bash
sudo EXCC_TUNE_KEEPFREE_VALUES="0 4 8 16 32 -8 -16" ./bin/tune_keepfree.sh
```

Das Skript gibt den besten gefundenen Wert aus, z.B.:

```text
Best candidate: EXCC_KEEPFREE=0 at 123.456 h/s
```

Diesen Wert dann als HiveOS Custom-Miner-Umgebungsvariable setzen oder als Extra-Argument eintragen:

```text
--keepfree 0
```

## HiveOS Paket bauen

Lokal oder auf einem Build-System:

```bash
./build-package.sh
```

Das erzeugt:

```text
dist/excc-amd-lolminer-1.0.0.tar.gz
```

Dieses Archiv kann auf HiveOS mit dem Custom-Miner-Mechanismus installiert werden, weil es ein Top-Level-Verzeichnis `excc-amd-lolminer/` enthaelt.

## Konfiguration

`h-config.sh` liest die HiveOS Custom-Miner-Variablen:

- `CUSTOM_URL`: Pool, z.B. `65.109.139.153:3052` oder `stratum+ssl://host:port`
- `CUSTOM_TEMPLATE`: Wallet/Worker-Template, z.B. `%WAL%.%WORKER_NAME%`
- `CUSTOM_PASS`: Pool-Passwort, Default `x`
- `CUSTOM_USER_CONFIG`: zusaetzliche lolMiner-Argumente

Optionale Umgebungsvariablen:

- `EXCC_DEVICES`: Default `AMD`
- `EXCC_API_PORT`: Default `8020`
- `EXCC_API_HOST`: Default `127.0.0.1`
- `EXCC_KEEPFREE`: Default `0`
- `EXCC_SHORTSTATS`: Default `30`
- `EXCC_LONGSTATS`: Default `120`
- `LOLMINER_VERSION`: Default `1.98a`; setze `latest`, um beim Rig-Setup den neuesten GitHub-Release zu laden
- `LOLMINER_SHA256`: optionaler SHA256-Check fuer das heruntergeladene lolMiner-Archiv

## Dateien

```text
miners/excc-amd-lolminer/
  h-manifest.conf       HiveOS Manifest
  h-config.sh           erzeugt die lolMiner CLI-Konfiguration
  h-run.sh              installiert/startet lolMiner
  h-stats.sh            liefert einfache HiveOS Stats aus dem Miner-Log
  bin/install_lolminer.sh
  bin/tune_keepfree.sh
install.sh              installiert den Custom Miner auf einem HiveOS-Rig
build-package.sh        baut ein HiveOS-kompatibles tar.gz
```

## Hinweis zur Veroeffentlichung

Der Download-Link funktioniert ohne Authentifizierung nur, wenn das GitHub-Repository oeffentlich ist oder die Dateien in ein oeffentliches Repository gemerged werden.
