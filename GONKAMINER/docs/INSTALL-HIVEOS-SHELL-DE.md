# GONKAMINER — Manuelle Installation über HiveOS Shell

Wenn der Miner im Flight Sheet **nicht heruntergeladen** wird oder `/hive/miners/custom/gonkaminer` **nicht existiert**, installiere GONKAMINER manuell direkt auf der Rig.

**Version:** 0.1.3  
**GPU:** AMD RX 6800 XT (und andere RDNA2/RDNA3-Karten)

---

## Voraussetzungen

- HiveOS **AMD-Image** mit funktionierendem GPU-Treiber
- SSH oder direkter Shell-Zugang zur Rig (**als root**)
- Internetzugang der Rig (für GitHub-Download)
- Mindestens **10 GB freier VRAM** für Gonka PoC

---

## Methode A — Einzeiler (empfohlen)

Auf der **HiveOS Shell** der Rig:

```bash
cd /tmp
curl -fsSL "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/install-hiveos-shell.sh" -o install.sh \
  || wget -q "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/install-hiveos-shell.sh" -O install.sh
bash install.sh
```

Falls der Einzeiler nicht lädt → **Methode B** verwenden.

---

## Methode B — Schritt für Schritt (copy & paste)

### 1. Shell öffnen

- HiveOS Web-UI → Rig → **Shell**  
  oder per SSH: `ssh root@DEINE_RIG_IP`

### 2. Verzeichnis vorbereiten

```bash
mkdir -p /hive/miners/custom/downloads
cd /hive/miners/custom/downloads
```

### 3. Miner-Paket herunterladen

**Mit wget:**

```bash
wget -c --timeout=60 --tries=3 \
  "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/gonkaminer-0.1.3.tar.gz" \
  -O gonkaminer-0.1.3.tar.gz
```

**Falls wget fehlschlägt — mit curl:**

```bash
curl -fL --retry 3 --connect-timeout 60 \
  -o gonkaminer-0.1.3.tar.gz \
  "https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/gonkaminer-0.1.3.tar.gz"
```

**Prüfen, ob die Datei da ist (sollte ~34 KB sein, nicht 0 Bytes):**

```bash
ls -lh gonkaminer-0.1.3.tar.gz
file gonkaminer-0.1.3.tar.gz
```

Erwartete Ausgabe: `gzip compressed data`

**Archiv-Inhalt prüfen:**

```bash
tar -tzf gonkaminer-0.1.3.tar.gz | head -5
```

Erwartete erste Zeile: `gonkaminer/`

### 4. Alte Installation entfernen (falls vorhanden)

```bash
rm -rf /hive/miners/custom/gonkaminer
```

### 5. Entpacken

```bash
cd /hive/miners/custom
tar -xzf downloads/gonkaminer-0.1.3.tar.gz
```

### 6. Rechte setzen

```bash
chmod +x /hive/miners/custom/gonkaminer/*.sh
chmod +x /hive/miners/custom/gonkaminer/scripts/*.sh
chown -R user:user /hive/miners/custom/gonkaminer
```

### 7. Installation prüfen

```bash
ls -la /hive/miners/custom/gonkaminer/
```

Diese Dateien **müssen** existieren:

| Datei | Pfad |
|-------|------|
| Manifest | `/hive/miners/custom/gonkaminer/h-manifest.conf` |
| Config | `/hive/miners/custom/gonkaminer/h-config.sh` |
| Start | `/hive/miners/custom/gonkaminer/h-run.sh` |
| Stats | `/hive/miners/custom/gonkaminer/h-stats.sh` |
| PoC-Code | `/hive/miners/custom/gonkaminer/vendor/pow/` |

Schnelltest:

```bash
test -f /hive/miners/custom/gonkaminer/h-run.sh && echo "OK: Miner installiert" || echo "FEHLER: Miner fehlt"
```

---

## Methode C — Download am PC, Upload per SCP

Wenn die Rig **kein GitHub** erreicht:

**Am PC (Browser):**  
https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/gonkaminer-0.1.3.tar.gz

**Vom PC zur Rig kopieren:**

```bash
scp gonkaminer-0.1.3.tar.gz root@DEINE_RIG_IP:/hive/miners/custom/downloads/
```

**Dann auf der Rig (Schritt 4–7 von Methode B):**

```bash
cd /hive/miners/custom
rm -rf gonkaminer
tar -xzf downloads/gonkaminer-0.1.3.tar.gz
chmod +x gonkaminer/*.sh gonkaminer/scripts/*.sh
chown -R user:user gonkaminer
```

---

## Flight Sheet einrichten (nach manueller Installation)

Auch bei manueller Installation braucht HiveOS die **korrekten Flight-Sheet-Werte**:

