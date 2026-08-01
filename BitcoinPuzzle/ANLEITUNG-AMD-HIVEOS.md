# Bitcoin Puzzle mit AMD-GPUs auf HiveOS  
## Ausführliche Schritt-für-Schritt-Anleitung

Stand: August 2026

Diese Anleitung führt dich **Nummer für Nummer** von einem leeren HiveOS-Rig bis zu einem laufenden AMD-Worker für das **Bitcoin Puzzle** (aktuell **#71**).

> **Kernpunkt:** Das ist **kein klassisches Mining**.  
> Du suchst einen **Private Key in einem bekannten Zahlenbereich**.  
> Nur wer den Key findet, bekommt die BTC (Winner-takes-all / Lotterie).

---

# Teil A — Verstehen (5 Minuten lesen)

## Schritt A1 — Was du suchst

2015 hat jemand ~1000 BTC auf 160 Adressen verteilt.  
Bei Puzzle Nummer `n` liegt der Private Key im Bereich:

```text
von  2^(n-1)
bis  2^n − 1
```

Beispiel Puzzle #71:

```text
von  0x400000000000000000
bis  0x7fffffffffffffffff
```

Deine GPU probiert Keys in diesem Bereich. Trifft ein Key die Puzzle-Adresse → Belohnung (~7,1 BTC bei #71).

## Schritt A2 — Status prüfen (immer zuerst)

1. Öffne im Browser: https://btcpuzzle.info/puzzle  
2. Schau, welches Puzzle aktuell das **niedrigste ungelöste** ist.  
3. Notiere dir die Nummer (Stand dieser Anleitung: **71**).  
4. Alternative Liste: https://privatekeyfinder.io/bitcoin-puzzle/

Wenn #71 schon gelöst ist, nimm das nächste ungelöste (z. B. #72) und ersetze in allen Befehlen `71` durch die neue Nummer.

## Schritt A3 — Welchen Weg du mit AMD gehst

| Situation | Was du machst |
|-----------|----------------|
| Reine AMD-Karten auf HiveOS | **BtcMole** (`_amdgpu`) — diese Anleitung |
| Nur NVIDIA | btcpuzzle.info Client / Docker |
| Puzzle mit bekanntem Public Key (#135 etc.) | Kangaroo (Vulkan/CUDA) |

**Diese Anleitung = AMD + HiveOS + BtcMole.**

Offizielle Links:

- BtcMole: https://github.com/keymole/btcmole  
- Telegram News: https://t.me/BtcMole  
- Pool-Bot (optional): https://t.me/BtcMoleBot  

---

# Teil B — HiveOS-Rig vorbereiten

## Schritt B1 — HiveOS auf dem Rig installieren

Falls der Rig noch nicht läuft:

1. Auf einem PC: https://hiveon.com/os/ → HiveOS-Image laden.  
2. Mit Balena Etcher (o. ä.) auf USB schreiben.  
3. USB am Rig starten, HiveOS installieren (Disk auswählen).  
4. Nach dem Boot siehst du im Bildschirm eine **Rig-ID / Farm-Hash**-Anzeige bzw. die IP.

Falls HiveOS schon läuft → weiter mit B2.

## Schritt B2 — Rig in der Hive-Farm anmelden

1. Im Browser: https://the.hiveos.farm/ (oder deine Hive-URL) einloggen.  
2. Farm öffnen bzw. neue Farm anlegen.  
3. Neuen Rig hinzufügen (Add Rig).  
4. **Farm Hash / Rig-Passwort** notieren.  
5. Am Rig (Tastatur oder Hive Shell) Farm Hash eingeben, bis der Rig **online** (grün) in der Farm erscheint.

Prüfen:

- Rig zeigt Status **Online**  
- Unter GPUs erscheinen deine AMD-Karten (z. B. RX 5700 XT, RX 6800 XT)

## Schritt B3 — SSH aktivieren und verbinden

1. In HiveOS Web-UI → Rig öffnen → **SSH** / Terminal freischalten (falls nötig Passwort setzen).  
2. Von deinem PC aus verbinden:

```bash
ssh user@RIG_IP
```

- Standard-User ist oft `user`  
- Passwort: das, was du in Hive gesetzt hast  

Erfolgreich, wenn du eine Shell auf dem Rig hast, z. B.:

```text
user@rigname:~$
```

## Schritt B4 — System aktualisieren

Auf dem Rig per SSH:

```bash
sudo apt-get update
sudo apt-get install -y wget unzip curl ca-certificates
```

## Schritt B5 — AMD-GPUs prüfen

```bash
amd-info
```

Erwartung: Liste deiner Karten mit Temp/Power.

Dann OpenCL prüfen:

```bash
clinfo | head -50
```

Erwartung: Platform „AMD“ / „AMD Accelerated Parallel Processing“ und Device-Namen.

Wenn **keine GPU** erscheint:

1. HiveOS → Rig → Driver / AMD OpenCL neu installieren (UI: AMD driver packages).  
2. Oder per Shell (Beispiel — Version an dein HiveOS-Image anpassen):

```bash
amd-ocl-install 5.7 5.7
```

3. Rig neu starten:

```bash
sudo reboot
```

Nach Reboot erneut `amd-info` und `clinfo` prüfen.

## Schritt B6 — Kühlung & Powerlimits setzen

1. HiveOS Web-UI → Rig → **Overclocking** (AMD).  
2. Sinnvolle Startwerte (Beispiel, **anpassen**):

| GPU | Core | Mem | Powerlimit | Fan |
|-----|------|-----|------------|-----|
| RX 5700 XT | stock/−100 | stock | 110–130 W | auto / 70–80 % |
| RX 6800 XT | stock | stock | 180–220 W | auto |
| RX 7900 XT | stock | stock | nach Temp | auto |

3. Apply auf den Rig.  
4. Ziel: stabile Temps (typisch Core < 85–90 °C, Hotspot im Auge behalten).

> Puzzle-Suche läuft oft **Tage/Wochen** — Stabilität > maximale Taktrate.

---

# Teil C — BtcMole installieren (erster Test per SSH)

## Schritt C1 — Arbeitsordner anlegen

```bash
sudo mkdir -p /hive/custom/btcmole
sudo chown -R user:user /hive/custom/btcmole
cd /hive/custom/btcmole
```

## Schritt C2 — Aktuelle AMD-Version ermitteln

1. Browser: https://github.com/keymole/btcmole  
2. Ordner `linux/` öffnen.  
3. Datei mit Suffix **`_amdgpu.zip`** suchen, z. B.:

```text
bm079.linux_amd64_amdgpu.zip
```

4. Die Nummer (`079`) notieren — in den nächsten Befehlen ersetzen.

## Schritt C3 — Binary herunterladen und entpacken

Auf dem Rig (Nummer anpassen):

```bash
cd /hive/custom/btcmole

wget -O bm-amdgpu.zip \
  "https://github.com/keymole/btcmole/raw/main/linux/bm079.linux_amd64_amdgpu.zip"

unzip -o bm-amdgpu.zip
ls -la
chmod +x bm079
```

Prüfen:

```bash
./bm079
```

Es sollte Hilfetext / Modus-Übersicht kommen, kein sofortiger „library not found“-Absturz.

## Schritt C4 — ROCm/HIP-Laufzeit prüfen

```bash
ls /opt/rocm/lib/libamdhip64.so* 2>/dev/null || echo "ROCm lib fehlt"
ldconfig -p | grep -i hip || echo "HIP nicht im Linker-Cache"
```

Falls Libraries fehlen:

```bash
# Beispiel für viele HiveOS-Setups — Version ggf. anpassen
amd-ocl-install 5.7 5.7
sudo ldconfig
```

Rig bei Bedarf rebooten und zu C3/C4 zurück.

## Schritt C5 — GFX-Override (nur wenn nötig)

Manche RDNA-Karten brauchen:

```bash
# RX 5700 XT / 5600 XT
export HSA_OVERRIDE_GFX_VERSION=10.1.0

# RX 6800 / 6900 XT
export HSA_OVERRIDE_GFX_VERSION=10.3.0

# RX 7900 XT / XTX
export HSA_OVERRIDE_GFX_VERSION=11.0.0
```

Dauerhaft in `~/.bashrc` oder später in der Flight-Sheet-Extra-Config setzen (siehe Teil E).

## Schritt C6 — Funktionstest mit gelöstem Puzzle #30

```bash
cd /hive/custom/btcmole
./bm079 bf -pz 30 +amdgpu
```

**Erfolg sieht so aus:**

1. GPU wird erkannt.  
2. Keys/s werden angezeigt.  
3. Nach kurzer Zeit: Meldung wie `TREASURE KEY FOUND!`  
4. Datei erscheint:

```bash
ls -la FOUND_PUZZLE_30.txt
cat FOUND_PUZZLE_30.txt
```

**Wenn das fehlschlägt:** nicht mit #71 weitermachen.  
Zurück zu C4/C5 (ROCm, GFX-Override), anderes `_amdgpu`-Release, Telegram-Gruppe von BtcMole.

## Schritt C7 — Optional: CPU abschalten

Auf reinen GPU-Rigs oft sinnvoll:

```bash
./bm079 bf -pz 30 +amdgpu -cpu
```

---

# Teil D — Puzzle #71 solo starten (SSH)

## Schritt D1 — Startbefehl

```bash
cd /hive/custom/btcmole
./bm079 bf -pz 71 +amdgpu -cpu
```

Mit GFX-Override in einer Zeile:

```bash
HSA_OVERRIDE_GFX_VERSION=10.3.0 ./bm079 bf -pz 71 +amdgpu -cpu
```

## Schritt D2 — Was du im Log sehen solltest

- Verbindung/Init der AMD-GPU(s)  
- Sector / Progress  
- Geschwindigkeit in **Mkey/s** oder ähnlich  
- Keine Endlos-Crash-Schleife  

Progress-Dateien im Ordner (nicht löschen!):

```bash
ls -la bf_*.map bf_*.state 2>/dev/null
ls -la *.map *.state 2>/dev/null
```

Bei Neustart denselben Befehl im **gleichen Ordner** erneut ausführen → Resume.

## Schritt D3 — Im Hintergrund laufen lassen (tmux)

SSH-Fenster schließen ohne Abbruch:

```bash
sudo apt-get install -y tmux
cd /hive/custom/btcmole
tmux new -s puzzle
./bm079 bf -pz 71 +amdgpu -cpu
```

- Detach: `Ctrl+B`, dann `D`  
- Später wieder rein:

```bash
tmux attach -t puzzle
```

Besser langfristig: **Flight Sheet** (Teil E), damit HiveOS den Prozess überwacht.

## Schritt D4 — Optional: BtcMole-Pool statt Solo

1. Telegram öffnen → [@BtcMoleBot](https://t.me/BtcMoleBot)  
2. Anweisungen folgen → **Session-ID** erhalten.  
3. Starten:

```bash
cd /hive/custom/btcmole
./bm079 dig -s DEINE_SESSION +amdgpu -cpu
```

GPU mit Monitor etwas entlasten:

```bash
./bm079 dig -s DEINE_SESSION +amdgpu:90% -cpu
```

---

# Teil E — Als HiveOS Custom Miner einrichten (empfohlen)

Damit der Solver nach Reboot wieder startet und über Flight Sheets gesteuert wird.

## Schritt E1 — Wrapper-Skripte holen

Die Vorlagen liegen in diesem Repo:

```text
BitcoinPuzzle/hiveos-btcmole/
  h-manifest.conf
  h-config.sh
  h-run.sh
  h-stats.sh
```

Auf dem Rig oder auf deinem PC in einen Ordner legen, z. B. `btcmole-pack/`.

## Schritt E2 — Binary ins Paket legen

```bash
mkdir -p ~/btcmole-pack
cd ~/btcmole-pack

# Skripte aus dem Repo hierher kopieren (oder von GitHub raw laden)
# Dann Binary:
cp /hive/custom/btcmole/bm079 ./btcmole
chmod +x btcmole h-config.sh h-run.sh h-stats.sh
```

Inhalt prüfen:

```bash
ls -la
# erwartet: btcmole, h-manifest.conf, h-config.sh, h-run.sh, h-stats.sh
```

## Schritt E3 — Tarball bauen

```bash
cd ~/btcmole-pack
tar -czvf btcmole-hive-1.0.0.tar.gz \
  h-manifest.conf h-config.sh h-run.sh h-stats.sh btcmole

ls -lh btcmole-hive-1.0.0.tar.gz
```

## Schritt E4 — Tarball hosten (direkte HTTPS-URL)

HiveOS muss die Datei **ohne Login** laden können.

Möglichkeiten:

1. **GitHub Release** in deinem Repo hochladen → Download-URL kopieren  
2. Eigener Webserver / Object Storage  
3. Vorübergehend: anderer HTTPS-Host mit Direct-Link  

Beispiel-URL-Form:

```text
https://github.com/DEIN_USER/DEIN_REPO/releases/download/v1.0.0/btcmole-hive-1.0.0.tar.gz
```

URL im Browser testen: Download muss sofort starten.

## Schritt E5 — Flight Sheet anlegen

1. HiveOS Web → Farm → **Flight Sheets**  
2. **Add Flight Sheet**  
3. Ausfüllen:

| Feld | Wert |
|------|------|
| Name | z. B. `BTC-Puzzle-71-AMD` |
| Coin | Custom / beliebig |
| Wallet | Platzhalter, z. B. `puzzle` oder `%WAL%` |
| Pool URL | Platzhalter, z. B. `localhost` |
| Miner | **Custom** → **Setup Miner Config** |

Im Fenster **Setup Miner Config**:

| Feld | Wert |
|------|------|
| Miner name | `btcmole` |
| Installation URL | deine HTTPS-URL zum `.tar.gz` |
| Hash algorithm | (leer / custom) |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` (wird vom Solver oft ignoriert) |
| Pool URL | `localhost` |
| Pass | `x` |
| Extra config arguments | siehe E6 |

4. Speichern.

## Schritt E6 — Extra config wählen

**Solo Puzzle 71:**

```text
bf -pz 71 +amdgpu -cpu
```

**Mit GFX-Override (RX 6800 XT Beispiel):**

```text
HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu -cpu
```

**BtcMole Pool:**

```text
dig -s DEINE_SESSION +amdgpu -cpu
```

## Schritt E7 — Flight Sheet auf den Rig anwenden

1. Farm → Rig auswählen.  
2. Flight Sheet `BTC-Puzzle-71-AMD` wählen → **Apply** / Start.  
3. Warten, bis Custom Miner heruntergeladen und gestartet ist (1–2 Minuten).

## Schritt E8 — Logs prüfen

Im Hive Web: Rig → **Miner log** / Console.

Oder per SSH:

```bash
miner log
# bzw.
tail -f /var/log/miner/custom/btcmole.log
```

Erwartung:

- `Starting ... bf -pz 71 ...`  
- GPU erkannt  
- Keys/s laufen  

Fehler „binary not found“ → Tarball ohne `btcmole` Binary gebaut (zurück zu E2).

## Schritt E9 — Autostart absichern

1. Flight Sheet bleibt dem Rig zugewiesen.  
2. Hive Watchdog / Miner-Autostart aktiv (Standard).  
3. Rig rebooten und prüfen, ob der Solver von allein wieder läuft:

```bash
sudo reboot
```

Nach Offline→Online erneut Logs checken.

---

# Teil F — Betrieb & Überwachung

## Schritt F1 — Tägliche Kurzchecks

1. Hive: Rig online? Temps ok?  
2. Log: Keys/s noch vorhanden?  
3. Arbeitsordner: Progress-Dateien wachsen?

```bash
cd /hive/miners/custom/btcmole 2>/dev/null || cd /hive/custom/btcmole
ls -lt | head
```

## Schritt F2 — Fund-Datei beobachten

```bash
ls -la /hive/miners/custom/btcmole/FOUND_* 2>/dev/null
ls -la /hive/custom/btcmole/FOUND_* 2>/dev/null
```

Oder im Log nach:

```text
TREASURE KEY FOUND
```

## Schritt F3 — Update von BtcMole

```bash
cd /hive/custom/btcmole
./bm079 upgrade
# danach neue Datei bm080 (o. ä.) nutzen / ins Custom-Paket legen und Tarball-URL aktualisieren
```

Flight Sheet: neue Installation URL setzen oder Extra-Config unverändert lassen, wenn du nur das Binary im Paket getauscht hast.

---

# Teil G — Wenn der Key gefunden wird (kritisch)

## Schritt G1 — Sofort stoppen und sichern

1. Miner/Solver stoppen (Flight Sheet stoppen oder `tmux` beenden).  
2. Found-Datei kopieren:

```bash
cp FOUND_*.txt /home/user/
# zusätzlich auf USB / anderen Rechner
```

3. **Niemals** den Private Key in Chat, Screenshot, Pastebin posten.

## Schritt G2 — Offline importieren

1. Auf einem **offline** Rechner Wallet-Software (z. B. Electrum offline).  
2. Private Key / WIF importieren.  
3. Prüfen, dass die Puzzle-Adresse sichtbar ist und Guthaben stimmt.

## Schritt G3 — Auszahlen ohne Front-Running

Bei Puzzle #66 wurde eine öffentliche Broadcast-TX abgegriffen.

Deshalb:

1. **Nicht** einfach „Send all“ öffentlich in den Mempool werfen.  
2. Private Submission / Miner-Direkt / erprobte Out-of-Band-Methoden nutzen.  
3. Aktuelle Community-Empfehlungen lesen (btcpuzzle FAQ / erfahrene Solver), bevor du sendest.

## Schritt G4 — Aufräumen

1. Found-Dateien von unsicheren Cloud-Syncs entfernen.  
2. Rig-Logs bedenken (Key kann im Klartext im Log stehen).  
3. Bei Pool/RSA-Setup: nur mit deinem Private Key entschlüsseln.

---

# Teil H — Fehlerbehebung (Schritt für Schritt)

## H1 — `./bm079` startet nicht / shared library missing

```bash
ldd ./bm079 | grep "not found"
```

→ fehlende Libs notieren, ROCm/`amd-ocl-install` nachziehen, `ldconfig`.

## H2 — 0 GPUs erkannt

```bash
amd-info
clinfo | grep -i device
echo $HSA_OVERRIDE_GFX_VERSION
```

→ Override setzen (C5), Treiber neu, andere Karte testen.

## H3 — Läuft in SSH, stirbt nach Disconnect

→ tmux (D3) oder Flight Sheet (Teil E).

## H4 — Hive Custom Miner lädt nicht

- URL im Browser testen (muss Direct Download sein)  
- Miner name exakt `btcmole` (wie Ordnername im Tarball-Inhalt)  
- Tarball muss die Dateien **im Root** haben (nicht verschachtelt `btcmole/btcmole/...` falsch packen)

Richtiges Packen:

```bash
cd ~/btcmole-pack
tar -tzvf btcmole-hive-1.0.0.tar.gz
# erwartet Zeilen wie:
# h-manifest.conf
# h-run.sh
# btcmole
# NICHT: btcmole-pack/h-run.sh
```

## H5 — Crash nach wenigen Sekunden (RDNA)

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0   # anpassen
./bm079 bf -pz 30 +amdgpu -cpu
```

## H6 — Sehr niedrige Keys/s

- Powerlimit zu niedrig  
- Falsches Binary (CPU-only statt `_amdgpu`)  
- GPU clockt nicht (Hive OC-Profil prüfen)  
- `-cpu` und iGPU gleichzeitig auf APU → nur `+amdgpu -cpu`

---

# Teil I — Komplette Minimal-Checkliste

Arbeite die Liste von oben nach unten ab und hake ab:

1. [ ] Puzzle-Status auf btcpuzzle.info geprüft (#71 noch offen?)  
2. [ ] HiveOS-Rig online, AMD-GPUs sichtbar  
3. [ ] SSH funktioniert  
4. [ ] `amd-info` / `clinfo` ok  
5. [ ] BtcMole `_amdgpu` entpackt unter `/hive/custom/btcmole`  
6. [ ] Test `bf -pz 30 +amdgpu` → `FOUND_PUZZLE_30.txt`  
7. [ ] Start `bf -pz 71 +amdgpu -cpu`  
8. [ ] (Empfohlen) Custom-Miner-Tarball gebaut & gehostet  
9. [ ] Flight Sheet angelegt, Extra config gesetzt, Apply  
10. [ ] Logs zeigen Keys/s  
11. [ ] Reboot-Test: Solver startet automatisch  
12. [ ] Plan für Fund (Offline-Wallet, keine Public-Broadcast) steht

---

# Teil J — Befehls-Spickzettel

```bash
# GPUs
amd-info
clinfo | head -40

# Installation
cd /hive/custom/btcmole
wget -O bm.zip "https://github.com/keymole/btcmole/raw/main/linux/bm079.linux_amd64_amdgpu.zip"
unzip -o bm.zip && chmod +x bm079

# Tests
./bm079 bf -pz 30 +amdgpu -cpu

# Produktiv Solo
HSA_OVERRIDE_GFX_VERSION=10.3.0 ./bm079 bf -pz 71 +amdgpu -cpu

# Pool
./bm079 dig -s DEINE_SESSION +amdgpu -cpu

# Logs Hive
tail -f /var/log/miner/custom/btcmole.log

# Fund suchen
ls FOUND_* 2>/dev/null
```

---

# Teil K — Realistische Erwartung

- Puzzle #71 hat einen riesigen Suchraum.  
- Auch schnelle AMD-Rigs brauchen **Glück**, nicht nur Zeit.  
- Stromkosten können den Erwartungswert übersteigen.  
- Nutze den Rig nur, wenn du das als experimentelles / Lotterie-Projekt verstehst.

---

# Links

| Was | URL |
|-----|-----|
| Puzzle-Liste | https://btcpuzzle.info/puzzle |
| BtcMole | https://github.com/keymole/btcmole |
| BtcMole Bot | https://t.me/BtcMoleBot |
| HiveOS | https://hiveon.com/os/ |
| Hive Custom Miner Docs | https://hiveon.com/knowledge-base/ |
| Wrapper in diesem Repo | `BitcoinPuzzle/hiveos-btcmole/` |

---

## Haftung

Du bist selbst verantwortlich für Hardware, Strom, Sicherheit der Keys und rechtmäßige Nutzung. Diese Anleitung dient nur der technischen Dokumentation öffentlicher Bitcoin-Puzzle-Adressen.
