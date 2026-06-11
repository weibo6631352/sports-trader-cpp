#!/usr/bin/env python3
"""g2_recheck.py — G2 复核: ① entry 流动性能否预测重定价 ② ask/bid 真净 edge。

G2 初查发现: PM 真重定价时(~22%事件)~70% 朝 sharp 收敛 ~1.5-2%。但两 caveat 待钉:
  A. "条件于重定价"是事后的 → 进场(t0)时能否用【流动性】预测哪些盘会重定价?(可选性)
  B. 收敛测的 microprice → 真套利净 edge 要用【ask 进场/bid 出场】算(扣穿价成本+fee)。

字段: f1=score_total f9=microprice f13=best_bid f14=best_ask f18=bet365 sharp。
gap0=sharp−mp; gap0>0 → 买 YES @ask(f14), Δ 后市值=mp; gap0<0 → 买 NO≡卖 YES @bid(f13)。
net = 毛 − entry taker fee(rate·p·(1−p), 体育 rate=0.03; 出场 maker≈0 fee)。
服务器跑: ~/stcpp-ops/run.sh 'python3 /tmp/g2r.py 8000000'
"""
import json
import sys
import statistics as st
from collections import defaultdict

FV = "data/ml_capture/quotes.jsonl.fv.jsonl"
H_NS = 10e9          # 复核用 10s horizon
FEE_RATE = 0.03      # 体育 taker
MAX = int(sys.argv[1]) if len(sys.argv) > 1 else 8_000_000


def num(r, k):
    v = r.get(k)
    try:
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def main():
    seqs = defaultdict(list)
    n = 0
    for line in open(FV):
        n += 1
        if n > MAX:
            break
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        c = r.get("condition_id")
        ts = r.get("as_of_ts_ns") or 0
        st_ = num(r, "f1"); mp = num(r, "f9"); bid = num(r, "f13"); ask = num(r, "f14"); sh = num(r, "f18")
        if c and ts and st_ is not None and mp is not None:
            seqs[c].append((ts, st_, mp, bid, ask, sh))
    print(f"[g2r] 扫 {n} 行 → {len(seqs)} condition", file=sys.stderr)

    # 按 entry spread 分桶: tight<2% / mid 2-5% / wide>5%(含幻影)。
    buckets = {"tight(<2%)": (0, 0.02), "mid(2-5%)": (0.02, 0.05), "wide(>5%)": (0.05, 9)}
    repriced = defaultdict(int); total = defaultdict(int); conv = defaultdict(int)
    nets = defaultdict(list)  # bucket → net edge list (ask/bid 调整)
    all_net = []
    for c, rows in seqs.items():
        rows.sort(key=lambda x: x[0])
        for i in range(1, len(rows)):
            ts0, s0, mp0, bid0, ask0, sh0 = rows[i]
            if s0 <= rows[i - 1][1]:
                continue
            if sh0 is None or not (0 < sh0 < 1) or bid0 is None or ask0 is None:
                continue
            if not (0 < bid0 < ask0 < 1):
                continue
            spread = ask0 - bid0
            gap0 = sh0 - mp0
            if gap0 == 0:
                continue
            # 找 t0+10s 后 mp
            mph = None
            for j in range(i + 1, len(rows)):
                if rows[j][0] >= ts0 + H_NS:
                    mph = rows[j][2]; break
            if mph is None:
                continue
            # 分桶 (A: 流动性→重定价)
            bk = next((k for k, (lo, hi) in buckets.items() if lo <= spread < hi), "wide(>5%)")
            total[bk] += 1
            moved = abs(mph - mp0) > 0.005
            if moved:
                repriced[bk] += 1
                if (1 if gap0 > 0 else -1) * (mph - mp0) > 0:
                    conv[bk] += 1
            # B: ask/bid 真净 edge
            if gap0 > 0:   # 买 YES @ask, Δ后市值 mp
                entry = ask0; gross = mph - entry
            else:          # 买 NO ≡ 卖 YES @bid, Δ后市值 mp(profit if mp 跌)
                entry = bid0; gross = entry - mph
            fee = FEE_RATE * entry * (1 - entry)  # taker 进场; 出场 maker≈0
            net = gross - fee
            nets[bk].append(net); all_net.append(net)

    print("[g2r] === A. entry 流动性(spread)→ 重定价/收敛 (Δ10s) ===")
    for bk in buckets:
        t = total[bk]
        if not t:
            print(f"  {bk}: 无样本"); continue
        rp = repriced[bk]
        print(f"  {bk}: n={t} 重定价率={100*rp/t:.0f}% 重定价中收敛率={100*conv[bk]/max(1,rp):.0f}%")
    print("[g2r] === B. ask/bid 真净 edge (买@ask/卖@bid − taker fee, 出场 maker) ===")
    for bk in buckets:
        ns = nets[bk]
        if not ns:
            print(f"  {bk}: 无样本"); continue
        pos = sum(1 for x in ns if x > 0)
        print(f"  {bk}: n={len(ns)} net>0={100*pos/len(ns):.0f}% 均net={st.mean(ns):+.4f} 中位={st.median(ns):+.4f}")
    if all_net:
        pos = sum(1 for x in all_net if x > 0)
        print(f"  全部: n={len(all_net)} net>0={100*pos/len(all_net):.0f}% 均={st.mean(all_net):+.4f} 中位={st.median(all_net):+.4f}")
    print("[g2r] 判读: 若 tight 桶 重定价率高 + 真净 edge 均>0 → 进场用 spread 门选盘, 套利净正可建引擎A。")


if __name__ == "__main__":
    main()
