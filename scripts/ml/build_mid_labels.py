#!/usr/bin/env python3
"""build_mid_labels.py — fv.jsonl → seq-arb 训练 jsonl (未来 Δt mid 移动标签).

Owner: 老雷 (GM) — 短时套利训练数据 label-join (CLAUDE.md §12.4 离线; 纯 stdlib 不进生产).
last_review: 2026-06-01

输入: data/ml_capture/quotes.jsonl.fv.jsonl (FeatureVectorRecord 落盘)
  每行: {condition_id, as_of_ts_ns, fair_value, f0..f109}  (f8 = b_mid 订单簿 mid)
输出: 训练 jsonl, 每行 {f0..f109, y_wall_2s..y_wall_60s, label_valid}
  y_wall_Δs = mid(t+Δ) − mid(t)  (PIT 无外推: t+Δ 超过该 condition 末样本 → 该 horizon NaN/None)
  label_valid = 至少一个 horizon 有未来覆盖.

红线: 纯离线 label 计算; PIT 严格 (只用未来真实样本, 不外推); 列序对齐 kMlFeatureCount=110.
"""
import argparse
import bisect
import json
import sys

N_FEATURES = 110
WALL_HORIZONS_S = [2, 3, 5, 10, 12, 15, 30, 60]  # = C++ kWallHorizonsSec
MID_COL = 8  # f8 = b_mid (订单簿 mid, dollar prob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fv", required=True, help="quotes.jsonl.fv.jsonl")
    ap.add_argument("--out", required=True, help="训练 jsonl 输出")
    a = ap.parse_args()

    # 1. 读全部行, 按 condition 分组
    rows_by_cond = {}
    n_read = 0
    for line in open(a.fv):
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
        except Exception:
            continue
        cid = d.get("condition_id")
        ts = d.get("as_of_ts_ns")
        if cid is None or ts is None:
            continue
        mid = d.get("f%d" % MID_COL)
        if mid is None:
            continue
        rows_by_cond.setdefault(cid, []).append((int(ts), float(mid), d))
        n_read += 1

    n_out = 0
    n_valid = 0
    with open(a.out, "w") as fout:
        for cid, rows in rows_by_cond.items():
            rows.sort(key=lambda r: r[0])  # 按时间
            ts_list = [r[0] for r in rows]
            mid_list = [r[1] for r in rows]
            for i, (ts, mid, d) in enumerate(rows):
                ys = []
                valid = False
                for h in WALL_HORIZONS_S:
                    target = ts + h * 1_000_000_000  # Δs → ns
                    j = bisect.bisect_left(ts_list, target, i + 1)
                    if j < len(rows):  # PIT: 找到 t+Δ 之后第一个真实样本
                        ys.append(mid_list[j] - mid)
                        valid = True
                    else:
                        ys.append(None)  # 无未来覆盖 → NaN (train 时 mask)
                out = {("f%d" % k): d.get("f%d" % k) for k in range(N_FEATURES)}
                for h, y in zip(WALL_HORIZONS_S, ys):
                    out["y_wall_%ds" % h] = y
                out["label_valid"] = 1 if valid else 0
                fout.write(json.dumps(out) + "\n")
                n_out += 1
                if valid:
                    n_valid += 1

    print("[label] 读 %d 行, %d condition; 输出 %d 行 (%d 有标签) → %s"
          % (n_read, len(rows_by_cond), n_out, n_valid, a.out), file=sys.stderr)


if __name__ == "__main__":
    main()
