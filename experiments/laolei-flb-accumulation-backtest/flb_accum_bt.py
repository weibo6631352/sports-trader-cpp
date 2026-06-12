#!/usr/bin/env python3
# 一次性预研 (§12.4 数据探索, 跑完归档): FLB 累积 vs 一次性 $25 回测。
# 数据源: 我们 166 settled 市场 (settlements.jsonl) + PM data-api /trades 真实成交流。
# 问题: 把 $25 拆成深度切单累积, 会不会 (a) 救回一次性 FOK 吃不满错过的入场, (b) 漂移成本侵蚀 edge。
# 注: /trades 是成交印记 (非 resting book), 故用「触发邻窗成交量」作可成交深度 proxy — 保守可辩护。
import json, urllib.request, sys, time

TRIGGER = 0.77      # FLB 触发: favorite mid 穿 0.77
STAKE = 25.0        # 目标仓位
SAFETY = 0.80       # 切单安全系数
MINBITE = 5.0       # 单笔下限
IMMED_W = 8         # 即时深度窗 (秒): 一次性 FOK 能吃到的近邻成交量
ACCUM_W = 90        # 累积窗 (秒): 多 tick 补仓总时长

SETTLE_PATH = sys.argv[1] if len(sys.argv) > 1 else "quotes.jsonl.settlements.jsonl"

def get_trades(cid):
    url = f"https://data-api.polymarket.com/trades?market={cid}&limit=2000"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)

def get_winner_map(cid):
    # 权威结算: CLOB /markets/<cid> tokens[].winner (true/false)。返回 {token_id: won_bool}
    url = f"https://clob.polymarket.com/markets/{cid}"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=20) as r:
        m = json.load(r)
    if not m.get("closed"): return None
    out = {}
    for t in m.get("tokens", []):
        out[str(t.get("token_id"))] = bool(t.get("winner"))
    return out if out else None

settles = {}
for line in open(SETTLE_PATH):
    line = line.strip()
    if not line: continue
    d = json.loads(line)
    settles[d["condition_id"]] = d.get("settlement_value")

n_mkt = 0; n_trig = 0; n_no_trades = 0
# 累加器
oneshot_entries = 0; oneshot_pnl = 0.0       # 一次性: 仅当即时深度≥$25 才入场
accum_entries = 0;  accum_pnl = 0.0;  accum_filled_usd = 0.0  # 累积: 窗内尽量补到$25
rescue_entries = 0; rescue_pnl = 0.0          # 累积救回(一次性会错过)的入场 → 这些是 +EV 还是 -EV?
thick_win = [0,0]; thin_win = [0,0]           # [wins, total] 按即时深度厚/薄分组的胜率
drift_samples = []                            # 累积 vwap - 触发价 (漂移成本)
ENTRY_PXS = []; TRIG_TO_END = []              # 诊断: 触发价分布 + 触发到末笔时长

