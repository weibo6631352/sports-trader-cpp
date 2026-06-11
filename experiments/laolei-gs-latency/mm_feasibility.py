#!/usr/bin/env python3
# mm_feasibility.py — Engine B 做市 carry 可行性 (老板「对任何东西做优化」奔 +200u)
#
# 经济 (PM maker fee=0, takerOnly=true 已确认):
#   做市挂 YES bid + ask, 捕获 spread = cross_spread (= YES_ask+NO_ask-1 = YES bid-ask, 二元市场恒等)。
#   逆选成本 ≈ mid 在持仓窗内的漂移 (vol)。半价差 > 每笔逆选 → 净正。
#   判据: cross_spread > 2 × vol_hold  (半价差 cross_spread/2 > vol_hold)。
#
# 跑: .venv/bin/python3 mm_feasibility.py leadlag.jsonl [liq_cond1 liq_cond2 ...]

import json
import sys
from collections import defaultdict

import numpy as np


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "data/ml_capture/leadlag.jsonl"
    liquid = set(sys.argv[2:])  # 已知流动盘 condition_ids (只在这些下结论)

    rows = defaultdict(list)  # cond -> [(ts, mid, cross_spread)]
    for line in open(path):
        try:
            d = json.loads(line)
        except Exception:
            continue
        c = d.get("condition_id", "")
        mid = d.get("market_mid")
        cs = d.get("cross_spread")
        ts = d.get("as_of_ts_ns")
        if c == "" or mid is None or ts is None:
            continue
        try:
            mid = float(mid); ts = float(ts) / 1e9
            cs = float(cs) if cs is not None else float("nan")
        except Exception:
            continue
        if 0.0 < mid < 1.0:
            rows[c].append((ts, mid, cs))

    HOLD = 5.0  # 持仓窗 (秒) — 做市单典型在簿时长 / 重报间隔
    print(f"{'condition':<26} {'n':>5} {'spread(med)':>11} {'vol_5s':>8} {'spread/2vol':>11} {'结论':>8}")
    results = []
    for c, r in rows.items():
        if liquid and c not in liquid:
            continue
        if len(r) < 60:
            continue
        r.sort()
        ts = np.array([x[0] for x in r])
        mid = np.array([x[1] for x in r])
        cs = np.array([x[2] for x in r])
        cs_valid = cs[np.isfinite(cs) & (cs > 0)]
        if len(cs_valid) < 20:
            continue
        spread_med = float(np.median(cs_valid))
        # vol over HOLD: 重采样到 1s grid, 算 HOLD 秒间隔的 mid 变化 RMS
        t0, t1 = ts[0], ts[-1]
        grid = np.arange(t0, t1, 1.0)
        idx = np.clip(np.searchsorted(ts, grid, side="right") - 1, 0, len(ts) - 1)
        mg = mid[idx]
        step = int(HOLD)
        if len(mg) <= step:
            continue
        dmoves = mg[step:] - mg[:-step]  # HOLD 秒漂移
        vol_hold = float(np.sqrt(np.mean(dmoves ** 2)))
        ratio = spread_med / (2 * vol_hold) if vol_hold > 1e-9 else float("inf")
        verdict = "净正✓" if ratio > 1.0 else "净负✗"
        results.append((c, len(r), spread_med, vol_hold, ratio, verdict))
        print(f"{c[:26]:<26} {len(r):>5} {spread_med:>11.4f} {vol_hold:>8.4f} {ratio:>11.2f} {verdict:>8}")

    if results:
        ratios = np.array([x[4] for x in results])
        spreads = np.array([x[2] for x in results])
        vols = np.array([x[3] for x in results])
        print(f"\n=== 汇总 ({len(results)} 盘) ===")
        print(f"价差 spread 中位: {np.median(spreads):.4f} ({np.median(spreads)*100:.2f} cents)")
        print(f"5s 漂移 vol 中位: {np.median(vols):.4f} ({np.median(vols)*100:.2f} cents)")
        print(f"spread/(2·vol) 中位: {np.median(ratios):.2f}  (>1=做市净正)")
        npos = int(np.sum(ratios > 1.0))
        print(f"净正盘: {npos}/{len(results)}")
        print()
        if np.median(ratios) > 1.2:
            print("【判读】价差显著 > 2×逆选漂移 → 做市 carry 净正空间真实存在 (PM maker 零费助攻)。")
            print("        → 值得建 Engine B 做市引擎: 双边挂单 + 库存管理 + 快撤单(~14ms)。下一步原型 paper 跑。")
        elif np.median(ratios) > 0.8:
            print("【判读】价差 ≈ 2×逆选漂移 (临界) → 做市 carry 薄利, 成败取决于撤单速度 + 库存/选盘。")
            print("        → 可建原型但需精细 (只做价差最宽/漂移最小的盘 + 激进撤单)。")
        else:
            print("【判读】价差 < 2×逆选漂移 → 被动挂单大概率被逆选吃穿, 做市 carry 也 -EV。")
            print("        → PM in-play 太「有效+快」, 连做市都难。需重新审视是否有任何可行 edge。")
    else:
        print("\n无足够数据的流动盘 (cross_spread 缺失 / 样本不足)。")


if __name__ == "__main__":
    main()
