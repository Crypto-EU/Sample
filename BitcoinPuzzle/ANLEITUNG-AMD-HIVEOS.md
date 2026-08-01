# Bitcoin Puzzle mit AMD-GPUs auf HiveOS — vollständige Anleitung

Stand: August 2026

Diese Anleitung erklärt, wie du mit **AMD-Grafikkarten** unter **HiveOS** am **Bitcoin Puzzle** teilnimmst — von der Erklärung bis zum laufenden Worker.

> **Wichtig:** Das ist **kein Mining** im klassischen Sinn (kein Stratum, kein Hashrate für Blöcke). Du suchst einen **Private Key in einem bekannten Hex-Bereich**. Nur wer den Key findet, bekommt die BTC-Belohnung (Winner-takes-all).

---

## 1. Was ist das Bitcoin Puzzle?

2015 hat ein anonymer Ersteller ~**1000 BTC** auf **160 Adressen** verteilt. Puzzle Nr. `n` hat einen Private Key im Bereich:

```text
2^(n-1)  …  2^n − 1
```

Je höher die Nummer, desto größer der Suchraum (jedes Puzzle ist ~2× schwerer als das vorherige).

| Status (Aug 2026) | Details |
|-------------------|---------|
| Gelöst | u. a. #1–#70 sowie jedes 5. Puzzle bis #130 |
| Leichtestes ungelöstes Brute-Force-Ziel | **Puzzle #71** (~7,1 BTC) |
| Beispiel Kangaroo-Ziel (Public Key bekannt) | z. B. **#135** |

Aktuelle Liste immer prüfen:

- https://btcpuzzle.info/puzzle
- https://privatekeyfinder.io/bitcoin-puzzle/

### Puzzle #71 (aktuelles Haupttziel)

| Feld | Wert |
|------|------|
| Belohnung | ~7,1 BTC |
| Keyspace | `0x400000000000000000` … `0x7fffffffffffffffff` |
| Typ | nur Adresse bekannt → **Brute-Force / BSGS** |
| Chance | ~33,5 Mio. „Ranges“ à ~35 TKeys — Lotterie-Charakter |

---

## 2. AMD vs. NVIDIA — was geht unter HiveOS?

