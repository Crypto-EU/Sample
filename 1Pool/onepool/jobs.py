"""Job manager: refresh from upstream or synthesize demo jobs."""

from __future__ import annotations

import asyncio
import hashlib
import logging
import time
from typing import Optional

from .config import PoolConfig
from .pow import MiningJob
from .stats import PoolStats
from .upstream import UpstreamClient

log = logging.getLogger("onepool.jobs")


class JobManager:
    def __init__(self, cfg: PoolConfig, stats: PoolStats, upstream: Optional[UpstreamClient] = None) -> None:
        self.cfg = cfg
        self.stats = stats
        self.upstream = upstream
        self.current: Optional[MiningJob] = None
        self._lock = asyncio.Lock()
        self._task: Optional[asyncio.Task] = None
        self._stop = asyncio.Event()

    async def start(self) -> None:
        self._stop.clear()
        await self.refresh()
        self._task = asyncio.create_task(self._loop(), name="job-refresh")

    async def stop(self) -> None:
        self._stop.set()
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

    async def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                await self.refresh()
            except Exception as e:
                log.warning("job refresh failed: %s", e)
                self.stats.upstream_ok = False
            try:
                await asyncio.wait_for(self._stop.wait(), timeout=self.cfg.job_refresh_sec)
            except asyncio.TimeoutError:
                pass

    async def refresh(self) -> None:
        async with self._lock:
            if self.cfg.mode == "demo":
                job = self._make_demo_job()
            else:
                if not self.upstream:
                    raise RuntimeError("proxy mode requires upstream")
                job = await self.upstream.get_job()
                self.stats.upstream_ok = self.upstream.status.ok
                self.stats.upstream_endpoint = self.upstream.status.endpoint
            self.current = job
            self.stats.current_job_id = job.job_id
            self.stats.current_height = job.height
            log.debug("job height=%s id=%s… share_diff=%s", job.height, job.job_id[:16], job.share_difficulty)

    def get(self) -> MiningJob:
        if self.current is None:
            raise RuntimeError("no job yet")
        # Stamp fresh worktime so miners sync clocks.
        job = self.current
        if not job.worktime:
            job.worktime = int(time.time() * 1_000_000)
        return job

    def find(self, job_id: str) -> Optional[MiningJob]:
        if self.current and (self.current.job_id == job_id or self.current.previous_blockhash == job_id):
            return self.current
        return self.current  # soft: accept against latest job (vardiff pools rotate fast)

    def _make_demo_job(self) -> MiningJob:
        now = int(time.time() * 1_000_000)
        # Deterministic-ish previous hash from time bucket (changes every ~10s).
        bucket = now // 10_000_000
        seed = hashlib.sha256(f"1pool-demo-{bucket}".encode()).hexdigest()
        # 78-char timehash style: 14 hextime + 64 hash
        from .pow import hextime_us

        prev = hextime_us(now) + seed
        main = hextime_us(now - 1_000_000) + hashlib.sha256(f"main-{bucket}".encode()).hexdigest()
        digest = hashlib.sha256(f"digest-{bucket}".encode()).hexdigest()
        height = self.cfg.demo_height + int(bucket % 100000)
        return MiningJob(
            job_id=prev,
            previous_blockhash=prev,
            job_digest=f"{digest}@1pool-demo",
            main_blockhash=main,
            validator=self.cfg.demo_validator,
            miner=self.cfg.demo_miner,
            receipts=[],
            height=height,
            main_height=self.cfg.demo_main_height + int(bucket % 100000),
            share_difficulty=int(self.cfg.default_share_difficulty),
            network_difficulty=int(self.cfg.default_share_difficulty) * 8,
            difficulty=str(self.cfg.default_share_difficulty),
            worktime=now,
            vardiff=True,
        )
