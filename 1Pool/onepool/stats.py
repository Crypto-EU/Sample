"""Worker / share accounting."""

from __future__ import annotations

import json
import os
import threading
import time
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional


def now_us() -> int:
    return int(time.time() * 1_000_000)


@dataclass
class WorkerStats:
    wallet: str
    worker: str
    accepted: int = 0
    rejected: int = 0
    invalid: int = 0
    last_share_us: int = 0
    connected_us: int = 0
    shares_diff: float = 0.0  # sum of accepted share difficulties
    hashrate_est: float = 0.0
    agent: str = ""

    @property
    def key(self) -> str:
        return f"{self.wallet}.{self.worker}"


@dataclass
class PoolStats:
    started_us: int = field(default_factory=now_us)
    accepted: int = 0
    rejected: int = 0
    invalid: int = 0
    forwarded_ok: int = 0
    forwarded_fail: int = 0
    blocks_found: int = 0
    active_connections: int = 0
    current_job_id: str = ""
    current_height: int = 0
    upstream_ok: bool = False
    upstream_endpoint: str = ""
    workers: Dict[str, WorkerStats] = field(default_factory=dict)
    recent: List[dict] = field(default_factory=list)
    _lock: threading.RLock = field(default_factory=threading.RLock, repr=False)

    def ensure_worker(self, wallet: str, worker: str) -> WorkerStats:
        key = f"{wallet}.{worker}"
        with self._lock:
            w = self.workers.get(key)
            if w is None:
                w = WorkerStats(wallet=wallet, worker=worker, connected_us=now_us())
                self.workers[key] = w
            return w

    def note_connect(self, delta: int = 1) -> None:
        with self._lock:
            self.active_connections = max(0, self.active_connections + delta)

    def record_share(
        self,
        *,
        wallet: str,
        worker: str,
        ok: bool,
        reason: str,
        difficulty: int,
        forwarded: Optional[bool] = None,
        is_block: bool = False,
    ) -> None:
        with self._lock:
            w = self.ensure_worker(wallet, worker)
            w.last_share_us = now_us()
            entry = {
                "ts": now_us(),
                "wallet": wallet,
                "worker": worker,
                "ok": ok,
                "reason": reason,
                "difficulty": difficulty,
                "forwarded": forwarded,
                "block": is_block,
            }
            self.recent.append(entry)
            if len(self.recent) > 200:
                self.recent = self.recent[-200:]
            if ok:
                self.accepted += 1
                w.accepted += 1
                w.shares_diff += float(difficulty)
                # crude hashrate: difficulty / elapsed since connect (hashes ≈ difficulty for target)
                elapsed = max(1.0, (now_us() - (w.connected_us or now_us())) / 1_000_000.0)
                w.hashrate_est = w.shares_diff / elapsed
                if is_block:
                    self.blocks_found += 1
            else:
                if reason.startswith("above") or reason.startswith("blockhash") or reason.startswith("bad"):
                    self.invalid += 1
                    w.invalid += 1
                self.rejected += 1
                w.rejected += 1
            if forwarded is True:
                self.forwarded_ok += 1
            elif forwarded is False:
                self.forwarded_fail += 1

    def snapshot(self) -> dict:
        with self._lock:
            workers = [asdict(w) for w in self.workers.values()]
            total_hr = sum(w.hashrate_est for w in self.workers.values())
            return {
                "started_us": self.started_us,
                "uptime_sec": (now_us() - self.started_us) / 1_000_000.0,
                "accepted": self.accepted,
                "rejected": self.rejected,
                "invalid": self.invalid,
                "forwarded_ok": self.forwarded_ok,
                "forwarded_fail": self.forwarded_fail,
                "blocks_found": self.blocks_found,
                "active_connections": self.active_connections,
                "current_job_id": self.current_job_id,
                "current_height": self.current_height,
                "upstream_ok": self.upstream_ok,
                "upstream_endpoint": self.upstream_endpoint,
                "pool_hashrate_est": total_hr,
                "workers": workers,
                "recent": list(self.recent[-50:]),
            }

    def save(self, path: str) -> None:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        snap = self.snapshot()
        # drop recent for persistence size
        snap.pop("recent", None)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(snap, f, indent=2)

    def load(self, path: str) -> None:
        if not os.path.isfile(path):
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception:
            return
        with self._lock:
            self.accepted = int(data.get("accepted") or 0)
            self.rejected = int(data.get("rejected") or 0)
            self.invalid = int(data.get("invalid") or 0)
            self.forwarded_ok = int(data.get("forwarded_ok") or 0)
            self.forwarded_fail = int(data.get("forwarded_fail") or 0)
            self.blocks_found = int(data.get("blocks_found") or 0)
            for w in data.get("workers") or []:
                ws = WorkerStats(
                    wallet=str(w.get("wallet") or ""),
                    worker=str(w.get("worker") or ""),
                    accepted=int(w.get("accepted") or 0),
                    rejected=int(w.get("rejected") or 0),
                    invalid=int(w.get("invalid") or 0),
                    last_share_us=int(w.get("last_share_us") or 0),
                    connected_us=int(w.get("connected_us") or now_us()),
                    shares_diff=float(w.get("shares_diff") or 0),
                    hashrate_est=float(w.get("hashrate_est") or 0),
                )
                if ws.wallet:
                    self.workers[ws.key] = ws
