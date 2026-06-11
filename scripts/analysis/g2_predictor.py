#!/usr/bin/env python3
"""g2_predictor.py — 承重墙实验: t0 特征能否 OOS 选出 net>0 的可交易事件?

老板 2026-06-03 路口①。G2 复核结论: 朴素全事件套利净负(进 ask 吃掉收敛); edge 真在 ~17-22%
会重定价的盘上, 但【进场流动性挑不出】(复核A)。→ 套利可行的唯一前提 = 模型能在 t0 选出
"会朝 sharp 重定价"的事件。本实验直接验证: walk-forward 训 lightgbm 预测每个 score 事件的
净 edge, 看模型【选中交易的子集】OOS 实测净 edge 能否 > 0(朴素全交易是负的基线)。

label = 真 ask/bid 调整净 edge(买@ask/卖@bid, 10s 后市值 mp, 扣 taker fee, 出场 maker)。
特征(t0 可得, 防未来函数): gap0/|gap0|/spread/depth/imbalance/ofi/realized_vol/score_diff/
  mp 动量(1,3 tick)/sharp 动量。walk-forward 按 ts 时间切(训过去测未来)。
服务器跑: ~/stcpp-ops/run.sh '.venv/bin/python3 /tmp/g2p.py'
"""
import json
import sys
from collections import defaultdict

FV = "data/ml_capture/quotes.jsonl.fv.jsonl"
H_NS = 10e9
FEE_RATE = 0.03
MAX = int(sys.argv[1]) if len(sys.argv) > 1 else 30_000_000
# 采集字段 (idx → name): score_total/mp/bid/ask/sharp/imbalance/depth/score_diff/ofi/rvol
KEEP = {"f1": "st", "f9": "mp", "f13": "bid", "f14": "ask", "f18": "sharp",
        "f10": "imb", "f12": "depth", "f0": "sd", "f30": "ofi", "f108": "rvol"}


def num(v):
    try:
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def main():
    import numpy as np
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
        c = r.get("condition_id"); ts = r.get("as_of_ts_ns") or 0
        if not c or not ts:
            continue
        row = {nm: num(r.get(k)) for k, nm in KEEP.items()}
        row["ts"] = ts
        seqs[c].append(row)
    print(f"[g2p] 扫 {n} 行 → {len(seqs)} condition", file=sys.stderr)

    X, y, T = [], [], []  # 特征 / net 标签 / 事件时间(walk-forward 切)
    for c, rows in seqs.items():
        rows.sort(key=lambda x: x["ts"])
        for i in range(3, len(rows)):
            cur = rows[i]; prev = rows[i - 1]
            if cur["st"] is None or prev["st"] is None or cur["st"] <= prev["st"]:
                continue  # 非 score 增事件
            sh = cur["sharp"]; mp0 = cur["mp"]; bid0 = cur["bid"]; ask0 = cur["ask"]
            if sh is None or not (0 < sh < 1) or mp0 is None or bid0 is None or ask0 is None:
                continue
            if not (0 < bid0 < ask0 < 1):
                continue
            # 未来 mp @ t0+10s
            mph = None
            for j in range(i + 1, len(rows)):
                if rows[j]["ts"] >= cur["ts"] + H_NS and rows[j]["mp"] is not None:
                    mph = rows[j]["mp"]; break
            if mph is None:
                continue
            gap0 = sh - mp0
            if gap0 == 0:
                continue
            # net edge (买@ask 或 卖@bid, 扣 taker fee, 出场 maker)
            if gap0 > 0:
                entry = ask0; gross = mph - entry
            else:
                entry = bid0; gross = entry - mph
            net = gross - FEE_RATE * entry * (1 - entry)
            # t0 特征 (防未来: 全用 i 及更早)
            def g(row, k, d=0.0):
                v = row.get(k); return d if v is None or v != v else v
            mp_m1 = mp0 - g(rows[i - 1], "mp", mp0)
            mp_m3 = mp0 - g(rows[i - 3], "mp", mp0)
            sh_m1 = sh - g(rows[i - 1], "sharp", sh)
            feat = [gap0, abs(gap0), ask0 - bid0, g(cur, "depth"), g(cur, "imb"), g(cur, "ofi"),
                    g(cur, "rvol"), g(cur, "sd"), mp0, mp_m1, mp_m3, sh_m1]
            X.append(feat); y.append(net); T.append(cur["ts"])
    X = np.array(X); y = np.array(y); T = np.array(T)
    print(f"[g2p] 事件样本 n={len(y)} | 全交易基线 均net={y.mean():+.4f} net>0={100*(y>0).mean():.0f}%")
    if len(y) < 100:
        print("[g2p] 样本太少, 结论不可信"); return

    import lightgbm as lgb
    order = np.argsort(T)
    X, y = X[order], y[order]
    K = 5; fold = len(y) // K
    sel_nets, sel_cnt, base_nets = [], 0, []
    for f in range(1, K):  # walk-forward: 用前 f 折训, 测第 f 折
        tr_end = f * fold; te_end = (f + 1) * fold if f < K - 1 else len(y)
        Xtr, ytr = X[:tr_end], y[:tr_end]; Xte, yte = X[tr_end:te_end], y[tr_end:te_end]
        if len(Xte) < 20:
            continue
        m = lgb.LGBMRegressor(n_estimators=120, num_leaves=15, min_child_samples=20,
                              learning_rate=0.05, verbose=-1)
        m.fit(Xtr, ytr)
        pred = m.predict(Xte)
        traded = pred > 0  # 模型说 net>0 才交易
        base_nets.extend(yte.tolist())
        if traded.sum() > 0:
            sel_nets.extend(yte[traded].tolist()); sel_cnt += int(traded.sum())
    import statistics as st
    print(f"[g2p] === walk-forward OOS({K-1} 折测) ===")
    print(f"  全交易(基线): n={len(base_nets)} 均net={st.mean(base_nets):+.4f} net>0={100*sum(1 for x in base_nets if x>0)/len(base_nets):.0f}%")
    if sel_nets:
        pos = sum(1 for x in sel_nets if x > 0)
        print(f"  模型选中交易: n={sel_cnt}({100*sel_cnt/len(base_nets):.0f}% 覆盖) 均net={st.mean(sel_nets):+.4f} net>0={100*pos/len(sel_nets):.0f}%")
        print("[g2p] 判读: 模型选中子集 均net>0 且明显高于全交易基线 → 模型是承重墙, 套利活, 可建引擎A。")
        print("           若选中子集仍 ≤0 或与基线无异 → 模型选不出, 纯价差套利在此数据/分辨率下不成立。")
    else:
        print("  模型从不交易(pred 全≤0) → 选不出正 edge 事件。")


if __name__ == "__main__":
    main()
