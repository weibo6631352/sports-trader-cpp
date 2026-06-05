#!/usr/bin/env python3
"""backtest_sharp.py — sharp 信号是否真 +EV (历史已结算回测)。
f18=g_bm_inplay_fair (inplay 直播源 bet365 de-vig sharp), f8=b_mid (PM 市场), f84=market_type, label=结算结果。
策略: 当 |sharp − 市场| > 阈值 → 买 sharp 偏好的一边 (sharp>市场买YES, 否则买NO), 按市场价进场, 结算。
对照: 若市场高效, 按市场价进场结算 = 0 EV; sharp-follow 的正 PnL = sharp 真 edge。"""
import json
import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "data/ml_capture/quotes.jsonl.training.jsonl"
FEE = 0.03  # 体育 moneyline 手续费率 (近似)


def run(min_edge, moneyline_only=True, use_ask_proxy=True):
    trades = 0
    pnl_sum = 0.0
    wins = 0
    sharp_better = 0  # sharp 比市场更接近结果的次数
    buckets = {}  # edge 档 → [n, pnl_sum]
    valid_sharp = 0
    n_settled = 0
    with open(PATH) as f:
        for line in f:
            try:
                r = json.loads(line)
            except Exception:
                continue
            if not r.get("label_valid", 0):
                continue
            n_settled += 1
            sharp = r.get("f18")
            mkt = r.get("f8")
            cat = r.get("f84")
            label = r.get("label")
            if sharp is None or mkt is None or label is None:
                continue
            sharp = float(sharp); mkt = float(mkt); label = float(label)
            cat = float(cat) if cat is not None else 0.0
            if moneyline_only and abs(cat) > 1e-6:
                continue
            if not (0.02 < sharp < 0.98) or not (0.02 < mkt < 0.98):
                continue
            valid_sharp += 1
            edge = sharp - mkt
            if abs(edge) < min_edge:
                continue
            trades += 1
            # sharp 是否比市场更接近真结果
            if abs(sharp - label) < abs(mkt - label):
                sharp_better += 1
            # follow sharp: 买 sharp 偏好边, 进场价 = 市场该边价 (+半价差近似吃单成本)
            if edge > 0:
                entry = mkt
                payoff = label
            else:
                entry = 1 - mkt
                payoff = 1 - label
            half_spread = 0.01 if use_ask_proxy else 0.0  # 近似吃单半价差
            entry_eff = entry + half_spread
            fee = FEE * entry * (1 - entry)
            pnl = payoff - entry_eff - fee
            pnl_sum += pnl
            if pnl > 0:
                wins += 1
            b = round(abs(edge), 2)
            bk = "0.02-0.05" if b < 0.05 else ("0.05-0.10" if b < 0.10 else ("0.10-0.20" if b < 0.20 else ">0.20"))
            buckets.setdefault(bk, [0, 0.0])
            buckets[bk][0] += 1
            buckets[bk][1] += pnl
    print(f"--- min_edge={min_edge} moneyline_only={moneyline_only} ask近似={use_ask_proxy} ---")
    print(f"已结算行={n_settled}  有效sharp行(f18+f8 valid)={valid_sharp}  sharp信号交易={trades}")
    if trades:
        print(f"sharp 比市场更准: {100*sharp_better/trades:.1f}%  (>50% = sharp 有信息)")
        print(f"sharp-follow 净 PnL/trade = {pnl_sum/trades:+.4f}  胜率(pnl>0)={100*wins/trades:.1f}%  总PnL={pnl_sum:+.1f}")
        print("  按 sharp 偏离档:")
        for k in ["0.02-0.05", "0.05-0.10", "0.10-0.20", ">0.20"]:
            if k in buckets:
                n, p = buckets[k]
                print(f"    {k}: n={n} 净PnL/trade={p/n:+.4f} 总={p:+.1f}")


if __name__ == "__main__":
    for me in (0.02, 0.05, 0.10):
        run(me)
        print()
