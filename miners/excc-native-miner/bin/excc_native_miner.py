#!/usr/bin/env python3
"""Experimental native EXCC stratum miner.

This process is intentionally independent from lolMiner. It connects to an EXCC
stratum pool, builds EXCC work headers, runs the native Equihash 144/5 solver
binary, and submits found Equihash solutions.
"""

from __future__ import annotations

import argparse
import binascii
import json
import os
import queue
import random
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any, Optional


EXCC_NONCE_OFFSET = 140
EXCC_XNONCE_OFFSET = 144
EXCC_XNONCE_SIZE = 12
EXCC_WORKDATA_LEN = 192


@dataclass
class Job:
    job_id: str
    prev_hash: str
    cb1: str
    cb2: str
    version: str
    nbits: str
    ntime: str
    clean: bool
    extranonce1: str


class StratumMiner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.sock: Optional[socket.socket] = None
        self.file = None
        self.next_id = 1
        self.extranonce1 = ""
        self.current_job: Optional[Job] = None
        self.job_version = 0
        self.stop = threading.Event()
        self.submit_ids: set[int] = set()
        self.solutions = 0
        self.submitted = 0
        self.accepted = 0
        self.rejected = 0
        self.started = time.time()
        self.log_lock = threading.Lock()

    def log(self, message: str) -> None:
        with self.log_lock:
            print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}", flush=True)

    def send(self, method: str, params: list[Any], msg_id: Optional[int] = None) -> int:
        if self.sock is None:
            raise RuntimeError("stratum socket is not connected")
        if msg_id is None:
            msg_id = self.next_id
            self.next_id += 1
        payload = json.dumps({"id": msg_id, "method": method, "params": params}, separators=(",", ":"))
        self.sock.sendall(payload.encode("utf-8") + b"\n")
        return msg_id

    def connect(self) -> None:
        host, port = parse_pool(self.args.pool)
        self.log(f"Connecting to stratum pool {host}:{port}")
        self.sock = socket.create_connection((host, port), timeout=self.args.connect_timeout)
        self.sock.settimeout(None)
        self.file = self.sock.makefile("r", encoding="utf-8", newline="\n")

        self.send("mining.subscribe", [self.args.agent])
        self.send("mining.authorize", [self.args.user, self.args.password])

    def handle_response(self, msg: dict[str, Any]) -> None:
        msg_id = msg.get("id")
        if msg_id in self.submit_ids:
            self.submit_ids.discard(msg_id)
            if msg.get("result") is True:
                self.accepted += 1
                self.log(f"share accepted accepted={self.accepted} rejected={self.rejected}")
            else:
                self.rejected += 1
                self.log(f"share rejected result={msg.get('result')} error={msg.get('error')}")
            return

        result = msg.get("result")
        if isinstance(result, list) and len(result) >= 3 and isinstance(result[1], str):
            self.extranonce1 = result[1]
            self.log(f"subscribed extranonce1={self.extranonce1} extranonce2_size={result[2]}")
        elif result is True:
            self.log(f"authorized user={self.args.user}")
        elif msg.get("error"):
            self.log(f"stratum error: {msg.get('error')}")

    def handle_notify(self, params: list[Any]) -> None:
        if len(params) < 9:
            self.log(f"ignoring short mining.notify params={params!r}")
            return
        if not self.extranonce1:
            self.log("received mining.notify before subscribe extranonce; waiting")
            return

        job = Job(
            job_id=str(params[0]),
            prev_hash=str(params[1]),
            cb1=str(params[2]),
            cb2=str(params[3]),
            version=str(params[5]),
            nbits=str(params[6]),
            ntime=str(params[7]),
            clean=bool(params[8]),
            extranonce1=self.extranonce1,
        )
        self.current_job = job
        self.job_version += 1
        self.log(f"new job id={job.job_id} clean={job.clean} ntime={job.ntime}")

    def handle_method(self, msg: dict[str, Any]) -> None:
        method = msg.get("method")
        params = msg.get("params") or []
        if method == "mining.notify":
            self.handle_notify(params)
        elif method == "mining.set_difficulty":
            diff = params[0] if params else "?"
            self.log(f"pool difficulty set to {diff}")
        elif method == "client.get_version":
            msg_id = msg.get("id")
            if isinstance(msg_id, int):
                self.send("client.get_version", [self.args.agent], msg_id=msg_id)
        elif method:
            self.log(f"unhandled stratum method={method}")

    def read_loop(self) -> None:
        assert self.file is not None
        for line in self.file:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError as exc:
                self.log(f"invalid json from pool: {exc}: {line[:200]}")
                continue

            if msg.get("method"):
                self.handle_method(msg)
            else:
                self.handle_response(msg)

    def submit_solution(self, job: Job, nonce_hex_le: str, solution_hex: str) -> None:
        submit_id = self.next_id
        self.next_id += 1
        timestamp = f"{int(job.ntime, 16):08x}"
        params = [self.args.user, job.job_id, job.extranonce1, timestamp, nonce_hex_le, solution_hex]
        self.submit_ids.add(submit_id)
        self.submitted += 1
        self.send("mining.submit", params, msg_id=submit_id)
        self.log(f"submitted share job={job.job_id} nonce={nonce_hex_le} submitted={self.submitted}")

    def mine_loop(self) -> None:
        while not self.stop.is_set():
            job = self.current_job
            version = self.job_version
            if job is None:
                time.sleep(0.2)
                continue

            header_hex = build_header(job)
            nonce = random.getrandbits(32)
            cmd = [
                self.args.solver,
                "-x",
                header_hex,
                "-n",
                str(nonce),
                "-r",
                str(self.args.range),
                "-t",
                str(self.args.threads),
            ]

            started = time.time()
            try:
                proc = subprocess.run(
                    cmd,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=self.args.solver_timeout,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                self.log("solver timed out; reducing --range may improve responsiveness")
                continue

            elapsed = max(time.time() - started, 0.001)
            rate = float(self.args.range) / elapsed
            self.log(f"native rate={rate:.6f} sol/s range={self.args.range} accepted={self.accepted} rejected={self.rejected}")

            if proc.returncode != 0:
                self.log(f"solver exit={proc.returncode}: {proc.stderr.strip()[:300]}")
                time.sleep(1)
                continue

            if version != self.job_version:
                self.log("discarding solver output from stale job")
                continue

            for line in proc.stdout.splitlines():
                parts = line.strip().split()
                if len(parts) != 3 or parts[0] != "solution":
                    continue
                nonce_hex_le, solution_hex = parts[1], parts[2]
                if len(solution_hex) != 200:
                    self.log(f"ignoring unexpected solution length={len(solution_hex)}")
                    continue
                self.solutions += 1
                self.submit_solution(job, nonce_hex_le, solution_hex)

    def run(self) -> None:
        self.connect()
        reader = threading.Thread(target=self.read_loop, name="stratum-reader", daemon=True)
        reader.start()
        self.mine_loop()


def parse_pool(pool: str) -> tuple[str, int]:
    for prefix in ("stratum+tcp://", "tcp://"):
        if pool.startswith(prefix):
            pool = pool[len(prefix) :]
    if "/" in pool:
        pool = pool.split("/", 1)[0]
    if ":" not in pool:
        raise ValueError("pool must be host:port")
    host, port_s = pool.rsplit(":", 1)
    return host, int(port_s)


def hex_to_bytes(value: str, name: str) -> bytes:
    try:
        return binascii.unhexlify(value)
    except binascii.Error as exc:
        raise ValueError(f"{name} is not valid hex") from exc


def build_header(job: Job) -> str:
    work = bytearray(EXCC_WORKDATA_LEN)
    pos = 0

    version = hex_to_bytes(job.version, "version")
    if len(version) != 4:
        raise ValueError("block version must be 4 bytes")
    work[pos : pos + 4] = version
    pos += 4

    prev_hash = hex_to_bytes(job.prev_hash, "previous hash")
    if len(prev_hash) != 32:
        raise ValueError("previous hash must be 32 bytes")
    work[pos : pos + 32] = prev_hash
    pos += 32

    cb1 = hex_to_bytes(job.cb1, "coinbase part 1")
    if len(cb1) < 108:
        raise ValueError("coinbase part 1 is shorter than 108 bytes")
    work[pos : pos + 108] = cb1[:108]

    extranonce = hex_to_bytes(job.extranonce1, "extranonce1")
    if len(extranonce) != EXCC_XNONCE_SIZE:
        raise ValueError(f"extranonce1 must be {EXCC_XNONCE_SIZE} bytes for EXCC")
    work[EXCC_XNONCE_OFFSET : EXCC_XNONCE_OFFSET + EXCC_XNONCE_SIZE] = extranonce

    cb2 = hex_to_bytes(job.cb2, "coinbase part 2")
    work[176 : 176 + min(len(cb2), EXCC_WORKDATA_LEN - 176)] = cb2[: EXCC_WORKDATA_LEN - 176]

    return binascii.hexlify(work).decode("ascii")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Experimental native EXCC Equihash 144/5 miner")
    parser.add_argument("--pool", required=True, help="stratum+tcp://host:port")
    parser.add_argument("--user", required=True, help="wallet.worker")
    parser.add_argument("--password", default="x")
    parser.add_argument("--solver", required=True)
    parser.add_argument("--threads", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("--range", type=int, default=1, help="nonces per solver call")
    parser.add_argument("--solver-timeout", type=int, default=900)
    parser.add_argument("--connect-timeout", type=int, default=20)
    parser.add_argument("--agent", default="excc-native-miner/0.1.0")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not os.path.exists(args.solver):
        print(f"solver not found: {args.solver}", file=sys.stderr)
        return 1
    miner = StratumMiner(args)
    try:
        miner.run()
    except KeyboardInterrupt:
        miner.stop.set()
        return 0
    except Exception as exc:
        print(f"fatal: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
