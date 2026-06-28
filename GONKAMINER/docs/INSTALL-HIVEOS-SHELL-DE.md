# GONKAMINER — Installation HiveOS Shell (letzter Stand v0.2.0)

**Ein Befehl. Kein venv-Gehakel. RX 6800 XT.**

---

## Schritt 1 — Auf der HiveOS Shell (als root)

```bash
cd /tmp
wget -q "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.0/install-hiveos-shell.sh" -O install.sh \
  || curl -fsSL -o install.sh "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.0/install-hiveos-shell.sh"
bash install.sh
```

Das Skript:
- lädt `gonkaminer-0.2.0.tar.gz` herunter
- entpackt nach `/hive/miners/custom/gonkaminer/`
- installiert Python-Pakete **ohne venv** (`pydeps/`)
- installiert ROCm-PyTorch (5–15 Minuten)
- führt `doctor.sh` aus

---

## Schritt 2 — Prüfen

```bash
bash /hive/miners/custom/gonkaminer/scripts/doctor.sh
curl http://127.0.0.1:8080/health
```

Erwartung: `doctor` = PASS, health = `{"status":"ok",...}`

---

## Schritt 3 — Miner starten

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0
cd /hive/miners/custom/gonkaminer
./h-run.sh
```

Log: `tail -f /var/log/miner/custom/gonkaminer.log`

---

## Schritt 4 — Flight Sheet

| Feld | Wert |
|------|------|
| Miner | Custom |
| Miner name | `gonkaminer` |
| Install URL | `https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.0/gonkaminer-0.2.0.tar.gz` |
| Extra config | `HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080` |

---

## Wenn etwas schiefgeht

```bash
cd /hive/miners/custom/gonkaminer
rm -f .bootstrap_ok
rm -rf pydeps .venv
bash scripts/bootstrap.sh
bash scripts/doctor.sh
```

### `ensurepip` / `python3-venv` Fehler (v0.1.x)

v0.2.0 braucht **kein venv** mehr. Neu installieren mit `install.sh` oben.

### Kein GNK

GONKAMINER ist nur der PoC-Worker. Du brauchst einen **Gonka API-Node** (Docker auf separatem Server). Ohne den läuft der Worker, bekommt aber keine Jobs.

---

## Links

- Release: https://github.com/Crypto-EU/Sample/releases/tag/gonkaminer-v0.2.0
- Download: https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.0/gonkaminer-0.2.0.tar.gz
