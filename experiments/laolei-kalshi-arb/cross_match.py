#!/usr/bin/env python3
# Kalshi × Polymarket 全量跨市场价差匹配器 (老雷 2026-06-09, 隔离不碰生产)。
# 目标: 拉两边全部流动盘 → 按事件 fuzzy-match → 算同盘 YES 价差 → 找可套的重叠盘。
# 一次性研究脚本 (老板「免费源/调研」, 非生产 — 符合 Python 仅限探索的项目纪律)。
import json, urllib.request, re, sys
from collections import defaultdict

UA = {"User-Agent": "Mozilla/5.0 research"}
def get(url, timeout=20):
    try:
        req = urllib.request.Request(url, headers=UA)
        return json.load(urllib.request.urlopen(req, timeout=timeout))
    except Exception as e:
        return None

STOP = set("will the be a an of to in on by before after who what is are next not no yes than over under "
           "and or for at as price range day game season win wins won lose 2026 2025 vs new this his".split())
def sig(title):
    t = re.sub(r"[^a-z0-9 ]", " ", (title or "").lower())
    toks = [w for w in t.split() if w not in STOP and len(w) > 2]
    return set(toks)

# ---- 1) Kalshi 全部流动盘 (cursor 分页; 留 yes_ask 在 (0.03,0.97) 的近钱可交易盘) ----
print("拉 Kalshi 流动盘...", file=sys.stderr)
kal = []
cursor = ""
for _ in range(25):  # 上限 25 页 ×1000 = 25000 盘
    url = "https://api.elections.kalshi.com/trade-api/v2/markets?limit=1000&status=open"
    if cursor: url += "&cursor=" + cursor
    d = get(url)
    if not d: break
    for m in d.get("markets", []):
        ya, yb = m.get("yes_ask_dollars"), m.get("yes_bid_dollars")
        if ya is None: continue
        try: ya = float(ya); yb = float(yb or 0)
        except: continue
        if not (0.03 < ya < 0.97): continue
        title = (m.get("title", "") + " " + (m.get("yes_sub_title", "") or "")).strip()
        kal.append({"t": title, "mid": (yb + ya) / 2, "bid": yb, "ask": ya, "tk": m.get("ticker", ""), "s": sig(title)})
    cursor = d.get("cursor", "")
    if not cursor: break
print(f"  Kalshi 流动盘: {len(kal)}", file=sys.stderr)

# ---- 2) PM 全部流动盘 (offset 分页, order=volume; 留有报价的) ----
print("拉 PM 流动盘...", file=sys.stderr)
pm = []
for off in range(0, 3000, 500):
    d = get(f"https://gamma-api.polymarket.com/markets?closed=false&active=true&limit=500&order=volume&ascending=false&offset={off}")
    if not d: break
    ms = d if isinstance(d, list) else d.get("markets", [])
    if not ms: break
    for m in ms:
        px = m.get("outcomePrices")
        if not px: continue
        try:
            if isinstance(px, str): px = json.loads(px)
            yes = float(px[0])
        except: continue
        if not (0.03 < yes < 0.97): continue
        q = m.get("question", "") or ""
        pm.append({"t": q, "yes": yes, "vol": float(m.get("volume") or 0), "s": sig(q)})
print(f"  PM 流动盘: {len(pm)}", file=sys.stderr)

# 提取标的数字(strike/阈值)。两边都有数字时, 必须有共同数字(同 strike) 才算同合约。
def nums(t):
    return set(re.findall(r"\d[\d,]*\.?\d*", (t or "").replace(",", "")))
for k in kal: k["n"] = nums(k["t"])
for p in pm: p["n"] = nums(p["t"])

# ---- 3) 倒排索引 + Jaccard fuzzy-match + strike 约束 + per-PM 去重 ----
inv = defaultdict(list)
for i, p in enumerate(pm):
    for tok in p["s"]: inv[tok].append(i)
best_per_pm = {}  # pm_idx -> (jaccard, kal)
for k in kal:
    cand = defaultdict(int)
    for tok in k["s"]:
        for i in inv.get(tok, []): cand[i] += 1
    for i, sh in cand.items():
        if sh < 2: continue
        p = pm[i]
        j = sh / max(1, len(k["s"] | p["s"]))
        if j < 0.40: continue
        # strike 约束: 两边都有数字 → 必须有共同数字(同阈值)
        if k["n"] and p["n"] and not (k["n"] & p["n"]): continue
        if i not in best_per_pm or j > best_per_pm[i][0]:
            best_per_pm[i] = (j, k)
matches = []
for i, (j, k) in best_per_pm.items():
    p = pm[i]
    matches.append((abs(k["mid"] - p["yes"]), j, k["t"][:46], k["mid"], p["t"][:46], p["yes"], p["vol"]))

matches.sort(reverse=True)
print(f"\n=== 匹配到的同事件盘: {len(matches)} (按价差排序) ===")
print(f"{'价差':>5} {'相似':>4}  {'Kalshi盘':<46} {'K_YES':>6}  {'PM盘':<46} {'PM_YES':>6} {'PM_vol':>8}")
for sp, j, kt, km, pt, py, pv in matches[:30]:
    flag = " <<可套?" if sp >= 0.08 else ""
    print(f"{sp*100:>4.0f}c {j:>4.2f}  {kt:<46} {km:>6.2f}  {pt:<46} {py:>6.2f} {pv:>8.0f}{flag}")
print(f"\n价差>=8c 的(潜在套利, 需人核解析+方向一致): {sum(1 for m in matches if m[0]>=0.08)}")
print("注: 价差大也可能是【解析不同/方向相反/不同结算时刻】的假信号, 须逐个核对; 这是候选筛, 非成交信号。")