for cid, sv in settles.items():
    n_mkt += 1
    try:
        trades = get_trades(cid)
    except Exception as e:
        n_no_trades += 1; continue
    if not trades:
        n_no_trades += 1; continue
    try:
        winmap = get_winner_map(cid)
    except Exception:
        winmap = None
    if not winmap:
        n_no_trades += 1; continue   # 无权威结算 → 不猜, 跳过
    by_tok = {}
    for x in trades:
        by_tok.setdefault(str(x["asset"]), []).append(
            (int(x["timestamp"]), float(x["price"]), float(x["size"]), x["side"]))
    # 每个 token 独立看是否触发 (favorite 穿 0.77); 一个市场最多取第一个触发的 token
    triggered_here = False
    for tok, seq in by_tok.items():
        if triggered_here: break
        seq.sort()
        # 真穿越: 必须先在 < TRIGGER 有成交 (从下方穿), 排除「开盘即高/首笔已决出」的伪触发
        below_first = next((i for i,(t,p,s,sd) in enumerate(seq) if p < TRIGGER), None)
        if below_first is None: continue        # 从没低于 0.77 → 全程高位, 非穿越
        trig_i = next((i for i in range(below_first+1, len(seq)) if seq[i][1] >= TRIGGER), None)
        if trig_i is None: continue
        triggered_here = True; n_trig += 1
        T, entry_px = seq[trig_i][0], seq[trig_i][1]
        if tok not in winmap: continue          # 该 token 无权威结算映射 → 跳过
        won = 1 if winmap[tok] else 0           # 权威: tokens[].winner
        ENTRY_PXS.append(round(entry_px,3))
        # 触发到该 token 末笔成交的剩余时长 (越短 = 越接近结算 = 越「事后诸葛」)
        TRIG_TO_END.append(seq[-1][0] - T)
        limit_px = min(0.98, entry_px + 0.02)   # 限价: 触发价 + 2 tick 容差
        # 触发后的 BUY 成交流 (价 ≤ 限价, 我们的买单只吃 ask ≤ limit)
        fwd = [(t,p,s) for (t,p,s,sd) in seq[trig_i:] if sd=="BUY" and p <= limit_px]
        immed = sum(s for (t,p,s) in fwd if t <= T + IMMED_W)        # 即时深度 proxy
        accum_avail = sum(s for (t,p,s) in fwd if t <= T + ACCUM_W)  # 累积窗可吃量
        # ---- 一次性 FOK: 即时深度 ≥ $25 才能整单成交, 否则 KILL=错过 ----
        if immed >= STAKE:
            oneshot_entries += 1
            oneshot_pnl += STAKE * ((1.0 if won else 0.0) - entry_px) / entry_px
        # ---- 累积: 窗内按 ask 流 vwap 补到 min($25, 可吃量), 每口≥$5 ----
        fill = min(STAKE, accum_avail)
        if fill >= MINBITE:
            # vwap over the window's buy flow (近似累积均价)
            tot_sz = sum(s for (t,p,s) in fwd if t <= T + ACCUM_W)
            vwap = (sum(p*s for (t,p,s) in fwd if t <= T + ACCUM_W) / tot_sz) if tot_sz>0 else entry_px
            accum_entries += 1; accum_filled_usd += fill
            pnl = fill * ((1.0 if won else 0.0) - vwap) / vwap
            accum_pnl += pnl
            drift_samples.append(vwap - entry_px)
            if immed < STAKE:   # 一次性会错过 → 累积救回
                rescue_entries += 1; rescue_pnl += pnl
        # 厚/薄分组胜率
        grp = thick_win if immed >= STAKE else thin_win
        grp[0] += won; grp[1] += 1
    time.sleep(0.05)  # 友好限速防 429

def wr(g): return f"{g[0]}/{g[1]} = {100*g[0]/g[1]:.1f}%" if g[1] else "n/a"
def avg(x): return sum(x)/len(x) if x else 0.0
print("="*60)
print(f"市场总数={n_mkt}  无 trades={n_no_trades}  触发(穿0.77)={n_trig}")
print("-"*60)
print(f"[一次性 $25]  入场={oneshot_entries}  累计PnL={oneshot_pnl:+.2f}u"
      f"  /入场={oneshot_pnl/oneshot_entries:+.3f}u" if oneshot_entries else "[一次性] 无入场")
print(f"[累积到$25 ]  入场={accum_entries}  已成交=${accum_filled_usd:.0f}  累计PnL={accum_pnl:+.2f}u"
      f"  /入场={accum_pnl/accum_entries:+.3f}u" if accum_entries else "[累积] 无入场")
print("-"*60)
print(f"累积【救回】(一次性会错过)的入场: {rescue_entries} 笔, PnL={rescue_pnl:+.2f}u"
      f"  /入场={rescue_pnl/rescue_entries:+.3f}u" if rescue_entries else "无救回入场")
print(f"  → 救回的是 {'+EV ✅ 累积赚到' if rescue_pnl>0 else '-EV ❌ 累积反亏'}")
print("-"*60)
print(f"厚簿(即时≥$25)触发胜率: {wr(thick_win)}")
print(f"薄簿(即时<$25)触发胜率: {wr(thin_win)}   ← 累积救回的就是这批")
print(f"累积 vwap - 触发价 漂移: 均值={avg(drift_samples):+.4f}  n={len(drift_samples)}")
print("-"*60)
# 诊断: 触发价分布 (是否真在 0.77-0.85 入场, 还是在已决出的高位)
import collections
buckets = collections.Counter()
for p in ENTRY_PXS:
    if p < 0.80: buckets["0.77-0.80"] += 1
    elif p < 0.85: buckets["0.80-0.85"] += 1
    elif p < 0.90: buckets["0.85-0.90"] += 1
    elif p < 0.95: buckets["0.90-0.95"] += 1
    else: buckets["0.95+"] += 1
print("触发价分布:", dict(buckets))
print(f"触发→末笔时长: 中位={sorted(TRIG_TO_END)[len(TRIG_TO_END)//2] if TRIG_TO_END else 0}s"
      f"  最短={min(TRIG_TO_END) if TRIG_TO_END else 0}s  (越短=越接近结算)")
print("="*60)
