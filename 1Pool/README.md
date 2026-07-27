# 1Pool

Eigener **SASEUL (SL)** Mining Pool — kompatibel mit **1Miner**, hasher und dem RabbitMiner-Protokoll (`login` / `getjob` / `submit`).

## Was ist 1Pool?

| | RabbitMiner | **1Pool** |
|--|--|--|
| Protokoll | login / getjob / submit | **gleich** |
| Betrieb | fremder Pool | **dein Server** |
| Modi | — | **proxy** (Upstream) + **demo** (lokal) |
| Share-Check | ja | **SASEUL classic PoW** |
| Dashboard | ja | **http://HOST:8080** |
| Miner | 1Miner, hasher, … | **gleiche Clients** |

Im **Proxy-Modus** holt 1Pool Jobs von RabbitMiner (oder einem anderen 1Pool), prüft Shares lokal und leitet gültige Shares an den Upstream weiter. So betreibst du einen eigenen Stratum-Endpunkt für deine Rigs (HiveOS → dein Host:1911).

## Schnellstart (Demo)

Lokal ohne Upstream — nur Protokoll-Test:

```bash
cd 1Pool
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python -m onepool --config config.demo.yaml
```

Dann mit 1Miner:

```bash
./1miner --pool 127.0.0.1:1911 --wallet DEINE_WALLET.rig1
```

Dashboard: http://127.0.0.1:8080

## Proxy-Modus (Produktion)

1. Config kopieren und Wallet setzen:

```bash
cp config.example.yaml config.yaml
# upstream_wallet: deine SASEUL-Adresse
```

2. Starten:

```bash
python -m onepool --config config.yaml
# oder:
ONEPOOL_UPSTREAM_WALLET=deine_adresse python -m onepool -c config.yaml
```

3. HiveOS Flight Sheet (1Miner):

| Feld | Wert |
|------|------|
| Pool URL | `DEINE_IP:1911` |
| Wallet | `%WAL%.%WORKER_NAME%` |
| Extra | `--autotune-force` (optional) |

TLS-Ports analog RabbitMiner: `1901`/`1921` = SSL, `1911`/`1931` = TCP. Für TLS `tls_port`, `tls_cert`, `tls_key` in der Config setzen.

## Docker

```bash
cp config.example.yaml config.yaml
# upstream_wallet eintragen
docker compose up -d --build
```

Ports: `1911` (Stratum), `8080` (Dashboard).

## API

- `GET /` — Dashboard
- `GET /api/stats` — JSON (Worker, Shares, Hashrate-Schätzung)
- `GET /api/health` — Healthcheck

## Protokoll (Kurz)

Newline-terminiertes JSON (Array-`params` Pflicht):

```json
{"id":1,"method":"login","params":["WALLET","x","WORKER"]}
{"id":2,"method":"getjob","params":[]}
{"id":3,"method":"submit","params":["job_id","nonce","timestamp_us","blockhash"]}
```

Antworten enthalten `"ok":true` bei Erfolg. `getjob` liefert u.a. `previous_blockhash` (78 hex), `share_difficulty`, `worktime` (µs, Clock-Sync).

## Tests

```bash
cd 1Pool
pip install -r requirements.txt
python -m unittest discover -s tests -v
```

## Hinweise

- **Fee** (`pool_fee_percent`) ist derzeit Buchhaltung/Anzeige (PROP-Shares). Auszahlung musst du selbst organisieren (Upstream zahlt an `upstream_wallet`).
- Echter Solo-Betrieb gegen einen eigenen `saseul-node` braucht eine Node-seitige Job-API; der Proxy-Modus ist der praxisnahe Weg mit bestehendem Upstream.
- Timestamp-Drift: Shares müssen innerhalb von ~5 s zur Pool-Zeit liegen (`max_timestamp_drift_us`).

## License

MIT
