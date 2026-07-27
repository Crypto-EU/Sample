"""HTTP dashboard + JSON API."""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Optional

from aiohttp import web

from .config import PoolConfig
from .stats import PoolStats

log = logging.getLogger("onepool.http")

DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>1Pool — SASEUL</title>
<link rel="preconnect" href="https://fonts.googleapis.com"/>
<link href="https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;600;700&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet"/>
<style>
:root {
  --bg0: #0b1210;
  --bg1: #12201a;
  --fg: #e8f2ec;
  --muted: #8aa396;
  --accent: #3dd68c;
  --warn: #e6b35a;
  --bad: #e57373;
  --line: rgba(61,214,140,.18);
}
* { box-sizing: border-box; }
body {
  margin: 0; min-height: 100vh; color: var(--fg);
  font-family: "Space Grotesk", sans-serif;
  background:
    radial-gradient(1200px 600px at 10% -10%, rgba(61,214,140,.18), transparent 55%),
    radial-gradient(900px 500px at 100% 0%, rgba(80,140,255,.12), transparent 50%),
    linear-gradient(165deg, var(--bg0), var(--bg1) 55%, #0a1511);
}
header {
  padding: 2.5rem 1.5rem 1rem; max-width: 1100px; margin: 0 auto;
}
.brand {
  font-size: clamp(2.4rem, 6vw, 3.6rem); font-weight: 700; letter-spacing: -.03em;
  margin: 0; line-height: 1;
}
.brand span { color: var(--accent); }
.tag { color: var(--muted); margin-top: .6rem; font-size: 1.05rem; max-width: 36rem; }
main { max-width: 1100px; margin: 0 auto; padding: 1rem 1.5rem 3rem; }
.grid {
  display: grid; gap: 1rem;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  margin: 1.5rem 0;
}
.stat {
  padding: 1rem 1.1rem;
  border-top: 1px solid var(--line);
  background: linear-gradient(180deg, rgba(61,214,140,.06), transparent);
}
.stat .k { color: var(--muted); font-size: .8rem; text-transform: uppercase; letter-spacing: .08em; }
.stat .v {
  font-family: "JetBrains Mono", monospace; font-size: 1.45rem; margin-top: .35rem; font-weight: 600;
}
section h2 { font-size: 1.1rem; margin: 2rem 0 .8rem; font-weight: 600; }
table { width: 100%; border-collapse: collapse; font-size: .92rem; }
th, td {
  text-align: left; padding: .55rem .4rem; border-bottom: 1px solid var(--line);
  font-family: "JetBrains Mono", monospace; font-size: .82rem;
}
th { color: var(--muted); font-family: "Space Grotesk", sans-serif; font-weight: 600; font-size: .75rem; text-transform: uppercase; letter-spacing: .06em; }
.ok { color: var(--accent); }
.bad { color: var(--bad); }
.mono { font-family: "JetBrains Mono", monospace; }
footer { color: var(--muted); font-size: .85rem; margin-top: 2.5rem; }
</style>
</head>
<body>
<header>
  <h1 class="brand">1<span>Pool</span></h1>
  <p class="tag">Eigener SASEUL Mining Pool — RabbitMiner-kompatibel für 1Miner / hasher.</p>
</header>
<main>
  <div class="grid" id="stats"></div>
  <section>
    <h2>Worker</h2>
    <table>
      <thead><tr><th>Wallet</th><th>Worker</th><th>Accepted</th><th>Rejected</th><th>Hashrate (est.)</th><th>Last share</th></tr></thead>
      <tbody id="workers"></tbody>
    </table>
  </section>
  <section>
    <h2>Letzte Shares</h2>
    <table>
      <thead><tr><th>Zeit</th><th>Worker</th><th>Status</th><th>Diff</th><th>Grund</th></tr></thead>
      <tbody id="recent"></tbody>
    </table>
  </section>
  <footer id="meta"></footer>
</main>
<script>
const fmtHR = (h) => {
  if (!h || h <= 0) return "—";
  const u = ["H/s","KH/s","MH/s","GH/s","TH/s"];
  let i = 0; let v = h;
  while (v >= 1000 && i < u.length-1) { v /= 1000; i++; }
  return v.toFixed(2) + " " + u[i];
};
const ago = (us) => {
  if (!us) return "—";
  const s = Math.max(0, (Date.now()*1000 - us)/1e6);
  if (s < 60) return s.toFixed(0) + "s";
  if (s < 3600) return (s/60).toFixed(0) + "m";
  return (s/3600).toFixed(1) + "h";
};
async function refresh() {
  const r = await fetch("/api/stats");
  const d = await r.json();
  const cells = [
    ["Mode", d.mode],
    ["Height", d.current_height || "—"],
    ["Connections", d.active_connections],
    ["Accepted", d.accepted],
    ["Rejected", d.rejected],
    ["Blocks", d.blocks_found],
    ["Pool HR", fmtHR(d.pool_hashrate_est)],
    ["Fee", d.pool_fee_percent + "%"],
    ["Upstream", d.upstream_ok ? (d.upstream_endpoint || "ok") : (d.mode === "demo" ? "demo" : "down")],
  ];
  document.getElementById("stats").innerHTML = cells.map(([k,v]) =>
    `<div class="stat"><div class="k">${k}</div><div class="v">${v}</div></div>`).join("");
  document.getElementById("workers").innerHTML = (d.workers||[]).map(w =>
    `<tr><td>${w.wallet.slice(0,12)}…</td><td>${w.worker}</td><td class="ok">${w.accepted}</td><td class="bad">${w.rejected}</td><td>${fmtHR(w.hashrate_est)}</td><td>${ago(w.last_share_us)}</td></tr>`
  ).join("") || `<tr><td colspan="6">Noch keine Worker</td></tr>`;
  document.getElementById("recent").innerHTML = (d.recent||[]).slice().reverse().map(e =>
    `<tr><td>${ago(e.ts)}</td><td>${e.worker}</td><td class="${e.ok?"ok":"bad"}">${e.ok?"OK":"REJ"}</td><td>${e.difficulty}</td><td>${e.reason||""}</td></tr>`
  ).join("") || `<tr><td colspan="5">—</td></tr>`;
  document.getElementById("meta").textContent =
    `${d.pool_name} · uptime ${(d.uptime_sec/3600).toFixed(2)}h · job ${d.current_job_id ? d.current_job_id.slice(0,20)+"…" : "—"}`;
}
refresh(); setInterval(refresh, 3000);
</script>
</body>
</html>
"""


def create_http_app(cfg: PoolConfig, stats: PoolStats) -> web.Application:
    app = web.Application()

    async def index(_request: web.Request) -> web.Response:
        return web.Response(text=DASHBOARD_HTML, content_type="text/html")

    async def api_stats(_request: web.Request) -> web.Response:
        snap = stats.snapshot()
        snap["mode"] = cfg.mode
        snap["pool_name"] = cfg.pool_name
        snap["pool_fee_percent"] = cfg.pool_fee_percent
        return web.json_response(snap)

    async def api_health(_request: web.Request) -> web.Response:
        return web.json_response(
            {
                "ok": True,
                "mode": cfg.mode,
                "upstream_ok": stats.upstream_ok if cfg.mode == "proxy" else True,
                "connections": stats.active_connections,
            }
        )

    app.router.add_get("/", index)
    app.router.add_get("/api/stats", api_stats)
    app.router.add_get("/api/health", api_health)
    return app


async def start_http(cfg: PoolConfig, stats: PoolStats) -> web.AppRunner:
    app = create_http_app(cfg, stats)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, cfg.http_host, cfg.http_port)
    await site.start()
    log.info("dashboard http://%s:%s", cfg.http_host, cfg.http_port)
    return runner
