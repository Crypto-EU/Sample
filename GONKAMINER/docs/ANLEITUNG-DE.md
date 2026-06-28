# GONKAMINER — Deutsche Anleitung (HiveOS / AMD)

**GONKAMINER v0.1.1** — Community-PoC-Worker für das [Gonka](https://gonka.ai)-Netzwerk auf **AMD-GPUs** unter HiveOS.

## Wichtig vorab

Gonka ist **kein klassischer Pool-Miner** wie Pearl oder Ethereum. Du brauchst:

1. **Gonka Network + API Node** (offizielles Docker-Setup auf einem Linux-Server/VPS)
2. **GONKAMINER auf HiveOS** (dieser PoC-Worker auf der GPU-Rig)
3. Registrierung der Rig-IP im `node-config.json` der API-Node

Ohne API-Node startet GONKAMINER zwar den PoC-Dienst, erhält aber **keine Sprint-Jobs** und verdient **kein GNK**.

**Inference** (Qwen3-235B, Kimi K2.6) läuft offiziell nur auf **NVIDIA H100/H200** — nicht auf AMD.

## Voraussetzungen

| Komponente | Minimum |
|-----------|---------|
| GPU | AMD RX 6800 XT / 7900 XT (16–24 GB VRAM) |
| HiveOS | AMD-Image mit ROCm-Treiber |
| VRAM | ≥10 GB frei für PoC PARAMS_V1 |
| Server | Separater Linux-Host für `gonka-ai/gonka` deploy/join |
| Wallet | Gonka/Cosmos-Wallet (Host-Registrierung) |

## Schritt 1 — Offiziellen Gonka-Host vorbereiten

Auf einem **separaten Server** (nicht HiveOS):

```bash
git clone https://github.com/gonka-ai/gonka.git
cd gonka/deploy/join
# .env, node-config.json, Keys gemäß https://gonka.ai/docs/host/quickstart/
docker compose up -d
```

Dokumentation: https://gonka.ai/docs/host/quickstart/

## Schritt 2 — GONKAMINER auf HiveOS installieren

1. Flight Sheet JSON importieren: `hiveos/gonkaminer-gonka-flightsheet.json`
2. **Custom miner** → `gonkaminer`
3. Install-URL: Release-Tarball von GitHub (nach Veröffentlichung)
4. **Extra config:**

```
HSA_OVERRIDE_GFX_VERSION=10.3.0 GONKAMINER_PORT=8080
```

| GPU | HSA_OVERRIDE_GFX_VERSION |
|-----|--------------------------|
| RX 6000 (6800 XT) | 10.3.0 |
| RX 7000 (7900 XTX) | 11.0.0 |

## Schritt 3 — ML-Node bei der API registrieren

Auf dem **Network-Node-Server** die Hive-Rig als Inference/PoC-Node eintragen:

```bash
curl -X POST http://localhost:9200/admin/v1/nodes \
  -H "Content-Type: application/json" \
  -d '{
    "id": "hive-rig-01",
    "host": "DEINE_RIG_OEFFENTLICHE_IP",
    "inference_port": 8080,
    "poc_port": 8080,
    "max_concurrent": 1,
    "models": {}
  }'
```

`poc_port` muss auf GONKAMINER zeigen (Standard **8080**). Details: https://gonka.ai/docs/host/multiple-nodes/

## Schritt 4 — Starten & Logs prüfen

HiveOS startet `h-run.sh` → Python venv → `uvicorn` auf Port 8080.

Log: `/var/log/miner/custom/gonkaminer.log`

Erwartete Zeilen nach Sprint-Init durch API-Node:

```
Model initialized ...
Phase changed to: GENERATE
```

Health-Check von einem anderen Rechner:

```bash
curl http://RIG-IP:8080/health
curl http://RIG-IP:8080/api/v1/pow/status
```

## PoC-Algorithmus (Kurzfassung)

Siehe [ALGORITHM.md](ALGORITHM.md) und [RESEARCH.md](RESEARCH.md).

- Sprint = synchrones Rennen mit **Transformer-Inferenz** auf Nonces
- Treffer = Ausgabevektor nahe genug am Zielvektor (Euklidische Distanz)
- Gewichtung ∝ Anzahl gültiger Nonces pro Sprint

## Fehlerbehebung

### `CUDA is not available`

- v0.1.1 installiert automatisch **ROCm-PyTorch** (`scripts/install-rocm-torch.sh`) — nicht das NVIDIA-CUDA-Wheel von pip
- `HSA_OVERRIDE_GFX_VERSION=10.3.0` für RX 6800 XT (gfx1030)
- `rocm-smi` muss GPUs anzeigen
- Preflight-Log prüfen: `/var/log/miner/custom/gonkaminer.log`

### `Not enough GPU memory`

- PARAMS_V2 braucht ~38 GB — Consumer-AMD unterstützt nur **PARAMS_V1** (~10 GB)
- Andere GPU-Prozesse beenden

### Keine Sprint-Jobs

- API-Node läuft?
- Rig-IP in `node-config.json` korrekt?
- Firewall: Port 8080 von API-Server zur Rig offen?

### Kein GNK auf Wallet

- Epoch-Rewards erst nach vollständigem Host-Setup und erfolgreichem Sprint
- Collateral/Registrierung prüfen (offizielle FAQ)

## Links

| Ressource | URL |
|-----------|-----|
| Gonka | https://gonka.ai |
| GitHub | https://github.com/gonka-ai/gonka |
| PoC-Doku | https://github.com/gonka-ai/gonka/blob/main/docs/gonka_poc.md |
| GONKAMINER Repo | https://github.com/Crypto-EU/Sample/tree/main/GONKAMINER |

## Haftungsausschluss

Community-Projekt, nicht offiziell von Gonka AI. GNK-Mining ist experimentell; Hardware- und Netzwerkanforderungen können sich ändern.
