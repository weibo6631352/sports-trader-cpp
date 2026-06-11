#!/usr/bin/env python3
# tf.py — 从 CLOB tick 采集算【taker 成交流】(做市能不能赚的第一性问题: 有没有人来撞你的挂单)
#   + 价差 + 成交格式样例 (供写真做市回测)。
import json
import sys
from collections import defaultdict

PATH = sys.argv[1] if len(sys.argv) > 1 else "data/ml_capture/clob_ticks.jsonl"

trades = defaultdict(list)   # asset -> [(ts, price, size)]
spreads = defaultdict(list)  # asset -> [spread]
t_min = None; t_max = None
trade_sample = []; book_sample = None

for line in open(PATH):
    try:
        d = json.loads(line); rt = d["r"] / 1e9
        m = json.loads(d["m"]) if isinstance(d["m"], str) else d["m"]
    except Exception:
        continue
    t_min = rt if t_min is None else min(t_min, rt)
    t_max = rt if t_max is None else max(t_max, rt)
    for mm in (m if isinstance(m, list) else [m]):
        et = mm.get("event_type") or mm.get("type") or ""
        if et == "book":
            if book_sample is None:
                book_sample = str(mm)[:300]
            aid = mm.get("asset_id", "")
            try:
                bids = mm.get("bids") or mm.get("buys") or []
                asks = mm.get("asks") or mm.get("sells") or []
                bb = max(float(x["price"]) for x in bids) if bids else None
                ba = min(float(x["price"]) for x in asks) if asks else None
                if bb and ba:
                    spreads[aid].append(ba - bb)
            except Exception:
                pass
        elif et in ("last_trade_price", "trade", "tick"):
            if len(trade_sample) < 3:
                trade_sample.append(str(mm)[:240])
            aid = mm.get("asset_id", "")
            try:
                px = float(mm.get("price")); sz = float(mm.get("size", 0))
                trades[aid].append((rt, px, sz))
            except Exception:
                pass

span_min = (t_max - t_min) / 60.0 if t_min else 0
print(f"采集时长: {span_min:.1f} min")
print(f"\n{'asset(尾8)':>10} {'trades':>7} {'笔/min':>7} {'vol':>9} {'vol/min':>9} {'spread中位':>10}")
tot_tr = 0
import statistics
for aid in sorted(set(list(trades) + list(spreads)), key=lambda a: -len(trades.get(a, []))):
    tr = trades.get(aid, [])
    vol = sum(x[2] for x in tr)
    sp = spreads.get(aid, [])
    spm = statistics.median(sp) if sp else float("nan")
    print(f"{aid[-8:]:>10} {len(tr):>7} {len(tr)/span_min if span_min else 0:>7.1f} {vol:>9.0f} {vol/span_min if span_min else 0:>9.0f} {spm:>10.4f}")
    tot_tr += len(tr)
print(f"\n总 trades: {tot_tr}  ({tot_tr/span_min if span_min else 0:.1f}/min 全盘合计)")
print("\n--- trade 样例 (写回测用) ---")
for s in trade_sample:
    print("  ", s)
print("--- book 样例 ---\n  ", book_sample)
if tot_tr == 0:
    print("\n【判读】零 taker 成交 → 这些流动盘此刻没人主动交易(只挂单不成交) → 做市无流可吃, 此刻无意义。")
elif tot_tr / max(span_min, 1) < 3:
    print(f"\n【判读】成交极稀({tot_tr/max(span_min,1):.1f}/min) → 做市能撞上的单太少, 难积累 +200u。")
else:
    print(f"\n【判读】有成交流({tot_tr/max(span_min,1):.1f}/min) → 有做市原料, 下一步真 fill-on-trade 回测。")
