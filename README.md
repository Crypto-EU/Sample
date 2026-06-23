# EXCC AMD lolMiner fuer HiveOS

HiveOS Custom Miner fuer ExchangeCoin (EXCC) mit Equihash 144/5 auf AMD-Grafikkarten.

Dieses Repository baut keinen eigenen GPU-Miner von Grund auf. Es liefert eine HiveOS-Integration, die den offiziellen Linux-Release von [lolMiner](https://github.com/Lolliedieb/lolMiner-releases) installiert und mit diesen sicheren Defaults startet:

- Coin: `EXCC`
- Algorithmus: `Equihash 144/5` / `EQUI144_5`
- GPU-Auswahl: `--devices AMD`
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
install.sh              installiert den Custom Miner auf einem HiveOS-Rig
build-package.sh        baut ein HiveOS-kompatibles tar.gz
```

## Hinweis zur Veroeffentlichung

Der Download-Link funktioniert ohne Authentifizierung nur, wenn das GitHub-Repository oeffentlich ist oder die Dateien in ein oeffentliches Repository gemerged werden.
