"""Configuration loading for 1Pool."""

from __future__ import annotations

import os
from dataclasses import dataclass, field, asdict
from typing import List, Optional

try:
    import yaml  # type: ignore
except ImportError:  # pragma: no cover
    yaml = None


@dataclass
class PoolConfig:
    # Listen
    listen_host: str = "0.0.0.0"
    listen_port: int = 1911  # RabbitMiner-style plain TCP
    tls_port: int = 0  # 0 = disabled; e.g. 1901
    tls_cert: str = ""
    tls_key: str = ""

    # HTTP dashboard / API
    http_host: str = "0.0.0.0"
    http_port: int = 8080

    # Mode: proxy | demo
    mode: str = "proxy"

    # Upstream RabbitMiner / 1Pool endpoints (host:port). TLS inferred from port.
    upstream: List[str] = field(default_factory=lambda: ["nl.rabbitminer.cc:1901", "fi.rabbitminer.cc:1901"])
    upstream_wallet: str = ""  # pool operator wallet used for upstream login
    upstream_worker: str = "1pool"
    upstream_password: str = "x"
    upstream_tls: Optional[bool] = None  # None = auto by port

    # Pool identity / fee accounting (PROP-style share tracking; fee is informational)
    pool_name: str = "1Pool"
    pool_fee_percent: float = 1.0
    default_share_difficulty: int = 100_000_000_000
    max_timestamp_drift_us: int = 5_000_000
    job_refresh_sec: float = 2.0
    verify_shares: bool = True
    forward_shares: bool = True

    # Demo mode: synthetic jobs
    demo_miner: str = "00000000000000000000000000000000000000000000"
    demo_validator: str = "00000000000000000000000000000000000000000000"
    demo_height: int = 1
    demo_main_height: int = 1

    # Persistence
    state_file: str = "data/1pool-state.json"

    @classmethod
    def from_dict(cls, d: dict) -> "PoolConfig":
        known = {f.name for f in cls.__dataclass_fields__.values()}  # type: ignore[attr-defined]
        kwargs = {k: v for k, v in d.items() if k in known}
        return cls(**kwargs)

    def to_dict(self) -> dict:
        return asdict(self)


def _env_override(cfg: PoolConfig) -> PoolConfig:
    mapping = {
        "ONEPOOL_LISTEN_PORT": ("listen_port", int),
        "ONEPOOL_TLS_PORT": ("tls_port", int),
        "ONEPOOL_HTTP_PORT": ("http_port", int),
        "ONEPOOL_MODE": ("mode", str),
        "ONEPOOL_UPSTREAM": ("upstream", lambda s: [x.strip() for x in s.split(",") if x.strip()]),
        "ONEPOOL_UPSTREAM_WALLET": ("upstream_wallet", str),
        "ONEPOOL_UPSTREAM_WORKER": ("upstream_worker", str),
        "ONEPOOL_POOL_NAME": ("pool_name", str),
        "ONEPOOL_FEE": ("pool_fee_percent", float),
        "ONEPOOL_STATE_FILE": ("state_file", str),
    }
    for env, (attr, cast) in mapping.items():
        val = os.environ.get(env)
        if val is not None and val != "":
            setattr(cfg, attr, cast(val))
    return cfg


def load_config(path: Optional[str] = None) -> PoolConfig:
    cfg = PoolConfig()
    if path and os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        data: dict
        if path.endswith((".yaml", ".yml")):
            if yaml is None:
                raise RuntimeError("PyYAML required for YAML configs (pip install pyyaml)")
            data = yaml.safe_load(text) or {}
        else:
            import json

            data = json.loads(text)
        if not isinstance(data, dict):
            raise ValueError("config root must be an object")
        cfg = PoolConfig.from_dict(data)
    return _env_override(cfg)
