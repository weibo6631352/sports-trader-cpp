#!/usr/bin/env python3
# market_quality.py — 查 PM 市场流动性: 验证「测的盘是否太薄」+ 找有没有流动性好的 in-play 单场盘
import json
import sys
import urllib.request


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read().decode())


def num(x):
    try:
        return float(x)
    except Exception:
        return 0.0


# 1. 刚测的市场流动性 (验证是否太薄)
measured = sys.argv[1:] if len(sys.argv) > 1 else []
if measured:
    print("=== 刚测市场的 PM 流动性 ===")
    for c in measured:
        try:
            d = fetch("https://gamma-api.polymarket.com/markets?condition_ids=" + c)
            m = d[0] if isinstance(d, list) and d else None
            if m:
                q = (m.get("question") or "")[:42]
                print(f"  vol={num(m.get('volume')):>10.0f} vol24h={num(m.get('volume24hr')):>9.0f} liq={num(m.get('liquidity')):>9.0f}  {q}")
            else:
                print(f"  {c[:24]}: 无")
        except Exception as e:
            print(f"  {c[:24]}: err {e}")

# 2. 当前【live 进行中】的单场体育盘 (非 futures), 按流动性排
print("\n=== 当前 live 进行中的体育事件 (events, 看有无流动性好的单场盘) ===")
try:
    # gamma events: sports tag, 进行中 (有 startDate 已过 + 未 closed)
    evs = fetch("https://gamma-api.polymarket.com/events?closed=false&active=true&limit=200&order=volume24hr&ascending=false")
    evs = evs if isinstance(evs, list) else evs.get("data", [])
    rows = []
    for e in evs:
        if not isinstance(e, dict):
            continue
        title = (e.get("title") or "")[:46]
        liq = num(e.get("liquidity"))
        v24 = num(e.get("volume24hr"))
        # live 标志: 部分 event 有 'live' 或 markets 里有 game 进行
        live = e.get("live") or e.get("inPlay") or ""
        # 单场盘判别: 标题含 vs / @ (对阵) 且非 futures(win the ... champion)
        is_match = (" vs " in title.lower() or " @ " in title.lower())
        is_future = any(k in title.lower() for k in ["win the", "champion", "winner", "mvp", "to win"])
        if is_match and not is_future:
            rows.append((liq, v24, live, title))
    rows.sort(reverse=True)
    if rows:
        print(f"  {'liq':>9} {'vol24h':>9} {'live':>5}  title")
        for liq, v24, live, title in rows[:20]:
            print(f"  {liq:>9.0f} {v24:>9.0f} {str(live)[:5]:>5}  {title}")
    else:
        print("  没找到「单场对阵」型 event (PM 体育多是 futures/outright?)")
except Exception as e:
    print(f"  err {e}")

# 3. esports live (记忆: PM 流动 live 单场 = esports)
print("\n=== esports / 电竞 live 盘 ===")
try:
    for tag in ["esports", "cs2", "lol", "dota"]:
        d = fetch(f"https://gamma-api.polymarket.com/events?closed=false&active=true&limit=30&tag={tag}")
        d = d if isinstance(d, list) else d.get("data", [])
        for e in d[:5]:
            if isinstance(e, dict):
                t = (e.get("title") or "")[:46]
                print(f"  [{tag}] liq={num(e.get('liquidity')):>8.0f} live={e.get('live','')} {t}")
except Exception as e:
    print(f"  err {e}")
