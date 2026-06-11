#!/usr/bin/env python3
"""g2_convergence.py — G2 命门验证: score 事件后 PM 是否朝 bet365 sharp 收敛?

老板 2026-06-03 v3 事件套利, 架构评审(老郭)定 G2 为引擎A 交易代码的前置闸门。
数据说话: 用已采集 tick(quotes.jsonl.fv.jsonl)实测——
  事件 = score_total(f1)增加(进球/得分/跑垒);
  在事件 tick t0 记 gap0 = sharp(f18) − microprice(f9);
  看 t0 之后 Δ∈{5,10,15}s 的 microprice 是否朝 sharp 方向移动(收敛)+ 幅度。
  y_realized = sign(gap0)·(mp(t0+Δ) − mp(t0))  ← 正=PM 朝 sharp 收敛(套利可吃)。

仅在 sharp 有效(f18∈(0,1))的 tick 可测(sharp 覆盖 ~11%, 仅 tennis/soccer)。
在服务器跑(数据在那, 2.4GB): ~/stcpp-ops/run.sh 'python3 /tmp/g2.py'
"""
import json
import sys
from collections import defaultdict

FV = "data/ml_capture/quotes.jsonl.fv.jsonl"
F_SCORE_TOTAL = "f1"
F_MICROPRICE = "f9"
F_SHARP = "f18"
F_CAT_SPORT = "f83"
HORIZONS_NS = [(5, 5e9), (10, 10e9), (15, 15e9)]
MAX_LINES = int(sys.argv[1]) if len(sys.argv) > 1 else 4_000_000


