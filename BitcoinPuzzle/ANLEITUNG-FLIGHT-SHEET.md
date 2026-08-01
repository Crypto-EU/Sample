# HiveOS Flight Sheet — ausführliche Anleitung  
## Bitcoin Puzzle mit AMD (BtcMole Custom Miner)

Stand: August 2026

Diese Anleitung erklärt **nur den HiveOS-Flight-Sheet-Weg**: von der Paket-Vorbereitung bis zum laufenden Worker auf dem Rig.  
Ziel: Puzzle **#71** (Nummer anpassen, falls schon gelöst) mit **AMD-GPUs**.

> Flight Sheets sind HiveOS-„Arbeitsaufträge“: Wallet + Pool + Miner-Config.  
> Du weist sie einem Rig zu → Hive lädt den Miner, schreibt die Config und startet ihn.

---

# 0. Was du vorher brauchst

## 0.1 Checkliste

| # | Voraussetzung | Erledigt? |
|---|---------------|-----------|
| 1 | HiveOS-Account + Farm | ☐ |
| 2 | Rig online (grün), AMD-GPUs sichtbar | ☐ |
| 3 | SSH-Zugang zum Rig (empfohlen) | ☐ |
| 4 | `amd-info` zeigt Karten | ☐ |
| 5 | BtcMole AMD-Binary (`bmXXX` / `_amdgpu`) | ☐ |
| 6 | Ort zum Hosten einer `.tar.gz` (HTTPS Direct Link) | ☐ |

## 0.2 Warum Custom Miner + Flight Sheet?

Bitcoin-Puzzle ist **kein** normales Coin-Mining. Es gibt keinen festen „BTC Puzzle“-Eintrag mit Stratum in HiveOS.  
Deshalb:

1. Du baust ein **Custom-Miner-Paket** (`btcmole`).  
2. Du trägst dessen Download-URL in eine **Flight Sheet** ein.  
3. HiveOS installiert und startet das Paket wie jeden anderen Miner.

---

# 1. Custom-Miner-Paket vorbereiten

Hive erwartet ein **tar.gz**, dessen Inhalt ungefähr so aussieht:

```text
h-manifest.conf
h-config.sh
h-run.sh
h-stats.sh
btcmole          ← ausführbares Binary
```

Vorlagen: Ordner `BitcoinPuzzle/hiveos-btcmole/` in diesem Repo.

## Schritt 1.1 — Arbeitsordner anlegen (PC oder Rig)

```bash
mkdir -p ~/btcmole-pack
cd ~/btcmole-pack
```

Kopiere hinein:

- `h-manifest.conf`
- `h-config.sh`
- `h-run.sh`
- `h-stats.sh`

```bash
chmod +x h-config.sh h-run.sh h-stats.sh
```

## Schritt 1.2 — AMD-Binary von BtcMole holen

1. Öffne: https://github.com/keymole/btcmole  
2. Gehe in `linux/`  
3. Lade die Datei mit **`_amdgpu.zip`**, z. B.:

```text
bm079.linux_amd64_amdgpu.zip
```

4. Entpacken und als `btcmole` ablegen:

```bash
cd ~/btcmole-pack
unzip -o bm079.linux_amd64_amdgpu.zip
# Die ausführbare Datei heißt z. B. bm079
cp -f bm079 btcmole
chmod +x btcmole
ls -la btcmole
```

Prüfen:

```bash
./btcmole
# oder: file btcmole
```

Es muss eine Linux-x86_64-Executable sein, kein Text / kein Windows-`.exe`.

## Schritt 1.3 — Kurztest auf dem Rig (optional, aber empfohlen)

Vor dem Verpacken per SSH auf dem Rig:

```bash
cd /tmp
# Binary kurz hinlegen und testen
./btcmole bf -pz 30 +amdgpu -cpu
```

Erwartung: GPU erkannt, nach kurzer Zeit `FOUND_PUZZLE_30.txt`.  
Wenn das scheitert → erst Treiber/ROCm/`HSA_OVERRIDE_GFX_VERSION` klären (siehe Abschnitt 8), dann weiter.

## Schritt 1.4 — tar.gz bauen

**Wichtig:** Im Archiv müssen die Dateien **im Root** liegen (nicht in einem Unterordner).

```bash
cd ~/btcmole-pack
tar -czvf btcmole-hive-1.0.0.tar.gz \
  h-manifest.conf \
  h-config.sh \
  h-run.sh \
  h-stats.sh \
  btcmole
```

Kontrolle:

