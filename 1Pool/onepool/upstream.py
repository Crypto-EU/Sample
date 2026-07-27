"""Upstream RabbitMiner / 1Pool client (login / getjob / submit)."""

from __future__ import annotations

import asyncio
import logging
import ssl
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

from .pow import MiningJob, dumps_compact, parse_job_from_upstream

log = logging.getLogger("onepool.upstream")


def endpoint_use_tls(hostport: str, default: Optional[bool] = None) -> Tuple[str, int, bool]:
    e = hostport.strip()
    force_tls = False
    force_tcp = False
    for scheme, flag in (
        ("stratum+tls://", True),
        ("stratum+ssl://", True),
        ("tls://", True),
        ("ssl://", True),
        ("stratum+tcp://", False),
        ("tcp://", False),
    ):
        if e.startswith(scheme):
            e = e[len(scheme) :]
            if flag:
                force_tls = True
            else:
                force_tcp = True
            break
    if ":" not in e:
        raise ValueError(f"bad endpoint: {hostport}")
    host, port_s = e.rsplit(":", 1)
    port = int(port_s)
    if force_tls:
        use_tls = True
    elif force_tcp:
        use_tls = False
    elif default is not None:
        use_tls = default
    else:
        # RabbitMiner Saseul: 1901/1921 = SSL, 1911/1931 = TCP
        use_tls = port in (1901, 1921, 443, 8443)
    return host, port, use_tls


@dataclass
class UpstreamStatus:
    ok: bool = False
    endpoint: str = ""
    last_error: str = ""
    last_job_us: int = 0


class UpstreamClient:
    def __init__(
        self,
        endpoints: List[str],
        wallet: str,
        worker: str = "1pool",
        password: str = "x",
        tls_default: Optional[bool] = None,
    ) -> None:
        self.endpoints = endpoints
        self.wallet = wallet
        self.worker = worker
        self.password = password
        self.tls_default = tls_default
        self.status = UpstreamStatus()
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._lock = asyncio.Lock()
        self._msg_id = 1
        self._index = 0

    @property
    def connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    async def close(self) -> None:
        if self._writer is not None:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
        self._reader = None
        self._writer = None

    async def connect_and_login(self) -> None:
        last_err = "no endpoints"
        for i in range(len(self.endpoints)):
            idx = (self._index + i) % len(self.endpoints)
            ep = self.endpoints[idx]
            try:
                host, port, use_tls = endpoint_use_tls(ep, self.tls_default)
            except Exception as e:
                last_err = str(e)
                continue
            label = f"{host}:{port}" + (" (tls)" if use_tls else " (tcp)")
            log.info("upstream connecting %s", label)
            try:
                ssl_ctx = None
                if use_tls:
                    ssl_ctx = ssl.create_default_context()
                    ssl_ctx.check_hostname = False
                    ssl_ctx.verify_mode = ssl.CERT_NONE
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(host, port, ssl=ssl_ctx, server_hostname=host if use_tls else None),
                    timeout=12,
                )
                self._reader, self._writer = reader, writer
                self._index = idx
                ok = await self._login()
                if not ok:
                    last_err = "login rejected"
                    await self.close()
                    continue
                self.status = UpstreamStatus(ok=True, endpoint=label, last_error="")
                log.info("upstream login ok %s wallet=%s", label, self.wallet)
                return
            except Exception as e:
                last_err = f"{label}: {e}"
                log.warning("upstream failed: %s", last_err)
                await self.close()
        self.status = UpstreamStatus(ok=False, endpoint="", last_error=last_err)
        raise ConnectionError(f"all upstream endpoints failed: {last_err}")

    async def _send(self, obj: dict) -> None:
        assert self._writer is not None
        data = (dumps_compact(obj) + "\n").encode("utf-8")
        self._writer.write(data)
        await self._writer.drain()

    async def _recv(self, timeout: float = 45.0) -> dict:
        assert self._reader is not None
        line = await asyncio.wait_for(self._reader.readline(), timeout=timeout)
        if not line:
            raise ConnectionError("upstream closed")
        import json

        return json.loads(line.decode("utf-8", errors="replace").strip())

    async def _login(self) -> bool:
        self._msg_id = 1
        await self._send(
            {
                "id": self._msg_id,
                "method": "login",
                "params": [self.wallet, self.password, self.worker],
                "latehex": False,
            }
        )
        resp = await self._recv(15)
        return bool(resp.get("ok") is True or resp.get("result") is True)

    async def ensure(self) -> None:
        if not self.connected:
            await self.connect_and_login()

    async def get_job(self) -> MiningJob:
        async with self._lock:
            await self.ensure()
            self._msg_id += 1
            try:
                await self._send({"id": self._msg_id, "method": "getjob", "params": [], "latehex": False})
                resp = await self._recv(45)
            except Exception:
                await self.close()
                await self.connect_and_login()
                self._msg_id += 1
                await self._send({"id": self._msg_id, "method": "getjob", "params": [], "latehex": False})
                resp = await self._recv(45)
            if not resp.get("ok"):
                raise RuntimeError(f"getjob failed: {resp}")
            job = parse_job_from_upstream(resp)
            if not job.worktime:
                job.worktime = int(time.time() * 1_000_000)
            self.status.last_job_us = int(time.time() * 1_000_000)
            self.status.ok = True
            return job

    async def submit(self, job: MiningJob, nonce: str, timestamp_us: int, blockhash: str) -> Tuple[bool, str]:
        async with self._lock:
            await self.ensure()
            self._msg_id += 1
            try:
                await self._send(
                    {
                        "id": self._msg_id,
                        "method": "submit",
                        "params": [job.job_id, nonce, int(timestamp_us), blockhash],
                        "latehex": False,
                    }
                )
                resp = await self._recv(30)
            except Exception as e:
                await self.close()
                return False, f"upstream submit error: {e}"
            if resp.get("ok") is True or resp.get("result") is True:
                return True, "ok"
            err = resp.get("error") or resp
            return False, str(err)
