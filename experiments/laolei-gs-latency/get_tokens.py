#!/usr/bin/env python3
# 拉流动性最高的 live 单场盘的 clob token_ids (给 CLOB WSS tick 采集订阅)
import json
import urllib.request


def g(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    return json.loads(urllib.request.urlopen(req, timeout=12).read().decode())


def num(x):
    try:
        return float(x)
    except Exception:
        return 0.0


seen = {}
for off in (0, 100, 200):
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
        for m in (e.get("markets") or []):
            if m.get("closed"):
                continue
            cti = m.get("clobTokenIds")
            if not cti:
                continue
            try:
                toks = json.loads(cti) if isinstance(cti, str) else cti
            except Exception:
                continue
            if toks:
                seen[toks[0]] = (liq, t[:42], m.get("question", "")[:30])

rows = sorted(seen.items(), key=lambda kv: -kv[1][0])[:14]
print("# token_id (YES) | liq | event")
toks = []
for tok, (liq, t, q) in rows:
    print(f"{tok}  liq={liq:.0f}  {t}")
    toks.append(tok)
print("\nASSETS_JSON=" + json.dumps(toks))