| Weg | AMD? | HiveOS? | Empfehlung |
|-----|------|---------|------------|
| **BtcMole** (`_amdgpu`, ROCm/HIP) | ✅ ja | ✅ ja (Custom Miner / Shell) | **Beste Wahl für AMD** |
| **btcpuzzle.info Client** (VanitySearch/CUDA) | ❌ offiziell nur NVIDIA | ✅ | Nicht für reine AMD-Rigs |
| **clBitCrack** (OpenCL) | ⚠️ experimentell | ✅ möglich | Fallback, oft instabil |
| **Kangaroo** (Vulkan/wgpu) | ✅ | ✅ | Nur wenn **Public Key** bekannt (#135 etc.) |
| CloudSearch (btcpuzzle.info) | meist NVIDIA-Instanzen | — | Einfach, aber Mietkosten |

**Fazit für AMD-HiveOS:**  
→ **BtcMole** (Solo `bf` oder Pool `dig`) **oder** eigener Offline-Scan mit BitCrack/OpenCL.  
→ Der große Solo-Pool **btcpuzzle.info** ist für AMD derzeit **nicht** der Primärweg (CUDA-Client).

---

## 3. Voraussetzungen

### Hardware

- AMD-GPU(s): Polarish / Vega / RDNA1–4 (RX 400/500, 5000, 6000, 7000, …)
- Stabiler HiveOS-Rig (NTP/Zeit synchron)
- Genug PSU und Kühlung (Dauerlast wie beim Mining)

### HiveOS

1. Rig in HiveOS registriert und online
2. AMD-Treiber/OpenCL ok (`amd-info`, `clinfo` sollten GPUs zeigen)
3. SSH-Zugang zum Rig (für Setup sehr hilfreich)

### Software-Kenntnisse

- Flight Sheet / Custom Miner
- SSH (`ssh user@rig-ip`)
- Dateien editieren (`nano` / `vim`)

### Realistische Erwartung

Bei Puzzle #71 brauchst du selbst mit starker Hashrate **Glück** (viele Ranges, Winner-takes-all). Plane Stromkosten ein; es ist eher **Lotterie mit GPU** als „sicheres Einkommen“.

---

## 4. Sicherheit — bevor du startest

1. **Wenn der Key gefunden wird:** Sofort offline sichern, **nicht** die Auszahlung öffentlich im Mempool broadcasten (bei #66 wurde der Preis abgegriffen / front-run). Private Relay / Miner-Direkt-Submission nutzen.
2. **Untrusted Computer:** RSA Public Key nutzen (Key wird verschlüsselt gemeldet) — bei BtcMole/Pool-Anleitungen der jeweiligen Software folgen.
3. **Keine Screenshots** vom gefundenen Private Key in Discord/Telegram-Gruppen.
4. **Backup:** Found-Datei (`FOUND_*.txt`) sofort auf sicheren Offline-Speicher kopieren.
5. Nur an **öffentlichen Puzzle-Adressen** arbeiten — niemals fremde Wallets „knacken“.

---

## 5. Empfohlener Weg: BtcMole auf HiveOS (AMD)

BtcMole ist ein schneller Cross-Platform-Solver mit **AMD via ROCm/HIP**.

- Repo: https://github.com/keymole/btcmole  
- AMD-Linux-Build: `linux/bmXXX.linux_amd64_amdgpu.zip` (aktuell z. B. `bm079`)  
- Telegram: https://t.me/BtcMole · Support: @BtcMoleSup  

### 5.1 Zwei Betriebsarten

| Modus | Befehl | Netzwerk | Zweck |
|-------|--------|----------|-------|
| Offline Brute-Force | `bmXXX bf -pz 71 +amdgpu` | nein | Solo auf Puzzle #71 |
| Pool Dig | `bmXXX dig -s <session>` | ja | Kooperativer Pool (Session über @BtcMoleBot) |

### 5.2 ROCm unter HiveOS vorbereiten

BtcMole braucht auf Linux die AMD-GPU-Runtime (ROCm/HIP). Auf vielen HiveOS-Rigs:

```bash
# SSH auf den Rig
amd-info
clinfo | head -40

# Falls ROCm fehlt / veraltet (Beispiel HiveOS 0.6.x):
amd-ocl-install 5.7 5.7
# oder die von HiveOS angebotenen ROCm-Pakete gemäß deiner Image-Version
```

Danach prüfen:

```bash
ls /opt/rocm/lib/libamdhip64.so* 2>/dev/null
ldconfig -p | grep -i hip
```

Bei RDNA2/3-Karten ggf. nötig:

```bash
# Beispiel — an deine GPU anpassen:
export HSA_OVERRIDE_GFX_VERSION=10.3.0   # RX 6800/6900
# export HSA_OVERRIDE_GFX_VERSION=10.1.0 # RX 5700 XT
# export HSA_OVERRIDE_GFX_VERSION=11.0.0 # RX 7900
```

### 5.3 Binary installieren (Shell-Test)

```bash
cd /tmp
# Versionsnummer anpassen (siehe GitHub linux/ Ordner)
wget -O bm.zip \
  "https://github.com/keymole/btcmole/raw/main/linux/bm079.linux_amd64_amdgpu.zip"
unzip -o bm.zip
chmod +x bm079
./bm079 --help || ./bm079

# Selbsttest mit gelöstem Puzzle (sollte schnell einen Key finden):
./bm079 bf -pz 30 +amdgpu
# → FOUND_PUZZLE_30.txt erscheint
```

### 5.4 Puzzle #71 solo starten

```bash
mkdir -p /hive/custom/btcmole && cd /hive/custom/btcmole
# Binary hierher legen
./bm079 bf -pz 71 +amdgpu -cpu
```

Hinweise:

- Progress-Dateien (`bf_*.map`, `*.state`) liegen im Arbeitsverzeichnis — **nicht löschen**.
- Nach Neustart einfach denselben Befehl erneut → Resume.
- Mehrere GPUs: BtcMole erkennt AMD-Karten und verteilt Sectors.

### 5.5 Pool-Modus (`dig`)

1. Telegram: [@BtcMoleBot](https://t.me/BtcMoleBot) → Session-ID holen  
2. Starten:

```bash
./bm079 dig -s DEINE_SESSION +amdgpu
```

Bei Desktop-/Monitor-GPU Last drosseln:

```bash
./bm079 dig -s DEINE_SESSION +amdgpu:90% -cpu
```

---

## 6. HiveOS Custom Miner (Flight Sheet)

Damit der Solver nach Reboot wieder läuft und im Hive-UI erscheint, nutzt du ein **Custom-Miner-Paket**.

### 6.1 Paketstruktur

```text
btcmole/
  btcmole                 # oder bm079 → als btcmole verlinken
  h-manifest.conf
  h-config.sh
  h-run.sh
  h-stats.sh
  run.sh                  # optional
```

Die Vorlagen liegen in diesem Repo unter:

```text
BitcoinPuzzle/hiveos-btcmole/
```

### 6.2 Paket bauen & hosten

```bash
cd BitcoinPuzzle/hiveos-btcmole
# Binary aus BtcMole-Release als ./btcmole ablegen (oder bm079 → symlink)
chmod +x h-*.sh btcmole 2>/dev/null || true
tar -czvf btcmole-hive-1.0.0.tar.gz \
  h-manifest.conf h-config.sh h-run.sh h-stats.sh btcmole
```

Das `.tar.gz` irgendwo per **direkter HTTPS-URL** hosten (GitHub Release, eigener Server). HiveOS muss die Datei ohne Login laden können.

### 6.3 Flight Sheet anlegen

In HiveOS → Farm → **Flight Sheets** → Add:

| Feld | Wert |
|------|------|
| Coin | (egal / Custom) |
| Wallet | deine Notiz oder `%WAL%` (wird bei Puzzle oft ignoriert) |
| Pool | `btc-puzzle` (Platzhalter) |
| Miner | **Custom** → Setup Miner Config |
| Miner name | `btcmole` |
| Installation URL | `https://…/btcmole-hive-1.0.0.tar.gz` |
| Extra config | siehe unten |

**Extra config Beispiele:**

```bash
# Solo Puzzle 71, alle AMD-GPUs
bf -pz 71 +amdgpu

# Pool Dig
dig -s DEINE_SESSION +amdgpu

# Mit GFX-Override für RX 6800 XT
HSA_OVERRIDE_GFX_VERSION=10.3.0 bf -pz 71 +amdgpu
```

Flight Sheet auf den Rig **apply**/starten. Logs prüfen:

```bash
miner log
# oder
tail -f /var/log/miner/custom/btcmole.log
```

### 6.4 Stats

`h-stats.sh` meldet eine einfache Hashrate-Schätzung aus dem Log (Keys/s). Das ist **kein** klassischer Mining-Algo — Hive zeigt ggf. „Custom“.

---

## 7. Alternative A: Offline mit clBitCrack (OpenCL)

Falls ROCm/BtcMole Probleme macht:

1. BitCrack OpenCL bauen/beziehen (`clBitCrack`) — oft **v0.30** als stabilste AMD-Variante genannt  
2. Keyspace für Puzzle #71 setzen:

```bash
# Adresse von btcpuzzle.info / privatekeyfinder für Puzzle 71 übernehmen!
./clBitCrack --keyspace 400000000000000000:7fffffffffffffffff TARGET_ADDRESS
```

Nachteile: experimentell auf manchen AMD-Karten, kein modernes Pool-Protokoll, langsamere Pflege als BtcMole.

---

## 8. Alternative B: Kangaroo (Public-Key-Puzzles)

Für Puzzles mit **bekanntem Public Key** (z. B. #135) ist Pollard's Kangaroo deutlich effizienter als Blind-Brute-Force.

- Beispiel (Vulkan/AMD): https://github.com/oritwoen/kangaroo  
- Klassiker (CUDA): JeanLucPons/Kangaroo, RCKangaroo  

Unter HiveOS: Binary per SSH starten oder als Custom Miner packen. Mesa/Vulkan-Treiber müssen stimmen (bei AMD RADV oft Mesa ≥ 25.x nötig).

---

## 9. Alternative C: btcpuzzle.info (nur mit NVIDIA oder Cloud)

Offizielle Doku: https://btcpuzzle.info/how-to-join-pool  

1. Account auf btcpuzzle.info → **User Token**  
2. Client: https://github.com/ilkerccom/btcpuzzle  
3. Docker (CUDA 12.8): `ilkercndk/btcpuzzle:latest`  

**Hinweis der Pool-Seite:** „At a minimum, you need to have an Nvidia graphics card.“  
Mit **reinen AMD-Rigs** ist das kein Primärweg — außer du mietest NVIDIA über CloudSearch.

Testpool: Puzzle **#38** (zum RSA-/Notify-Test).

---

## 10. Checkliste „läuft korrekt?“

- [ ] `clinfo` / `amd-info` zeigt alle GPUs  
- [ ] Selbsttest `bf -pz 30` findet Key und schreibt `FOUND_PUZZLE_30.txt`  
- [ ] Bei #71: Keys/s im Log sichtbar, Progress-Map wächst  
- [ ] Temperaturen/Powerlimits in HiveOS gesetzt (z. B. Core/Mem wie beim Eth-Mining-Tuning, aber für Compute)  
- [ ] Watchdog: Flight Sheet auto-restart bei Crash  
- [ ] Found-Pfad bekannt und regelmäßig geprüft  

### Typische Fehler

| Symptom | Ursache / Fix |
|---------|----------------|
| Binary startet, 0 GPU | Falsches ZIP (nicht `_amdgpu`); ROCm fehlt |
| HIP/HSA Fehler gfx10xx | `HSA_OVERRIDE_GFX_VERSION` setzen |
| Sofort Crash OpenCL | clBitCrack experimentell → BtcMole versuchen |
| Kein Resume nach Reboot | Arbeitsverzeichnis gewechselt / Map gelöscht |
| Hive zeigt 0 H/s | Stats-Parser; Prozess trotzdem im `miner log` prüfen |

---

## 11. Was tun beim Fund?

1. Solver stoppen, `FOUND_*.txt` sichern (mehrere Kopien, offline).  
2. Private Key → WIF/Wallet **offline** importieren (Electrum offline, air-gapped).  
3. Auszahlung **nicht** öffentlich im Mempool: private Relay / Pool-Miner / out-of-band.  
4. Niemals den Key posten „zur Bestätigung“.  

---

## 12. Kurz-Fahrplan (AMD + HiveOS)

```text
1. HiveOS-Rig mit AMD online
2. SSH → ROCm/OpenCL prüfen
3. BtcMole _amdgpu laden, Test: bf -pz 30 +amdgpu
4. Custom-Miner-Paket bauen & hosten
5. Flight Sheet: Extra = "bf -pz 71 +amdgpu"
6. Logs + Temps überwachen
7. Optional: dig -s SESSION für BtcMole-Pool
```

---

## 13. Links

| Ressource | URL |
|-----------|-----|
| Puzzle-Status | https://btcpuzzle.info/puzzle |
| Pool-Guide (NVIDIA) | https://btcpuzzle.info/how-to-join-pool |
| BtcMole | https://github.com/keymole/btcmole |
| BitCrack | https://github.com/brichard19/BitCrack |
| Kangaroo (Vulkan/AMD) | https://github.com/oritwoen/kangaroo |
| HiveOS Custom Miner | https://hiveon.com/knowledge-base/ |
| Original Puzzle-TX | Blockchain-Explorer → Tx `08389f34…` |

---

## Lizenz / Haftung

Diese Anleitung ist reine Dokumentation. Puzzle-Suche kann hohe Stromkosten verursachen und keinen Fund garantieren. Du bist selbst für Legalität, Sicherheit der Keys und den Betrieb deiner Hardware verantwortlich.
