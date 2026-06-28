# GONKAMINER — Installation HiveOS Shell (v0.2.1)

## Sofort-Fix wenn „FEHLER: falsches Archiv“

Altes/kaputtes Download-Archiv löschen und neu installieren:

```bash
rm -rf /hive/miners/custom/gonkaminer
rm -f /hive/miners/custom/downloads/gonkaminer-*.tar.gz
cd /tmp
wget -q "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.1/install-hiveos-shell.sh" -O install.sh
GONKAMINER_FORCE=1 bash install.sh
```

---

## Normale Installation (ein Befehl)

```bash
cd /tmp
wget -q "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.1/install-hiveos-shell.sh" -O install.sh \
  || curl -fsSL -o install.sh "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.1/install-hiveos-shell.sh"
bash install.sh
```

---

## Manuell (wenn install.sh nicht geht)

```bash
rm -rf /hive/miners/custom/gonkaminer
rm -f /hive/miners/custom/downloads/gonkaminer-*.tar.gz
mkdir -p /hive/miners/custom/downloads
cd /hive/miners/custom/downloads

wget "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.1/gonkaminer-0.2.1.tar.gz" \
  -O gonkaminer-0.2.1.tar.gz

file gonkaminer-0.2.1.tar.gz
tar -tzf gonkaminer-0.2.1.tar.gz | head -3
```

Erwartung:
```
gonkaminer-0.2.1.tar.gz: gzip compressed data
gonkaminer/
gonkaminer/h-run.sh
```

Dann entpacken:

```bash
cd /hive/miners/custom
tar -xzf downloads/gonkaminer-0.2.1.tar.gz
chmod +x gonkaminer/*.sh gonkaminer/scripts/*.sh
cd gonkaminer
export HSA_OVERRIDE_GFX_VERSION=10.3.0
bash scripts/bootstrap.sh
bash scripts/doctor.sh
./h-run.sh
```

---

## Prüfen

```bash
bash /hive/miners/custom/gonkaminer/scripts/doctor.sh
curl http://127.0.0.1:8080/health
```

---

## Flight Sheet

| Feld | Wert |
|------|------|
| Miner name | `gonkaminer` |
| Install URL | `https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.1/gonkaminer-0.2.1.tar.gz` |
| Extra config | `HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080` |

---

Release: https://github.com/Crypto-EU/Sample/releases/tag/gonkaminer-v0.2.1
