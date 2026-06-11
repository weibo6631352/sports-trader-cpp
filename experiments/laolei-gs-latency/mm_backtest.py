#!/usr/bin/env python3
# mm_backtest.py — 做市 carry 回测 (老板「做市投入产出」最便宜的真答案)
#
# 在采集的 mid 时序上模拟被动做市, 算真实逆选后的净 PnL (验证理论 spread/2vol=1.31 扛不扛得住)。
# 模型 (乐观但抓核心逆选): 每 1s 围绕 mid 双边挂 bid=mid-δ / ask=mid+δ (δ=半价差); 下一 tick mid
#   越过挂价即成交(限价保护, 成交价=挂价); 库存上限 ±N。被动成交本质在动量时发生 → 自然含逆选。
#   PM maker fee=0 (已确认 takerOnly)。若连这个乐观成交模型都净负 → 做市也死。
#
# 跑: .venv/bin/python3 mm_backtest.py leadlag.jsonl

import json
import sys
from collections import defaultdict

import numpy as np


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "data/ml_capture/leadlag.jsonl"

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

    N_INV = 5         # 库存上限 ±5 unit
    SIZE = 1.0        # 每笔 1 unit
    DELTA_FLOOR = 0.005  # 半价差下限
    MOM_W = 3         # 动量回看 (秒) — 近 3s mid 变化 = "盘口压力/快撤信号"代理

    # 预处理每盘的 1s grid mid + 半价差
    mkts = []
    for c, r in rows.items():
        if len(r) < 120:
            continue
        r.sort()
        ts = np.array([x[0] for x in r]); mid = np.array([x[1] for x in r]); cs = np.array([x[2] for x in r])
        csv = cs[np.isfinite(cs) & (cs > 0)]
        cs_med = float(np.median(csv)) if len(csv) else 0.02
        delta = max(cs_med / 2.0, DELTA_FLOOR)
        t0, t1 = ts[0], ts[-1]
        grid = np.arange(t0, t1, 1.0)
        idx = np.clip(np.searchsorted(ts, grid, side="right") - 1, 0, len(ts) - 1)
        mkts.append((c, mid[idx], delta))

    def run(cancel_thr):
        """cancel_thr = 动量超此值则撤该侧挂单 (模拟 14ms 快撤, 不挂进趋势); inf=不撤(原始)。"""
        tot_pnl = 0.0; tot_fills = 0; pos_cnt = 0; npnls = []
        for c, mg, delta in mkts:
            cash = 0.0; inv = 0.0; fills = 0
            for i in range(MOM_W, len(mg) - 1):
                m = mg[i]; nm = mg[i + 1]
                mom = m - mg[i - MOM_W]  # 近 3s 动量 (>0 涨 / <0 跌)
                bid = m - delta; ask = m + delta
                # 快撤: 强下跌(mom<-thr)→不挂 bid(别买进跌势); 强上涨→不挂 ask
                post_bid = mom > -cancel_thr
                post_ask = mom < cancel_thr
                if post_bid and nm <= bid and inv < N_INV:
                    cash -= bid * SIZE; inv += SIZE; fills += 1
                if post_ask and nm >= ask and inv > -N_INV:
                    cash += ask * SIZE; inv -= SIZE; fills += 1
            pnl = cash + inv * mg[-1]
            tot_pnl += pnl; tot_fills += fills
            npnls.append(pnl)
            if pnl > 0: pos_cnt += 1
        return tot_pnl, tot_fills, pos_cnt, len(mkts)

    print(f"做市快撤扫描 ({len(mkts)} 盘, MOM_W={MOM_W}s):")
    print(f"{'cancel_thr':>12} {'总PnL($)':>12} {'成交':>8} {'净正盘':>10} {'每笔PnL':>12}")
    best = None
    for thr in [9.9, 0.05, 0.03, 0.02, 0.012, 0.008]:
        tp, tf, pc, n = run(thr)
        per_fill = tp / tf if tf else 0.0
        tag = "(不撤)" if thr > 1 else ""
        print(f"{thr:>12.3f}{tag:>6} {tp:>12.2f} {tf:>8} {pc:>6}/{n:<4} {per_fill:>12.5f}")
        if best is None or tp > best[1]:
            best = (thr, tp, tf, pc, n)

    print()
    bthr, btp, btf, bpc, bn = best
    if btp > 0 and bpc > bn * 0.55:
        print(f"【判读】快撤(thr={bthr})后做市净正: ${btp:.2f}, {bpc}/{bn} 盘净正 →")
        print(f"        你的直觉对: 不挂进趋势(=快撤)避开逆选后, 做市 carry 在 PM in-play 有正空间。")
        print(f"        → 值得建真 paper 做市原型(用真实盘口OFI做14ms快撤, 比1s动量代理更准)。")
    elif btp > 0:
        print(f"【判读】快撤后总净正(${btp:.2f})但仅 {bpc}/{bn} 盘 → 边际, 高度依赖选盘+撤单精度。")
        print(f"        → 可建原型但只在选定盘 + 需真14ms撤单(1s代理已勉强正, 更快或更好)。")
    else:
        print(f"【判读】即便快撤(扫了多档阈值)做市仍净负(最好 ${btp:.2f}) →")
        print(f"        逆选不是靠不挂进趋势能躲掉的(信息流在我们撤之前就成交) → 纯PM做市也难。")


if __name__ == "__main__":
    main()
