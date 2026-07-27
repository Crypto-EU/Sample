"""SASEUL classic PoW helpers (aligned with 1Miner / hasher)."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from typing import List, Optional


def sha256_hex(data: bytes | str) -> str:
    if isinstance(data, str):
        data = data.encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def hextime_us(utime: int) -> str:
    """Matches hasher / SASEUL Enc.hextime: %014llx lower-case, clipped to 14 chars."""
    out = f"{utime & ((1 << 56) - 1):014x}"
    if len(out) > 14:
        out = out[-14:]
    return out


def receipt_root_hex(receipts: Optional[List[str]] = None) -> str:
    receipts = receipts or []
    if not receipts:
        return sha256_hex(b"")
    layer = [sha256_hex(r) for r in receipts]
    while len(layer) > 1:
        nxt: List[str] = []
        for i in range(0, len(layer), 2):
            if i + 1 < len(layer):
                nxt.append(sha256_hex(layer[i] + layer[i + 1]))
            else:
                nxt.append(layer[i])
        layer = nxt
    return layer[0] if layer else ("0" * 64)


def build_header_hash_hex(
    *,
    height: int,
    timestamp_us: int,
    main_height: int,
    main_blockhash: str,
    validator: str,
    miner: str,
    receipts: Optional[List[str]] = None,
) -> str:
    # Exact hasher HeaderHashPlan layout (no JSON spaces).
    rr = receipt_root_hex(receipts)
    s = (
        f'{{"height":{height},"timestamp":{timestamp_us},'
        f'"receipt_root":"{rr}","main_height":{main_height},'
        f'"main_blockhash":"{main_blockhash}","validator":"{validator}",'
        f'"miner":"{miner}"}}'
    )
    return sha256_hex(s)


def root_hex_from_parts(previous_blockhash: str, header_hash_hex: str, nonce16: str) -> str:
    return sha256_hex(previous_blockhash + header_hash_hex + nonce16)


def blockhash_from_root(root64_hex: str, timestamp_us: int) -> str:
    return hextime_us(timestamp_us) + sha256_hex(root64_hex)


def target_from_difficulty(difficulty: int) -> bytes:
    """Return 32-byte big-endian target = 2^256 / difficulty (approx via integer div)."""
    if difficulty <= 1:
        return b"\xff" * 32
    # quotient = 2^256 // difficulty
    q = (1 << 256) // int(difficulty)
    return q.to_bytes(32, "big")


def hash_meets_target(digest32: bytes, target32: bytes) -> bool:
    return digest32 <= target32


def root_bytes_from_hex(root_hex: str) -> bytes:
    return bytes.fromhex(root_hex)


@dataclass
class MiningJob:
    job_id: str
    previous_blockhash: str
    job_digest: str = ""
    main_blockhash: str = ""
    validator: str = ""
    miner: str = ""
    receipts: List[str] = field(default_factory=list)
    height: int = 0
    main_height: int = 0
    share_difficulty: int = 1
    network_difficulty: int = 1
    difficulty: str = "1"
    worktime: int = 0
    vardiff: bool = True
    upstream_raw: Optional[dict] = None

    @property
    def digest64(self) -> str:
        d = self.job_digest
        at = d.find("@")
        return d if at < 0 else d[:at]


def verify_classic_share(
    job: MiningJob,
    nonce_hex: str,
    timestamp_us: int,
    blockhash_hex: str,
    *,
    max_drift_us: int = 5_000_000,
    now_us: Optional[int] = None,
) -> tuple[bool, str]:
    """Validate a classic share against the job. Returns (ok, error)."""
    if len(job.previous_blockhash) != 78:
        return False, "bad previous_blockhash"
    if not nonce_hex or not all(c in "0123456789abcdefABCDEF" for c in nonce_hex):
        return False, "bad nonce"
    if len(blockhash_hex) != 78:
        return False, "bad blockhash length"

    now = now_us if now_us is not None else int(__import__("time").time() * 1_000_000)
    # Prefer pool worktime as clock reference when present.
    ref = job.worktime if job.worktime else now
    drift = abs(int(timestamp_us) - int(ref))
    if drift > max_drift_us:
        return False, f"bad timestamp drift={drift} limit={max_drift_us} now={ref}"

    header = build_header_hash_hex(
        height=job.height,
        timestamp_us=int(timestamp_us),
        main_height=job.main_height,
        main_blockhash=job.main_blockhash,
        validator=job.validator,
        miner=job.miner,
        receipts=job.receipts,
    )
    root = root_hex_from_parts(job.previous_blockhash, header, nonce_hex)
    expect_bh = blockhash_from_root(root, int(timestamp_us))
    if expect_bh != blockhash_hex.lower():
        return False, "blockhash mismatch"

    target = target_from_difficulty(max(1, int(job.share_difficulty or 1)))
    if not hash_meets_target(bytes.fromhex(root), target):
        return False, "above target"

    return True, "ok"


def job_to_result(job: MiningJob) -> dict:
    return {
        "blockhash": "",
        "difficulty": str(job.difficulty or job.share_difficulty),
        "height": job.height,
        "job_digest": job.job_digest or (job.digest64 + "@1pool"),
        "job_id": job.job_id,
        "main_blockhash": job.main_blockhash,
        "main_height": job.main_height,
        "miner": job.miner,
        "network_difficulty": str(job.network_difficulty),
        "nonce": "",
        "previous_blockhash": job.previous_blockhash,
        "receipts": job.receipts,
        "share_difficulty": int(job.share_difficulty),
        "timestamp": 0,
        "worktime": int(job.worktime or 0),
        "validator": job.validator,
        "vardiff": bool(job.vardiff),
    }


def parse_job_from_upstream(payload: dict) -> MiningJob:
    """Parse getjob response (flat or nested under result)."""
    result = payload.get("result")
    if isinstance(result, dict):
        src = result
    else:
        src = payload
    digest = str(src.get("job_digest") or src.get("digest") or "")
    share_diff = src.get("share_difficulty") or src.get("difficulty") or 1
    try:
        share_diff_i = int(share_diff)
    except (TypeError, ValueError):
        share_diff_i = 1
    net_diff = src.get("network_difficulty") or share_diff_i
    try:
        net_diff_i = int(net_diff)
    except (TypeError, ValueError):
        net_diff_i = share_diff_i
    worktime = src.get("worktime") or src.get("work_time") or src.get("workTime") or src.get("timestamp") or 0
    try:
        worktime_i = int(worktime)
    except (TypeError, ValueError):
        worktime_i = 0
    prev = str(src.get("previous_blockhash") or "")
    job_id = str(src.get("job_id") or prev)
    receipts = src.get("receipts") or []
    if not isinstance(receipts, list):
        receipts = []
    return MiningJob(
        job_id=job_id,
        previous_blockhash=prev,
        job_digest=digest,
        main_blockhash=str(src.get("main_blockhash") or ""),
        validator=str(src.get("validator") or ""),
        miner=str(src.get("miner") or ""),
        receipts=[str(x) for x in receipts],
        height=int(src.get("height") or 0),
        main_height=int(src.get("main_height") or 0),
        share_difficulty=share_diff_i,
        network_difficulty=net_diff_i,
        difficulty=str(src.get("difficulty") or share_diff_i),
        worktime=worktime_i,
        vardiff=bool(src.get("vardiff", True)),
        upstream_raw=src if isinstance(src, dict) else None,
    )


def dumps_compact(obj: object) -> str:
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)
