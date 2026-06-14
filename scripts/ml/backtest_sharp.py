#!/usr/bin/env python3
"""backtest_sharp.py — sharp 信号是否真 +EV (历史已结算回测)。

2026-06-14 修 (老板「有问题可以合理的改」): 原读 `quotes.jsonl.training.jsonl`(ML 训练捕获, 2026-06-05
  已删) + 老 ML f-列 schema(f18/f8/f84/label) → 文件不存在跑出来是空。改读现有真数据:
  market_tape.jsonl(全 in-play sharp 盘 60s 快照, 自带 sharp+PM mid 预join) × settlements(结局)。
  字段映射: f18(sharp)→`sharp`(YES sharp) / f8(市场)→`mid`(PM YES mid) / f84(类型)→`mkt_id`(0=moneyline)
  / label→settlement_value(YES赢=1)。

策略: 当 |sharp − 市场| > 阈值 → 买 sharp 偏好的一边 (sharp>市场买YES, 否则买NO), 按市场价进场, 结算。
对照: 若市场高效, 按市场价进场结算 = 0 EV; sharp-follow 的正 PnL = sharp 真 edge。

⚠ 伪重复 (同 param_research/market_discovery): market_tape 每盘多条 60s 快照, 但只 1 个独立结局 →
  按快照算 trades 会高估有效样本。故同时报【独立结算盘数】, 显著性/可信度按盘看, 不按快照。
⚠ 陈旧/冻结 sharp: 停盘的盘 sharp 是死值(假 edge) → 过滤 sh_age_ms 过大的快照 (未来 market_tape 落
  core_frozen 后可精确排除; 现用新鲜度近似)。

用法: python3 backtest_sharp.py [market_tape.jsonl] [settlements.jsonl] [--stale-ms N]
"""
import json
import sys

A = [x for x in sys.argv[1:] if not x.startswith("--")]
FLAGS = sys.argv[1:]
TAPE = A[0] if len(A) > 0 else "data/ml_capture/market_tape.jsonl"
SETTLE = A[1] if len(A) > 1 else "data/ml_capture/quotes.jsonl.settlements_merged.jsonl"
STALE_MS = float(FLAGS[FLAGS.index("--stale-ms") + 1]) if "--stale-ms" in FLAGS else 30000.0
FEE = 0.03  # 体育 moneyline 手续费率 (近似)


def load(path):
    out = []
    try:
        for line in open(path):
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except Exception:
                    pass
    except FileNotFoundError:
        print(f"⚠ 文件不存在: {path}")
    return out


def run(rows, outcome, min_edge, moneyline_only=True, use_ask_proxy=True):
    trades = 0
    pnl_sum = 0.0
    wins = 0
    sharp_better = 0
    buckets = {}
    valid_sharp = 0
    n_settled_snap = 0
    trade_conds = set()      # 交易触发的独立盘 (有效 n)
    settled_conds = set()
    for r in rows:
        cond = r.get("cond")
        if cond not in outcome:
            continue
        n_settled_snap += 1
        settled_conds.add(cond)
        # 陈旧/冻结过滤: sharp 太旧 = 死值, 排除 (假 edge 污染)
        if r.get("sh_age_ms", 0) > STALE_MS:
            continue
        if r.get("bvalid") != 1:
            continue
        sharp = r.get("sharp")
        mkt = r.get("mid")
        cat = r.get("mkt_id")
        label = outcome[cond]  # YES 赢=1 / NO 赢=0
        if sharp is None or mkt is None or label is None:
            continue
        sharp = float(sharp); mkt = float(mkt); label = float(label)
        cat = float(cat) if cat is not None else 0.0
        if moneyline_only and abs(cat) > 1e-6:  # mkt_id != 0 = 非 moneyline
            continue
        if not (0.02 < sharp < 0.98) or not (0.02 < mkt < 0.98):
            continue
        valid_sharp += 1
        edge = sharp - mkt
        if abs(edge) < min_edge:
            continue
        trades += 1
        trade_conds.add(cond)
        if abs(sharp - label) < abs(mkt - label):
            sharp_better += 1
        # follow sharp: 买 sharp 偏好边, 进场价 = 市场该边价 (+半价差近似吃单成本)
        if edge > 0:
            entry = mkt
            payoff = label
        else:
            entry = 1 - mkt
            payoff = 1 - label
        half_spread = 0.01 if use_ask_proxy else 0.0
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
    print(f"--- min_edge={min_edge} moneyline_only={moneyline_only} ask近似={use_ask_proxy} stale_ms={STALE_MS:.0f} ---")
    print(f"已结算快照={n_settled_snap}(独立盘 {len(settled_conds)})  有效sharp快照={valid_sharp}  "
          f"sharp信号交易={trades}(独立盘 {len(trade_conds)} ← 有效n)")
    if trades:
        print(f"sharp 比市场更准: {100*sharp_better/trades:.1f}%  (>50% = sharp 有信息)")
        print(f"sharp-follow 净 PnL/trade = {pnl_sum/trades:+.4f}  胜率(pnl>0)={100*wins/trades:.1f}%  总PnL={pnl_sum:+.1f}")
        print(f"  ⚠ 伪重复: {trades} 笔仅 {len(trade_conds)} 个独立盘 → PnL/trade 的可信度按【盘】非笔; n<~12盘当线索非结论")
        print("  按 sharp 偏离档:")
        for k in ["0.02-0.05", "0.05-0.10", "0.10-0.20", ">0.20"]:
            if k in buckets:
                n, p = buckets[k]
                print(f"    {k}: n={n} 净PnL/trade={p/n:+.4f} 总={p:+.1f}")


if __name__ == "__main__":
    rows = load(TAPE)
    st = load(SETTLE)
    outcome = {s["condition_id"]: s.get("settlement_value") for s in st
               if s.get("parse_ok") != 0 and s.get("settlement_value", -1) != -1}
    print(f"market_tape={TAPE} ({len(rows)} 快照) | settlements={SETTLE} ({len(outcome)} 已结算盘)")
    print()
    for me in (0.02, 0.05, 0.10):
        run(rows, outcome, me)
        print()