```bash
tar -tzvf btcmole-hive-1.0.0.tar.gz
```

Richtige Ausgabe (Auszug):

```text
h-manifest.conf
h-config.sh
h-run.sh
h-stats.sh
btcmole
```

Falsch wäre z. B.:

```text
btcmole-pack/h-run.sh
btcmole-pack/btcmole
```

→ Dann neu packen, ohne übergeordneten Ordnernamen.

## Schritt 1.5 — Paket hosten (Installation URL)

HiveOS lädt das Paket per HTTP(S). Du brauchst einen **Direct Download**-Link.

### Variante A — GitHub Release (einfach)

1. Repo auf GitHub öffnen → **Releases** → **Draft a new release**  
2. Tag z. B. `btcmole-hive-1.0.0`  
3. Datei `btcmole-hive-1.0.0.tar.gz` hochladen  
4. Publish  
5. Rechtsklick auf den Asset-Link → Link kopieren  

Beispiel-Form:

```text
https://github.com/DEIN_USER/DEIN_REPO/releases/download/btcmole-hive-1.0.0/btcmole-hive-1.0.0.tar.gz
```

### Variante B — Eigener Webserver / Object Storage

Datei hochladen, öffentliche HTTPS-URL notieren.

### URL testen

Im Browser oder per curl:

```bash
curl -I "DEINE_URL"
```

Erwartung: `HTTP/2 200` (oder 302→200), `Content-Type` oft `application/gzip` / `octet-stream`.  
Kein Login, keine HTML-Download-Seite.

Diese URL ist deine **Installation URL** für die Flight Sheet.

---

# 2. Wallet in HiveOS anlegen (Platzhalter)

Puzzle braucht kein klassisches Pool-Wallet — Hive verlangt aber oft Wallet + Pool-Felder.

## Schritt 2.1 — Wallet erstellen

1. HiveOS Web → Farm → **Wallets** (oder „Finances / Wallets“).  
2. **Add Wallet**.  
3. Empfohlene Werte:

| Feld | Wert | Warum |
|------|------|--------|
| Coin | `BTC` oder Custom | nur Label |
| Address / Wallet | `puzzle` oder deine Notiz | wird vom Custom Miner meist ignoriert |
| Name | `BTC-Puzzle` | Anzeige in der Farm |

4. Speichern.

> Der echte „Fund“ landet in der BtcMole-`FOUND_*.txt`, nicht über Hive-Payout.

---

# 3. Flight Sheet anlegen — Feld für Feld

## Schritt 3.1 — Flight Sheets öffnen

1. Einloggen: https://the.hiveos.farm/  
2. Deine **Farm** wählen.  
3. Links/Menü: **Flight Sheets**.  
4. Button **Add Flight Sheet** / „+“.

## Schritt 3.2 — Kopfzeile der Flight Sheet

| Feld in der UI | Eintragen | Erklärung |
|----------------|-----------|-----------|
| **Flight Sheet name** | `BTC-Puzzle-71-AMD` | beliebiger klarer Name |
| **Coin** | BTC (oder Custom) | muss zur Wallet passen |
| **Wallet** | `BTC-Puzzle` (aus Schritt 2) | aus Dropdown wählen |

## Schritt 3.3 — Pool-Block (Platzhalter)

Auch wenn kein Stratum-Pool genutzt wird, Felder füllen:

| Feld | Eintragen |
|------|-----------|
| **Pool** | „Configure“ / Custom Pool |
| **URL** / Pool address | `localhost` oder `127.0.0.1` |
| **Port** | `1` (oder leer lassen, je nach UI) |
| **Password** | `x` |

Manche Hive-Versionen haben ein Feld „Pool URL“ als eine Zeile:

```text
localhost:1
```

Das reicht — BtcMole liest diese Hive-Pool-URL **nicht** für Puzzle #71 (Steuerung läuft über Extra config).

## Schritt 3.4 — Miner = Custom

1. Bei **Miner** das Dropdown öffnen.  
2. **Custom** wählen (nicht TeamRedMiner, nicht lolMiner, …).  
3. Button **Setup Miner Config** / Zahnrad klicken.

Es öffnet sich das Custom-Miner-Fenster.

---

# 4. Setup Miner Config — die wichtigsten Felder

Trage **exakt** folgendes ein (Namen müssen zum Paket passen):

## 4.1 Pflichtfelder

