"""Integration: demo pool accepts login/getjob over TCP."""

from __future__ import annotations

import asyncio
import json
import socket
import time
import unittest

from onepool.config import PoolConfig
from onepool.jobs import JobManager
from onepool.server import start_stratum_servers
from onepool.stats import PoolStats


class TestDemoServer(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        # Bind ephemeral port
        sock = socket.socket()
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
        sock.close()

        self.cfg = PoolConfig(
            mode="demo",
            listen_host="127.0.0.1",
            listen_port=port,
            http_port=0,
            default_share_difficulty=1,
            verify_shares=True,
            forward_shares=False,
            job_refresh_sec=30,
        )
        self.stats = PoolStats()
        self.jobs = JobManager(self.cfg, self.stats, None)
        await self.jobs.start()
        self.servers = await start_stratum_servers(self.cfg, self.jobs, self.stats, None)
        await asyncio.sleep(0.05)

    async def asyncTearDown(self):
        await self.jobs.stop()
        for s in self.servers:
            s.close()
            await s.wait_closed()

    async def _rpc(self, *msgs):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.cfg.listen_port)
        out = []
        for m in msgs:
            writer.write((json.dumps(m) + "\n").encode())
            await writer.drain()
            line = await asyncio.wait_for(reader.readline(), timeout=5)
            out.append(json.loads(line.decode()))
        writer.close()
        await writer.wait_closed()
        return out

    async def test_login_getjob(self):
        login, job = await self._rpc(
            {"id": 1, "method": "login", "params": ["testwallet", "x", "rig1"], "latehex": False},
            {"id": 2, "method": "getjob", "params": [], "latehex": False},
        )
        self.assertTrue(login.get("ok"))
        self.assertTrue(job.get("ok"))
        result = job["result"]
        self.assertEqual(len(result["previous_blockhash"]), 78)
        self.assertIn("share_difficulty", result)
        self.assertGreater(result.get("worktime", 0), 0)

    async def test_submit_valid_easy(self):
        from onepool.pow import (
            blockhash_from_root,
            build_header_hash_hex,
            root_hex_from_parts,
        )

        login, jobmsg = await self._rpc(
            {"id": 1, "method": "login", "params": ["w", "x", "r"], "latehex": False},
            {"id": 2, "method": "getjob", "params": [], "latehex": False},
        )
        self.assertTrue(login["ok"])
        job = self.jobs.get()
        ts = int(time.time() * 1_000_000)
        job.worktime = ts
        nonce = "00000000000000aa"
        header = build_header_hash_hex(
            height=job.height,
            timestamp_us=ts,
            main_height=job.main_height,
            main_blockhash=job.main_blockhash,
            validator=job.validator,
            miner=job.miner,
            receipts=job.receipts,
        )
        root = root_hex_from_parts(job.previous_blockhash, header, nonce)
        bh = blockhash_from_root(root, ts)

        reader, writer = await asyncio.open_connection("127.0.0.1", self.cfg.listen_port)
        writer.write(
            (
                json.dumps({"id": 1, "method": "login", "params": ["w", "x", "r"]})
                + "\n"
            ).encode()
        )
        await writer.drain()
        await reader.readline()
        writer.write(
            (
                json.dumps(
                    {
                        "id": 3,
                        "method": "submit",
                        "params": [job.job_id, nonce, ts, bh],
                        "latehex": False,
                    }
                )
                + "\n"
            ).encode()
        )
        await writer.drain()
        resp = json.loads((await asyncio.wait_for(reader.readline(), timeout=5)).decode())
        writer.close()
        await writer.wait_closed()
        self.assertTrue(resp.get("ok"), resp)
        self.assertGreaterEqual(self.stats.accepted, 1)


if __name__ == "__main__":
    unittest.main()
