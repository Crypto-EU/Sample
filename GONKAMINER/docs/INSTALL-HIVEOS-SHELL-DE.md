# GONKAMINER v0.2.2 — HiveOS Shell

## Du bist schon in `/hive/miners/custom/gonkaminer`?

Dann **nicht** neu installieren — direkt starten:

```bash
cd /hive/miners/custom/gonkaminer
export HSA_OVERRIDE_GFX_VERSION=10.3.0
chmod +x *.sh scripts/*.sh
bash scripts/bootstrap.sh
./h-run.sh
```

Log: `tail -f /var/log/miner/custom/gonkaminer.log`  
Test: `curl http://127.0.0.1:8080/health`

---

## Neuinstallation (v0.2.2)

```bash
rm -rf /hive/miners/custom/gonkaminer
rm -f /hive/miners/custom/downloads/gonkaminer-*.tar.gz
cd /tmp
wget "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.2.2/install-hiveos-shell.sh" -O install.sh
GONKAMINER_FORCE=1 bash install.sh
```

**Hinweis:** v0.2.2 nutzt `tar` statt `file` zur Archiv-Prüfung (HiveOS `file` liefert oft „unknown“).

---

## Archiv manuell prüfen

```bash
tar -tzf /hive/miners/custom/downloads/gonkaminer-0.2.2.tar.gz | head -3
```

Wenn `gonkaminer/` erscheint → Archiv ist OK (38K ist normal).

---

Release: https://github.com/Crypto-EU/Sample/releases/tag/gonkaminer-v0.2.2