def num(r, k):
    v = r.get(k)
    try:
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def main():
    # 流式按 condition 收集 (ts, score_total, mp, sharp); 只留有 sharp 的 condition 省内存。
    seqs = defaultdict(list)
    n = 0
    for line in open(FV):
        n += 1
        if n > MAX_LINES:
            break
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except json.JSONDecodeError:
            continue
        c = r.get("condition_id")
        if not c:
            continue
        ts = r.get("as_of_ts_ns") or 0
        st = num(r, F_SCORE_TOTAL)
        mp = num(r, F_MICROPRICE)
        sh = num(r, F_SHARP)
        sport = num(r, F_CAT_SPORT)
        if ts and st is not None and mp is not None:
            seqs[c].append((ts, st, mp, sh, sport))
    print(f"[g2] 扫 {n} 行 → {len(seqs)} condition", file=sys.stderr)

    # 每 condition: 找 score_total 增加的事件; 测 t0 后各 horizon 的 y_realized。
    events = 0
    have_sharp = 0
    by_h = {h: [] for h, _ in HORIZONS_NS}  # h → list of y_realized
    gaps = []
    for c, rows in seqs.items():
        rows.sort(key=lambda x: x[0])
        for i in range(1, len(rows)):
            ts0, st0, mp0, sh0, sport = rows[i]
            _, st_prev, _, _, _ = rows[i - 1]
            if st0 <= st_prev:  # 非 score 增加事件
                continue
            events += 1
            if sh0 is None or not (0.0 < sh0 < 1.0):  # 无 sharp → 不可测套利
                continue
            have_sharp += 1
            gap0 = sh0 - mp0
            gaps.append(gap0)
            sign = 1.0 if gap0 > 0 else (-1.0 if gap0 < 0 else 0.0)
            for h, hns in HORIZONS_NS:
                # 找 t0+h 之后第一个 tick
                target = ts0 + hns
                mph = None
                for j in range(i + 1, len(rows)):
                    if rows[j][0] >= target:
                        mph = rows[j][2]
                        break
                if mph is not None:
                    by_h[h].append(sign * (mph - mp0))

    # 细分诊断 (找真因): 按运动 + 材料 gap + 条件于 PM 真重定价。
    SPORT_NAME = {34: "nba?", 104: "?", 8: "soccer?"}  # cat_sport id 仅参考
    by_h_moved = {h: [] for h, _ in HORIZONS_NS}     # 仅 PM 真动了(|Δmp|>0.005)的子集
    by_h_material = {h: [] for h, _ in HORIZONS_NS}   # 仅 |gap0|>0.03 材料错价
    by_sport = defaultdict(lambda: {h: [] for h, _ in HORIZONS_NS})
    for c, rows in seqs.items():
        rows.sort(key=lambda x: x[0])
        for i in range(1, len(rows)):
            ts0, st0, mp0, sh0, sport = rows[i]
            if st0 <= rows[i - 1][1]:
                continue
            if sh0 is None or not (0.0 < sh0 < 1.0):
                continue
            gap0 = sh0 - mp0
            sign = 1.0 if gap0 > 0 else (-1.0 if gap0 < 0 else 0.0)
            for h, hns in HORIZONS_NS:
                target = ts0 + hns
                mph = None
                for j in range(i + 1, len(rows)):
                    if rows[j][0] >= target:
                        mph = rows[j][2]
                        break
                if mph is None:
                    continue
                y = sign * (mph - mp0)
                if abs(mph - mp0) > 0.005:
                    by_h_moved[h].append(y)
                if abs(gap0) > 0.03:
                    by_h_material[h].append(y)
                sp = int(sport) if sport is not None else -1
                by_sport[sp][h].append(y)

    import statistics as st
    print(f"[g2] score 事件 {events} | 其中有 sharp 可测 {have_sharp} ({100*have_sharp/max(1,events):.1f}%)")
    if gaps:
        ag = [abs(g) for g in gaps]
        ag.sort()
        print(f"[g2] 事件时 |gap0|(sharp−mp): 中位 {st.median(ag):.4f} p75 {ag[int(len(ag)*0.75)]:.4f} 均 {st.mean(ag):.4f}")
    print("[g2] === y_realized = sign(gap0)·Δmp (正=PM 朝 sharp 收敛, 套利可吃) ===")
    for h, _ in HORIZONS_NS:
        ys = by_h[h]
        if not ys:
            print(f"  Δ={h}s: 无样本")
            continue
        ys_sorted = sorted(ys)
        pos = sum(1 for y in ys if y > 0)
        print(f"  Δ={h:2d}s: n={len(ys)} 收敛率(y>0)={100*pos/len(ys):.1f}% 均值={st.mean(ys):+.4f} "
              f"中位={st.median(ys):+.4f} p25={ys_sorted[len(ys)//4]:+.4f} p75={ys_sorted[3*len(ys)//4]:+.4f}")
    def summ(tag, d):
        print(f"[g2] --- {tag} ---")
        for h, _ in HORIZONS_NS:
            ys = d[h]
            if not ys:
                print(f"  Δ={h}s: 无样本"); continue
            pos = sum(1 for y in ys if y > 0)
            print(f"  Δ={h:2d}s: n={len(ys)} 收敛率={100*pos/len(ys):.1f}% 均={st.mean(ys):+.4f} 中位={st.median(ys):+.4f}")
    summ("仅 PM 真重定价子集(|Δmp|>0.5%) — 流动盘上收敛?", by_h_moved)
    summ("仅材料错价子集(|gap0|>3%) — 大 gap 更收敛?", by_h_material)
    print("[g2] --- 按 cat_sport(样本前5) ---")
    top = sorted(by_sport.items(), key=lambda kv: -len(kv[1][15]))[:5]
    for sp, d in top:
        ys = d[15]
        if ys:
            pos = sum(1 for y in ys if y > 0)
            print(f"  sport={sp}: Δ15s n={len(ys)} 收敛率={100*pos/len(ys):.1f}% 均={st.mean(ys):+.4f}")
    print("[g2] 判读: 收敛率>50% 且均值能覆盖 BE(中价 ~1.4%, maker出 ~0.7%) → 套利论点成立。")


if __name__ == "__main__":
    main()
