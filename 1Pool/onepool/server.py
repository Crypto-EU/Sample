"""TCP (and optional TLS) pool protocol server — RabbitMiner-compatible."""

from __future__ import annotations

import asyncio
import json
import logging
import ssl
import time
from typing import Optional

from .config import PoolConfig
from .jobs import JobManager
from .pow import dumps_compact, job_to_result, verify_classic_share
from .stats import PoolStats, now_us
from .upstream import UpstreamClient

log = logging.getLogger("onepool.server")


class MinerSession:
    def __init__(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        cfg: PoolConfig,
        jobs: JobManager,
        stats: PoolStats,
        upstream: Optional[UpstreamClient],
    ) -> None:
        self.reader = reader
        self.writer = writer
        self.cfg = cfg
        self.jobs = jobs
        self.stats = stats
        self.upstream = upstream
        self.wallet = ""
        self.worker = ""
        self.authed = False
        peer = writer.get_extra_info("peername")
        self.peer = f"{peer[0]}:{peer[1]}" if peer else "?"

    async def send(self, obj: dict) -> None:
        data = (dumps_compact(obj) + "\n").encode("utf-8")
        self.writer.write(data)
        await self.writer.drain()

    async def run(self) -> None:
        self.stats.note_connect(+1)
        log.info("miner connected %s", self.peer)
        try:
            while True:
                line = await asyncio.wait_for(self.reader.readline(), timeout=120)
                if not line:
                    break
                text = line.decode("utf-8", errors="replace").strip()
                if not text:
                    continue
                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    await self.send({"id": None, "ok": False, "error": "invalid json"})
                    continue
                await self.handle(msg)
        except asyncio.TimeoutError:
            log.info("miner timeout %s", self.peer)
        except Exception as e:
            log.warning("miner session error %s: %s", self.peer, e)
        finally:
            self.stats.note_connect(-1)
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
            log.info("miner disconnected %s wallet=%s worker=%s", self.peer, self.wallet, self.worker)

    async def handle(self, msg: dict) -> None:
        mid = msg.get("id")
        method = str(msg.get("method") or "").lower()
        params = msg.get("params")

        if method == "login":
            await self._login(mid, params)
            return
        if not self.authed:
            await self.send({"id": mid, "ok": False, "error": "not logged in"})
            return
        if method == "getjob":
            await self._getjob(mid)
            return
        if method == "submit":
            await self._submit(mid, params)
            return
        await self.send({"id": mid, "ok": False, "error": f"unknown method: {method}"})

    async def _login(self, mid, params) -> None:
        if not isinstance(params, list) or len(params) < 1:
            await self.send({"id": mid, "ok": False, "error": "invalid json"})
            return
        wallet = str(params[0] or "").strip()
        worker = str(params[2] if len(params) > 2 else "rig").strip() or "rig"
        if not wallet:
            await self.send({"id": mid, "ok": False, "error": "wallet required"})
            return
        self.wallet = wallet
        self.worker = worker
        self.authed = True
        self.stats.ensure_worker(wallet, worker)
        log.info("login ok %s wallet=%s worker=%s", self.peer, wallet, worker)
        await self.send({"id": mid, "result": True, "ok": True})

    async def _getjob(self, mid) -> None:
        try:
            job = self.jobs.get()
        except Exception as e:
            await self.send({"id": mid, "ok": False, "error": f"no job: {e}"})
            return
        # Keep worktime fresh for miner clock sync.
        if not job.worktime:
            job.worktime = now_us()
        result = job_to_result(job)
        # Always include current pool microtime so miners can sync (hasher-compatible).
        result["worktime"] = now_us()
        await self.send({"id": mid, "result": result, "ok": True})

    async def _submit(self, mid, params) -> None:
        if not isinstance(params, list) or len(params) < 4:
            await self.send(
                {
                    "id": mid,
                    "ok": False,
                    "error": "submit requires job_id, nonce, timestamp, blockhash",
                }
            )
            return
        job_id = str(params[0])
        nonce = str(params[1]).lower()
        try:
            timestamp_us = int(params[2])
        except (TypeError, ValueError):
            await self.send({"id": mid, "ok": False, "error": "bad timestamp"})
            return
        blockhash = str(params[3]).lower()

        job = self.jobs.find(job_id)
        if job is None:
            await self.send({"id": mid, "ok": False, "error": "unknown job"})
            self.stats.record_share(
                wallet=self.wallet,
                worker=self.worker,
                ok=False,
                reason="unknown job",
                difficulty=0,
            )
            return

        # Stamp worktime for drift check if missing.
        if not job.worktime:
            job.worktime = now_us()

        if self.cfg.verify_shares:
            ok, reason = verify_classic_share(
                job,
                nonce,
                timestamp_us,
                blockhash,
                max_drift_us=self.cfg.max_timestamp_drift_us,
                now_us=now_us(),
            )
            if not ok:
                log.info(
                    "reject %s.%s reason=%s nonce=%s",
                    self.wallet,
                    self.worker,
                    reason,
                    nonce[:16],
                )
                await self.send({"id": mid, "ok": False, "error": reason})
                self.stats.record_share(
                    wallet=self.wallet,
                    worker=self.worker,
                    ok=False,
                    reason=reason,
                    difficulty=int(job.share_difficulty),
                )
                return
        else:
            reason = "ok"

        forwarded: Optional[bool] = None
        is_block = False
        if self.cfg.mode == "proxy" and self.cfg.forward_shares and self.upstream:
            fok, ferr = await self.upstream.submit(job, nonce, timestamp_us, blockhash)
            forwarded = fok
            if not fok:
                log.warning("upstream reject %s.%s: %s", self.wallet, self.worker, ferr)
                await self.send({"id": mid, "ok": False, "error": ferr})
                self.stats.record_share(
                    wallet=self.wallet,
                    worker=self.worker,
                    ok=False,
                    reason=f"upstream: {ferr}",
                    difficulty=int(job.share_difficulty),
                    forwarded=False,
                )
                return
            # Heuristic: if share also meets network difficulty, count as block.
            from .pow import hash_meets_target, target_from_difficulty, root_hex_from_parts, build_header_hash_hex

            header = build_header_hash_hex(
                height=job.height,
                timestamp_us=timestamp_us,
                main_height=job.main_height,
                main_blockhash=job.main_blockhash,
                validator=job.validator,
                miner=job.miner,
                receipts=job.receipts,
            )
            root = root_hex_from_parts(job.previous_blockhash, header, nonce)
            net_target = target_from_difficulty(max(1, int(job.network_difficulty or job.share_difficulty)))
            is_block = hash_meets_target(bytes.fromhex(root), net_target)

        await self.send({"id": mid, "result": True, "ok": True})
        self.stats.record_share(
            wallet=self.wallet,
            worker=self.worker,
            ok=True,
            reason="ok",
            difficulty=int(job.share_difficulty),
            forwarded=forwarded,
            is_block=is_block,
        )
        log.info(
            "accept %s.%s diff=%s forward=%s block=%s",
            self.wallet,
            self.worker,
            job.share_difficulty,
            forwarded,
            is_block,
        )


async def _client_connected(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    cfg: PoolConfig,
    jobs: JobManager,
    stats: PoolStats,
    upstream: Optional[UpstreamClient],
) -> None:
    session = MinerSession(reader, writer, cfg, jobs, stats, upstream)
    await session.run()


async def start_stratum_servers(
    cfg: PoolConfig,
    jobs: JobManager,
    stats: PoolStats,
    upstream: Optional[UpstreamClient],
) -> list[asyncio.AbstractServer]:
    servers: list[asyncio.AbstractServer] = []

    async def handler(r, w):
        await _client_connected(r, w, cfg, jobs, stats, upstream)

    srv = await asyncio.start_server(handler, cfg.listen_host, cfg.listen_port)
    servers.append(srv)
    log.info("stratum listening tcp://%s:%s", cfg.listen_host, cfg.listen_port)

    if cfg.tls_port and cfg.tls_cert and cfg.tls_key:
        ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_ctx.load_cert_chain(cfg.tls_cert, cfg.tls_key)
        tls_srv = await asyncio.start_server(handler, cfg.listen_host, cfg.tls_port, ssl=ssl_ctx)
        servers.append(tls_srv)
        log.info("stratum listening tls://%s:%s", cfg.listen_host, cfg.tls_port)

    return servers