| Feld | Wert | Regel |
|------|------|--------|
| **Miner name** | `btcmole` | **Muss** gleich `CUSTOM_NAME` in `h-manifest.conf` sein |
| **Installation URL** | `https://…/btcmole-hive-1.0.0.tar.gz` | Direct Link aus Schritt 1.5 |
| **Hash algorithm** | leer oder `custom` | kein Ethash/Kawpow |
| **Wallet and worker template** | `%WAL%.%WORKER_NAME%` | Hive-Standard; Solver ignoriert es meist |
| **Pool URL** | `%URL%` oder `localhost:1` | je nach UI-Vorlage |
| **Pass** | `x` | Platzhalter |

### Miner name — häufigster Fehler

- Paket heißt intern `btcmole` → Miner name **`btcmole`**  
- Nicht `BtcMole`, nicht `btcmole-hive`, nicht `custom`

Hive entpackt nach:

```text
/hive/miners/custom/btcmole/
```

## 4.2 Extra config arguments (das steuert BtcMole)

Das Feld **Extra config** / **Extra config arguments** wird von `h-config.sh` 1:1 in die Startzeile geschrieben.

### Variante A — Solo Puzzle 71 (Standard)

```text
bf -pz 71 +amdgpu -cpu
```

Bedeutung:

| Teil | Bedeutung |
|------|-----------|
| `bf` | Bruteforce-Modus (offline) |
| `-pz 71` | Bitcoin Puzzle Nummer 71 |
| `+amdgpu` | alle AMD-GPUs nutzen |
| `-cpu` | CPU nicht mitrechnen |

### Variante B — mit GFX-Override (RDNA oft nötig)

**RX 5700 XT / 5600 XT:**

```text
HSA_OVERRIDE_GFX_VERSION=10.1.0 bf -pz 71 +amdgpu -cpu
```

**RX 6800 / 6900 XT:**

```text
HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu -cpu
```

**RX 7900 XT / XTX:**

```text
HSA_OVERRIDE_GFX_VERSION=11.0.0 bf -pz 71 +amdgpu -cpu
```

> `HSA_…=…` muss **vor** `bf` stehen (unser `h-run.sh` exportiert `KEY=VAL`-Tokens zuerst).

### Variante C — BtcMole-Pool (`dig`)

1. Session bei https://t.me/BtcMoleBot holen.  
2. Extra config:

```text
dig -s DEINE_SESSION +amdgpu -cpu
```

Mit Drosselung (z. B. Desktop-GPU):

```text
dig -s DEINE_SESSION +amdgpu:90% -cpu
```

### Variante D — Selbsttest über Flight Sheet

Zum Prüfen der ganzen Kette einmal:

```text
bf -pz 30 +amdgpu -cpu
```

Wenn `FOUND_PUZZLE_30.txt` erscheint → Flight Sheet/Paket ok → Extra auf `#71` umstellen.

## 4.3 Speichern

1. Im Custom-Config-Fenster **Apply / Save**.  
2. In der Flight-Sheet-Hauptmaske erneut **Create / Save**.

Die Flight Sheet erscheint jetzt in der Liste, z. B. `BTC-Puzzle-71-AMD`.

---

# 5. Flight Sheet dem Rig zuweisen

## Schritt 5.1 — Rig öffnen

1. Farm → **Workers / Rigs**.  
2. Deinen AMD-Rig anklicken.

## Schritt 5.2 — Flight Sheet wählen

1. Bereich **Flight Sheet** / „Rocket“-Icon.  
2. Dropdown: `BTC-Puzzle-71-AMD` auswählen.  
3. **Apply** / **Start mining** / Häkchen.

## Schritt 5.3 — Was Hive jetzt macht

1. Lädt die Installation URL.  
2. Entpackt nach `/hive/miners/custom/btcmole/`.  
3. Ruft `h-config.sh` auf → schreibt Extra config nach `btcmole.conf`.  
4. Startet `h-run.sh` → startet `./btcmole …`.  
5. Schreibt Logs nach `/var/log/miner/custom/btcmole.log`.

Dauer: oft 30–120 Sekunden beim ersten Mal.

---

# 6. Prüfen, ob die Flight Sheet läuft

## 6.1 Im Hive Web-UI

Auf der Rig-Seite solltest du sehen:

| Anzeige | Gut | Schlecht |
|---------|-----|----------|
| Miner | `btcmole` / Custom | leer / anderer Miner |
| Status | mining / working | stopped / error |
| Hashrate | Zahl (kann „komisch“ skaliert sein) | dauerhaft 0 + Fehler |
| Log-Vorschau | Keys/s, GPU, `bf -pz 71` | `binary not found`, Download-Fehler |

