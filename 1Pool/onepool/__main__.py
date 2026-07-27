"""1Pool entrypoint: python -m onepool [--config path]."""

from __future__ import annotations

import argparse
import asyncio
import logging
import signal
import sys

from .config import load_config
from .dashboard import start_http
from .jobs import JobManager
from .server import start_stratum_servers
from .stats import PoolStats
from .upstream import UpstreamClient


def setup_logging(verbose: bool = False) -> None:
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s | %(levelname)-5s | %(name)s | %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )


async def amain(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="onepool", description="1Pool — SASEUL mining pool")
    parser.add_argument("--config", "-c", default="config.yaml", help="Path to YAML/JSON config")
    parser.add_argument("--mode", choices=["proxy", "demo"], help="Override mode")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args(argv)

    setup_logging(args.verbose)
    log = logging.getLogger("onepool")

    try:
        cfg = load_config(args.config if __import__("os").path.isfile(args.config) else None)
    except Exception as e:
        log.error("config error: %s", e)
        return 2

    if args.mode:
        cfg.mode = args.mode

    if cfg.mode == "proxy" and not cfg.upstream_wallet:
        log.error("proxy mode requires upstream_wallet (set in config or ONEPOOL_UPSTREAM_WALLET)")
        return 2

    stats = PoolStats()
    stats.load(cfg.state_file)

    upstream = None
    if cfg.mode == "proxy":
        upstream = UpstreamClient(
            cfg.upstream,
            wallet=cfg.upstream_wallet,
            worker=cfg.upstream_worker,
            password=cfg.upstream_password,
            tls_default=cfg.upstream_tls,
        )
        try:
            await upstream.connect_and_login()
        except Exception as e:
            log.error("upstream connect failed: %s", e)
            return 1

    jobs = JobManager(cfg, stats, upstream)
    await jobs.start()

    servers = await start_stratum_servers(cfg, jobs, stats, upstream)
    http_runner = await start_http(cfg, stats)

    stop = asyncio.Event()

    def _stop(*_args):
        stop.set()

    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            pass

    log.info(
        "1Pool ready mode=%s stratum=:%s http=:%s fee=%.2f%%",
        cfg.mode,
        cfg.listen_port,
        cfg.http_port,
        cfg.pool_fee_percent,
    )
    await stop.wait()

    log.info("shutting down…")
    await jobs.stop()
    for s in servers:
        s.close()
        await s.wait_closed()
    await http_runner.cleanup()
    if upstream:
        await upstream.close()
    try:
        stats.save(cfg.state_file)
    except Exception as e:
        log.warning("state save failed: %s", e)
    return 0


def main() -> None:
    try:
        raise SystemExit(asyncio.run(amain()))
    except KeyboardInterrupt:
        raise SystemExit(0)


if __name__ == "__main__":
    main()
