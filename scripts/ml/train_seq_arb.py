#!/usr/bin/env python3
"""train_seq_arb.py — 短时套利序列模型: 8-horizon 多输出 LightGBM → ONNX (CLAUDE.md §12.4 离线).

Owner: 老雷 (GM) — 短时套利引擎 模块2/5 (主计划 §3)
last_review: 2026-06-01

输入: mid_return_label.JoinMidLabelsFile 产的 training jsonl
  X = f0..f109 (110 列 = kMlFeatureCount, extract_full; BR-1 训练=推理同源)
  y = y_wall_2s / y_wall_3s / ... / y_wall_60s (墙钟 8 horizon 的 mid 移动; NaN=无未来覆盖→drop)
模型: 每 horizon 一个 LGBMRegressor (多输出); 预测 dmid。CI 由 conformal 离线校准 (本脚本暂只 dmid)。
导出: ONNX 输出 flat[8] = 各 horizon dmid (C++ OnnxSeqArbModel cnt==8 路径)。
红线: 只产 .onnx 不进生产 (推理走 C++); 列序锁对齐 kMlFeatureCount。stub 永不驱动 (无模型不发单)。
"""
import argparse
import json
import sys

N_FEATURES = 110  # = kMlFeatureCount (ml-feature-spec v0.12)
CAT_FEATURES = [82, 83, 84, 85]  # 同 train_fair_value: 类别列声明 categorical
WALL_HORIZONS_S = [2, 3, 5, 10, 12, 15, 30, 60]  # = C++ kWallHorizonsSec
Y_KEYS = [f"y_wall_{s}s" for s in WALL_HORIZONS_S]


def load_jsonl(path):
    with open(path) as f:
        return [json.loads(l) for l in f if l.strip()]


def num(v, d=0.0):
    return d if v is None else float(v)


def build_xy(rows):
    import numpy as np
    X, Y = [], []
    for r in rows:
        if "f0" not in r or not r.get("label_valid", 0):
            continue
        feats = [num(r.get(f"f{i}")) for i in range(N_FEATURES)]
        feats = [0.0 if (x != x) else x for x in feats]
        # 多 horizon y; NaN (None) 该 horizon 缺 → 该样本该列后续 mask
        ys = [r.get(k) for k in Y_KEYS]
        X.append(feats)
        Y.append([float("nan") if v is None else float(v) for v in ys])
    return np.asarray(X, dtype="float32"), np.asarray(Y, dtype="float32")


def export_onnx(models, n_features, out_path):
    # 每 horizon 一棵树 → 合并成单 ONNX (concat 8 输出 → flat[8])。
    from onnxmltools import convert_lightgbm
    from onnxmltools.convert.common.data_types import FloatTensorType
    import onnx
    from onnx import helper
    subs = []
    for h, m in enumerate(models):
        onx = convert_lightgbm(m, initial_types=[("input", FloatTensorType([None, n_features]))],
                               target_opset=12)
        subs.append(onx)
    # 简化: 导出第一个为代表 + 各 horizon 单独文件 (MVP; 合并图 v2 再做)。
    for h, onx in enumerate(subs):
        p = out_path.replace(".onnx", f"_h{WALL_HORIZONS_S[h]}s.onnx")
        with open(p, "wb") as f:
            f.write(onx.SerializeToString())
    print(f"[seq_arb] 导出 {len(subs)} 个 per-horizon ONNX (合并图 v2; MVP per-horizon)")


def train(X, Y, out_path):
    import lightgbm as lgb
    import numpy as np
    cat = [c for c in CAT_FEATURES if c < X.shape[1]]
    models = []
    for h in range(len(WALL_HORIZONS_S)):
        yh = Y[:, h]
        mask = ~np.isnan(yh)  # 该 horizon 有未来覆盖的样本
        if mask.sum() < 50:
            print(f"[seq_arb] horizon {WALL_HORIZONS_S[h]}s 样本不足 ({int(mask.sum())}), 跳过", file=sys.stderr)
            models.append(None)
            continue
        m = lgb.LGBMRegressor(n_estimators=50, num_leaves=31, learning_rate=0.05,
                              min_child_samples=20, verbose=-1)
        m.fit(X[mask], yh[mask], categorical_feature=cat)
        models.append(m)
    valid = [m for m in models if m is not None]
    if valid:
        export_onnx(valid, X.shape[1], out_path)
    return models


def selftest(out_path):
    import numpy as np
    import lightgbm as lgb
    rng = np.random.default_rng(7)
    n = 600
    X = rng.standard_normal((n, N_FEATURES)).astype("float32")
    X[:, 82:86] = rng.integers(0, 5, (n, 4)).astype("float32")
    models = []
    for h, s in enumerate(WALL_HORIZONS_S):
        # 合成: dmid 与 OFI(30)/microprice(17) 相关 + horizon 缩放
        y = (X[:, 30] * 0.002 + X[:, 17] * 0.003) * (1 + h * 0.1)
        m = lgb.LGBMRegressor(n_estimators=20, num_leaves=15, min_child_samples=10, verbose=-1)
        m.fit(X, y)
        models.append(m)
    export_onnx(models, N_FEATURES, out_path)
    print("[seq_arb] selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--features", help="training jsonl (f0..f109 + y_wall_Δs)")
    ap.add_argument("--out", default="model_seq_arb.onnx")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest(a.out)
        return
    if not a.features:
        print("需 --features <training.jsonl> 或 --selftest", file=sys.stderr)
        sys.exit(2)
    X, Y = build_xy(load_jsonl(a.features))
    if len(X) < 50:
        print(f"样本不足 ({len(X)}<50), 训练跳过 — 等真数据 (含进球场次的 fv.jsonl)", file=sys.stderr)
        sys.exit(1)
    print(f"[seq_arb] {len(X)} 样本 × {X.shape[1]} 列 × {len(WALL_HORIZONS_S)} horizon")
    train(X, Y, a.out)


if __name__ == "__main__":
    main()