> Hashrate in Hive bei Puzzle ist nur eine **Schätzung** aus `h-stats.sh`. Entscheidend ist das **Log**.

## 6.2 Miner-Log im UI

1. Rig → **Miner log** / Console / „Show log“.  
2. Suche nach Zeilen wie:

```text
Starting .../btcmole bf -pz 71 +amdgpu -cpu
env: HSA_OVERRIDE_GFX_VERSION=...
```

und Geschwindigkeitszeilen (Mkey/s, keys/s).

## 6.3 Per SSH (genaueste Kontrolle)

```bash
ssh user@RIG_IP

# Läuft der Prozess?
ps aux | grep -E 'btcmole|bm[0-9]' | grep -v grep

# Live-Log
tail -f /var/log/miner/custom/btcmole.log

# Installationsordner
ls -la /hive/miners/custom/btcmole/

# Geschriebene Config (= Extra config)
cat /hive/miners/custom/btcmole/btcmole.conf
```

Erwarteter Inhalt von `btcmole.conf`:

```text
bf -pz 71 +amdgpu -cpu
```

oder mit Override:

```text
HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu -cpu
```

## 6.4 Reboot-Test

```bash
sudo reboot
```

Nach dem Online-Kommen:

1. Flight Sheet noch zugewiesen?  
2. Miner startet von allein?  
3. Log zeigt wieder Keys/s?

Wenn ja → Autostart über Flight Sheet ist ok.

---

# 7. Flight Sheet ändern / aktualisieren

## 7.1 Extra config ändern (z. B. anderes Puzzle)

1. Flight Sheets → `BTC-Puzzle-71-AMD` → Edit.  
2. Setup Miner Config → Extra config anpassen, z. B.:

```text
bf -pz 72 +amdgpu -cpu
```

3. Speichern.  
4. Rig → Flight Sheet erneut **Apply** (manchmal „Reload“ nötig).

Hive schreibt die Config neu und startet den Miner neu.

## 7.2 Neues Binary / neue Paketversion

1. Neues `btcmole-hive-1.0.1.tar.gz` bauen und hosten.  
2. In der Flight Sheet **Installation URL** auf die neue URL setzen.  
3. Speichern + Apply.  

Oder auf dem Rig erzwingen:

```bash
/hive/miners/custom/custom-get "NEUE_URL" -f
```

Danach Flight Sheet neu starten.

## 7.3 Flight Sheet stoppen

Rig → Mining stoppen / Flight Sheet auf „None“ / Stop.  
Prozess sollte enden:

```bash
ps aux | grep btcmole | grep -v grep
```

---

# 8. Typische Flight-Sheet-Fehler

## 8.1 Download fehlgeschlagen / Installation URL

**Symptom:** Log: cannot download / 404 / HTML statt tar.gz  

**Fix:**

1. URL im Browser testen.  
2. Direct Link, kein GitHub-„Seite anzeigen“.  
3. HTTPS erreichbar vom Rig (Firewall/DNS).

```bash
curl -L -o /tmp/test.tgz "DEINE_URL"
tar -tzvf /tmp/test.tgz | head
```

## 8.2 `ERROR: no btcmole/bmXXX binary`

**Ursache:** Binary fehlte im tar.gz oder nicht executable.  

**Fix:** Schritt 1.2/1.4 wiederholen, `chmod +x btcmole`, neu hosten, URL aktualisieren, `custom-get … -f`.

## 8.3 Miner name falsch

**Symptom:** Hive sucht anderen Ordner, startet nicht.  

**Fix:** Miner name = `btcmole` (klein, exakt).

## 8.4 GPU wird nicht genutzt

**Symptom:** nur CPU / 0 GPU / HIP-Fehler  

**Fix Extra config:**

```text
HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu -cpu
```

(Wert an GPU anpassen.)  
Zusätzlich auf dem Rig ROCm/OpenCL prüfen (`amd-info`, `clinfo`).

## 8.5 Extra config wird ignoriert

**Symptom:** immer Default `bf -pz 71 +amdgpu` obwohl anders eingetragen  

**Fix:**

1. Flight Sheet speichern + neu Apply.  
2. Prüfen:

```bash
cat /hive/miners/custom/btcmole/btcmole.conf
```

3. Falls alt: `h-config.sh` manuell:

```bash
cd /hive/miners/custom/btcmole
# CUSTOM_USER_CONFIG setzt Hive — alternativ:
echo 'bf -pz 71 +amdgpu -cpu' > btcmole.conf
miner start
```

