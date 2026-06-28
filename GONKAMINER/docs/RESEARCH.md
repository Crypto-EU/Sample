# Gonka (GNK) — Research Summary

*Compiled June 2026 from [gonka.ai](https://gonka.ai), [gonka-ai/gonka](https://github.com/gonka-ai/gonka), and public docs.*

## What is Gonka?

**Gonka** is a decentralized AI compute network. The native token **GNK** (also written *gonka*) has a fixed supply of **1 billion**. Hosts earn GNK by contributing GPU compute — not by hashing meaningless PoW like Bitcoin.

| Aspect | Detail |
|--------|--------|
| Website | https://gonka.ai |
| GitHub | https://github.com/gonka-ai/gonka |
| Consensus | **Proof of Compute (PoC)** / “Proof of Work 2.0” |
| Epoch | ~24 h (17 280 blocks) |
| Sprint | Short synchronized compute race at epoch boundary |
| Emission | ~323 000 GNK/epoch, halving ~every 4 years |

## Network architecture

Three cooperating node types:

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│ Inference Chain │◄───►│ Decentralized API │◄───►│    ML Node      │
│  (Cosmos SDK)   │     │  (Go, orchestrator)│     │ Python/CUDA/vLLM│
└─────────────────┘     └──────────────────┘     └─────────────────┘
```

1. **Chain node** — Cosmos-based blockchain, PoC batch/validation messages, validator weights.
2. **API node** — Orchestrates PoC sprints, inference routing, callbacks to ML nodes.
3. **ML node** — Runs transformer PoC and (separately) vLLM inference for production models.

Official deployment: Docker Compose in `gonka-ai/gonka/deploy/join/`.

## How you earn GNK

- **Not** “connect wallet to pool and mine.”
- Register as a **Host**, run chain + API + ML infrastructure.
- **PoC Sprint** determines your **voting weight** (consensus power) for the next epoch.
- Between sprints, hosts run **real LLM inference** (Qwen3-235B, Kimi K2.6, MiniMax M2.7, …).
- Rewards scale with PoC weight and inference work; collateral rules apply for fraud.

## Official hardware requirements

From [Host Quickstart](https://gonka.ai/docs/host/quickstart/):

| Component | Requirement |
|-----------|-------------|
| GPU | **NVIDIA** A100 / H100 / H200 / B200 (datacenter) |
| CUDA | 12.6–12.9 via NVIDIA Container Toolkit |
| CPU | 16+ cores amd64 |
| RAM | 64+ GB (network node); 1.5× GPU VRAM for ML |
| Disk | 1 TB NVMe |
| Inference VRAM | 320–640+ GB per ML node for current flagship models |

**AMD GPUs are not listed** in official documentation. Inference stack is **vLLM + CUDA**.

## PoC vs inference

| Phase | Work | Hardware |
|-------|------|------------|
| **Sprint (PoC)** | ~5.5B–larger transformer forward passes on random nonces | Lighter than full Qwen3-235B; PARAMS_V1 needs ~10 GB VRAM |
| **Epoch work** | Production LLM inference | H100/H200 clusters |

GONKAMINER targets **Sprint PoC only** on AMD ROCm PyTorch.

## Token & economics

- Fixed 1B GNK; ~68% to hosts over time.
- Weight from PoC → governance + inference allocation.
- Fraud on validation → collateral slash (~20% cited in ecosystem articles).
- GNK tradeable (e.g. Uniswap per gonka.ai).

## Ecosystem status (2026)

- Mainnet live; Kimi K2.6 & MiniMax M2.7 on network.
- PoC v2 compliance enforced from epoch 155+.
- Large investment reported ($80M+ in third-party articles).
- Competing “useful work” projects: Bittensor, Akoya, Prime Intellect.

## Implications for AMD HiveOS

| Feasible | Not feasible (today) |
|----------|----------------------|
| Run vendored Gonka **PoC API** on ROCm PyTorch | Official vLLM inference on AMD for 235B models |
| PARAMS_V1 sprint on 16–24 GB cards | PARAMS_V2 (38 GB+ groups) on consumer AMD |
| HiveOS worker registered to remote API node | Full Gonka host on HiveOS alone (needs chain + API elsewhere) |

## References

- [gonka_poc.md](https://github.com/gonka-ai/gonka/blob/main/docs/gonka_poc.md)
- [pow-security-analysis.pdf](https://gonka.ai/pow-security-analysis.pdf)
- [FAQ](https://gonka.ai/FAQ/)
- [Tokenomics PDF](https://gonka.ai/tokenomics.pdf)
