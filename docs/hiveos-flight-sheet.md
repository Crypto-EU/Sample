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
| Pool URL | `65.109.139.153:3052` |
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
--coin EXCC --devices AMD
```

Wenn ein Extra-Argument denselben lolMiner-Parameter spaeter erneut setzt, verwendet lolMiner in der Regel den zuletzt gelesenen Wert.