| Feld | Wert |
|------|------|
| Miner | **Custom** |
| Miner name | `gonkaminer` |
| Installation URL | `https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/gonkaminer-0.1.3.tar.gz` |
| Pool URL | beliebig (z. B. `host-local:8080`) |
| Wallet | deine Gonka/Cosmos-Adresse |
| Extra config | `HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080` |

**Wichtig:**

- Miner name muss exakt `gonkaminer` heißen — **nicht** `gonkaminer-hiveos`
- Die URL muss `gonkaminer-0.1.3.tar.gz` enden (HiveOS leitet den Namen daraus ab)
- Wenn der Ordner schon existiert, überspringt HiveOS den Download und startet direkt

Flight Sheet speichern → Miner **neu starten**.

---

## Manuell starten (Test ohne Flight Sheet)

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0
export GONKAMINER_PORT=8080
export CUSTOM_USER_CONFIG="HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080"
cd /hive/miners/custom/gonkaminer
./h-config.sh
./h-run.sh
```

**Erster Start:** ROCm-PyTorch wird installiert — kann **5–15 Minuten** dauern.

Log live ansehen:

```bash
tail -f /var/log/miner/custom/gonkaminer.log
```

Health-Check (von der Rig oder einem anderen PC):

```bash
curl http://127.0.0.1:8080/health
```

Erwartete Antwort: `{"status":"ok",...}`

---

## Fehlerbehebung

### `ensurepip is not available` / `python3-venv` / kaputte `.venv`

Typische Meldung:

```
The virtual environment was not created successfully because ensurepip is not available.
apt install python3.10-venv
Failing command: /hive/miners/custom/gonkaminer/.venv/bin/python3
```

**Fix auf der HiveOS Shell (als root):**

```bash
apt-get update
apt-get install -y python3.10-venv python3-pip python3-venv
rm -rf /hive/miners/custom/gonkaminer/.venv
cd /hive/miners/custom/gonkaminer
bash scripts/setup-venv.sh .venv
./h-run.sh
```

Oder manuell:

```bash
apt-get update && apt-get install -y python3.10-venv python3-pip
rm -rf /hive/miners/custom/gonkaminer/.venv
python3 -m venv /hive/miners/custom/gonkaminer/.venv
/hive/miners/custom/gonkaminer/.venv/bin/pip install --upgrade pip
```

Ab **v0.1.3** versucht `h-run.sh` das automatisch.

### `wget: unable to resolve host address`

DNS-Problem auf der Rig:

```bash
ping -c 2 github.com
echo "nameserver 8.8.8.8" >> /etc/resolv.conf
```

Dann Download erneut versuchen.

### `404 Not Found` oder leere Datei (0 Bytes)

Alte/falsche URL. Nur diese URL verwenden:

```
https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/gonkaminer-0.1.3.tar.gz
```

**Nicht** verwenden: `gonkaminer-hiveos-0.1.0.tar.gz` oder `gonkaminer-hiveos-0.1.1.tar.gz`

### `Custom miner name should be "gonkaminer-hiveos"`

Flight Sheet URL ist falsch. URL muss mit `gonkaminer-0.1.3.tar.gz` enden und Miner name = `gonkaminer`.

### `/hive/miners/custom/gonkaminer` existiert nicht

Manuelle Installation (Methode B) wurde nicht ausgeführt oder `tar` ist fehlgeschlagen. Schritt 5–7 wiederholen.

### `CUDA is not available` im Log

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0   # RX 6800 XT
rocm-smi
cd /hive/miners/custom/gonkaminer
bash scripts/install-rocm-torch.sh .venv
```

### `python3-venv` fehlt (kurz)

Siehe Abschnitt **`ensurepip is not available`** oben.

### Miner startet, aber kein GNK

GONKAMINER ist nur der **PoC-Worker**. Du brauchst zusätzlich einen **Gonka API-Node** (Docker auf separatem Server) und die Rig-IP in `node-config.json`. Siehe [ANLEITUNG-DE.md](ANLEITUNG-DE.md).

---

## Deinstallation

```bash
miner stop
rm -rf /hive/miners/custom/gonkaminer
rm -f /hive/miners/custom/downloads/gonkaminer-0.1.3.tar.gz
```

---

## Links

| Was | URL |
|-----|-----|
| Release v0.1.3 | https://github.com/Crypto-EU/Sample/releases/tag/gonkaminer-v0.1.3 |
| Direkt-Download | https://github.com/Crypto-EU/Sample/releases/download/gonkaminer-v0.1.3/gonkaminer-0.1.3.tar.gz |
| Gonka Host-Doku | https://gonka.ai/docs/host/quickstart/ |