## 8.6 Hashrate in Hive = 0, aber Log ok

Normal möglich: Stats-Parser findet das Logformat nicht.  
→ Am Log und an `ps` orientieren, nicht nur an der Hive-Hashrate-Zahl.

## 8.7 Paket falsch verschachtelt

```bash
tar -tzvf btcmole-hive-1.0.0.tar.gz
```

Wenn Pfade `irgendwas/btcmole` enthalten → neu packen aus dem Ordner heraus (Schritt 1.4).

---

# 9. Fertige Flight-Sheet-Vorlagen (zum Abtippen)

## Vorlage 1 — Solo #71, AMD allgemein

```text
Flight Sheet name:     BTC-Puzzle-71-AMD
Coin:                  BTC
Wallet:                BTC-Puzzle
Pool URL:              localhost:1
Pass:                  x
Miner:                 Custom
  Miner name:          btcmole
  Installation URL:    https://…/btcmole-hive-1.0.0.tar.gz
  Wallet template:     %WAL%.%WORKER_NAME%
  Extra config:        bf -pz 71 +amdgpu -cpu
```

## Vorlage 2 — Solo #71, RX 6800 XT

```text
Extra config:  HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu -cpu
```

(Rest wie Vorlage 1.)

## Vorlage 3 — Solo #71, RX 5700 XT

```text
Extra config:  HSA_OVERRIDE_GFX_VERSION=10.1.0 bf -pz 71 +amdgpu -cpu
```

## Vorlage 4 — BtcMole Pool

```text
Extra config:  dig -s DEINE_SESSION +amdgpu -cpu
```

## Vorlage 5 — Selbsttest #30

```text
Extra config:  bf -pz 30 +amdgpu -cpu
```

Nach Erfolg Extra wieder auf #71 stellen und Apply.

---

# 10. Ablauf in 12 Schritten (Kurzüberblick)

1. Wrapper-Skripte nach `~/btcmole-pack` kopieren.  
2. BtcMole `_amdgpu` als `btcmole` ablegen, `chmod +x`.  
3. Optional: auf dem Rig `bf -pz 30` testen.  
4. `tar -czvf btcmole-hive-1.0.0.tar.gz …` (Root-Dateien!).  
5. tar.gz per HTTPS hosten, URL im Browser testen.  
6. Hive-Wallet `BTC-Puzzle` anlegen.  
7. Flight Sheet `BTC-Puzzle-71-AMD` anlegen.  
8. Miner = Custom, Miner name = `btcmole`, Installation URL setzen.  
9. Extra config = `bf -pz 71 +amdgpu -cpu` (ggf. mit HSA_…).  
10. Flight Sheet speichern.  
11. Rig → Flight Sheet Apply.  
12. Log prüfen → Reboot-Test → fertig.

---

# 11. Fund während Flight-Sheet-Betrieb

1. Im Log nach `TREASURE KEY FOUND` suchen.  
2. Datei sichern:

```bash
ls -la /hive/miners/custom/btcmole/FOUND_*
cp /hive/miners/custom/btcmole/FOUND_* /home/user/
```

3. Flight Sheet **sofort stoppen**.  
4. Key offline sichern — **nicht** öffentlich im Mempool broadcasten.  
5. Details: siehe `ANLEITUNG-AMD-HIVEOS.md` Teil G.

---

# 12. Nützliche Hive-/Shell-Befehle

```bash
# Custom Miner neu laden
/hive/miners/custom/custom-get "https://…/btcmole-hive-1.0.0.tar.gz" -f

# Miner steuern
miner start
miner stop
miner restart
miner log

# Config & Log
cat /hive/miners/custom/btcmole/btcmole.conf
tail -100 /var/log/miner/custom/btcmole.log

# GPUs
amd-info
```

---

# 13. Weiterführend

| Dokument | Inhalt |
|----------|--------|
| [ANLEITUNG-AMD-HIVEOS.md](./ANLEITUNG-AMD-HIVEOS.md) | Gesamtanleitung A–K (SSH, ROCm, Sicherheit, Fehler) |
| [hiveos-btcmole/](./hiveos-btcmole/) | Fertige Wrapper-Skripte |
| https://github.com/keymole/btcmole | Solver-Releases |
| https://hiveon.com/knowledge-base/ | Offizielle Custom-Miner-Doku |

---

## Haftung

Du bist selbst für Rig, Strom, Keys und rechtmäßige Nutzung verantwortlich. Flight Sheets steuern nur den Start deines lokalen Solvers; eine Gewinn-Garantie gibt es nicht.
