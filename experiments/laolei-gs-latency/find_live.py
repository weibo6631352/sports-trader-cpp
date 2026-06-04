#!/usr/bin/env python3
# 找【此刻真的在打 (in-play)】且流动性高的单场盘 (PM gamma), 不打 Goalserve
import json
import time
import urllib.request
from datetime import datetime, timezone


def g(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    return json.loads(urllib.request.urlopen(req, timeout=10).read().decode())


def num(x):
    try:
        return float(x)
    except Exception:
        return 0.0


def parse_ts(s):
    if not s:
        return 0
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()
    except Exception:
        return 0


now = time.time()
print("now utc =", datetime.now(timezone.utc).strftime("%H:%M"))

# 拉多页 events, 单场对阵, 按流动性
seen = {}
for off in (0, 100, 200, 300):
    try:
        evs = g(f"https://gamma-api.polymarket.com/events?closed=false&active=true&limit=100&offset={off}&order=liquidity&ascending=false")
        evs = evs if isinstance(evs, list) else evs.get("data", [])
    except Exception:
        break
    if not evs:
        break
    for e in evs:
        if not isinstance(e, dict):
            continue
        t = (e.get("title") or "")
        tl = t.lower()
        if not (" vs " in tl or " @ " in tl):
            continue
        if any(k in tl for k in ["win the", "champion", "winner", "mvp", "to win", "make the", "by ", "?"]):
            continue
        liq = num(e.get("liquidity"))
        gst = parse_ts(e.get("startDate")) or parse_ts(e.get("startTime"))
        # markets 里找 gameStartTime
        for m in (e.get("markets") or []):
            g2 = parse_ts(m.get("gameStartTime"))
            if g2:
                gst = g2
                break
        live = (gst > 0 and gst <= now)  # 已开赛
        seen[t] = (liq, live, gst)

rows = sorted(seen.items(), key=lambda kv: -kv[1][0])
print("\n=== 流动性最高的单场盘 (live=已开赛) ===")
print(f"  {'liq':>9} {'live?':>6} {'开赛(min前)':>10}  title")
live_liquid = []
for t, (liq, live, gst) in rows[:25]:
    ago = int((now - gst) / 60) if gst else -9999
    mark = "LIVE" if live else "sched"
    print(f"  {liq:>9.0f} {mark:>6} {ago:>9}m  {t[:48]}")
    if live and liq >= 50000:
        live_liquid.append((t, liq, ago))

print(f"\n>>> 此刻 live 且 liq≥50K 的单场盘: {len(live_liquid)} <<<")
for t, liq, ago in live_liquid:
    print(f"    {t[:52]}  liq={liq:.0f} 开赛{ago}min")
