#!/usr/bin/env python3
# 查 captured 市场的名字 + 是否 live (PM gamma, 不打 Goalserve), 刻画「有 bet365 赔率的盘」长啥样
import json
import sys
import urllib.request


def g(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    return json.loads(urllib.request.urlopen(req, timeout=8).read().decode())


def num(x):
    try:
        return float(x)
    except Exception:
        return 0.0


conds = sys.argv[1:]
print("=== 我们能拿到 bet365 赔率的 captured 市场 (按流动性) ===")
rows = []
for c in conds:
    try:
        d = g("https://gamma-api.polymarket.com/markets?condition_ids=" + c)
        m = d[0] if isinstance(d, list) and d else None
        if not m:
            rows.append((-1, c[:18], "(查无)"))
            continue
        q = (m.get("question") or "")[:50]
        liq = num(m.get("liquidity"))
        rows.append((liq, c[:18], q))
    except Exception as e:
        rows.append((-2, c[:18], "err %s" % e))
for liq, c, q in sorted(rows, reverse=True):
    print("  liq=%9.0f  %s" % (liq, q))

print("\n=== 当前 PM 上流动性最高的【单场对阵】(看跟上面是否重叠) ===")
try:
    evs = g("https://gamma-api.polymarket.com/events?closed=false&active=true&limit=120&order=liquidity&ascending=false")
    evs = evs if isinstance(evs, list) else evs.get("data", [])
    n = 0
    for e in evs:
        if not isinstance(e, dict):
            continue
        t = (e.get("title") or "")
        tl = t.lower()
        if not (" vs " in tl or " @ " in tl):
            continue
        if any(k in tl for k in ["win the", "champion", "winner", "mvp", "to win", "make the"]):
            continue
        liq = num(e.get("liquidity"))
        live = e.get("live") or e.get("inPlay") or ""
        print("  liq=%9.0f live=%-5s %s" % (liq, str(live)[:5], t[:50]))
        n += 1
        if n >= 12:
            break
except Exception as e:
    print("  err %s" % e)
