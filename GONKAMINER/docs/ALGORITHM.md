# Gonka Proof-of-Compute (PoC) — Algorithm

This document explains **how the Sprint / PoC algorithm works**, based on the Gonka whitepaper, glossary, and open-source `mlnode/packages/pow` implementation.

## Overview

Traditional mining iterates hashes until `hash < target`. Gonka instead runs **transformer neural network forward passes** on deterministic inputs. A nonce “hits” when the model output vector is **close enough** to a public target vector.

```
blockchain state ──► sprint seed (seedS)
                         │
                         ▼
              shared Transformer weights T
                         │
public_key + nonce ──► input embedding ──► forward pass ──► output vector V
                         │                                      │
                         │                              normalize + permute
                         │                                      │
                         └──────────────────► distance(V', VT) < τ ?
                                                    hit → voting weight
```

## Sprint timeline (per epoch)

1. **Epoch (~24 h)** — Hosts serve LLM inference; validations run in parallel.
2. **Sprint start** — Synchronized across all hosts (common random seed from chain).
3. **PoC generation** — ML nodes race through nonces; batches submitted to chain.
4. **PoC validation** — Sampled nonces re-computed by peers; fraud detection.
5. **Weight update** — `SetComputeValidators` applies new voting power.
6. **New epoch** — Repeat.

## Mathematical definition (simplified)

From [pow-security-analysis.pdf](https://gonka.ai/pow-security-analysis.pdf):

- **Sprint seed** `seedS` from blockchain state via PRNG.
- **Node seed** `seedN` from host public key.
- For each **nonce** `n`:
  1. Build input from `(seedS, seedN, n)`.
  2. Run shared transformer `T` → last sequence vector `V_O`.
  3. Normalize `V_O` to unit length.
  4. Apply nonce-dependent **permutation** `P` → `V'`.
  5. Compare `‖V' - V_T‖₂` to threshold `τ`.
  6. If distance `< τ` → **appropriate vector** (valid hit).

Expected hits ∝ nonces evaluated ∝ raw GPU throughput.

## Implementation (gonka-ai/gonka)

Core files in `mlnode/packages/pow/src/pow/`:

| File | Role |
|------|------|
| `random.py` | Deterministic embeddings, permutations, target sphere vector |
| `models/llama31.py` | Llama-style transformer (~5.5B params default) |
| `models/utils.py` | `Params`, `PARAMS_V1`, `PARAMS_V2` |
| `compute/compute.py` | Batch forward, distance, `ProofBatch` |
| `compute/gpu_group.py` | GPU grouping, VRAM checks |
| `compute/controller.py` | Multiprocess workers per GPU group |
| `service/routes.py` | REST API `/api/v1/pow/*` |

### PARAMS versions

| Version | Approx size | Min VRAM (group) | seq_len |
|---------|-------------|------------------|---------|
| **PARAMS_V1** | ~1–2B scale | **10 GB** | 128 |
| **PARAMS_V2** | Larger | **38 GB** | 256 |
| Default `Params` | ~5.5B | ~10+ GB FP16 | 16 |

Weights are **not downloaded** — they are **deterministically initialized** from the sprint `block_hash` (see `random_pool_optimized`, `ModelWrapper.build`).

### Proof batch

```python
ProofBatch(
  public_key, block_hash, block_height,
  nonces: List[int],
  dist: List[float],   # L2 distance per nonce
  node_id
)
```

Valid submissions filter `dist < r_target`.

## API workflow (ML node)

Orchestrated by the **API node**; ML worker exposes:

| Endpoint | Purpose |
|----------|---------|
| `POST /api/v1/pow/init` | Load model for sprint |
| `POST /api/v1/pow/init/generate` | Start generation phase |
| `POST /api/v1/pow/phase/generate` | Resume generation |
| `POST /api/v1/pow/validate` | Validate peer batch |
| `GET /api/v1/pow/status` | Worker state |
| `POST /api/v1/pow/stop` | Stop sprint |

Callback URL in init request sends batches back to API node → chain (`MsgSubmitPocBatch`).

## Validation & fraud

- Peers re-run selected nonces; compare distances.
- Statistical test (`ValidatedBatch` in `data.py`) flags fraud.
- Dishonest hosts risk collateral slash.

## AMD / ROCm notes

Upstream code uses `torch.cuda.*` (NVIDIA naming). **ROCm PyTorch** exposes the same CUDA-compatible API on HiveOS when `libamdhip64` and `HSA_OVERRIDE_GFX_VERSION` are set.

GONKAMINER vendors upstream PoC code unchanged except the FastAPI entrypoint. Requirements:

- PyTorch built for ROCm
- ≥10 GB VRAM for PARAMS_V1 groups
- Network connectivity to your Gonka API node

## Further reading

- [gonka_poc.md](https://github.com/gonka-ai/gonka/blob/main/docs/gonka_poc.md) — full workflow with chain integration
- [Glossary](https://gonka.ai/docs/glossary/) — Sprint, voting power, seeds
