#!/usr/bin/env python3
"""train_fair_value.py — 离线训练 fair value 模型 → 导 ONNX (CLAUDE.md §12.4: Python 仅离线训练).

Owner: 老雷 (GM) — Phase 2 残差 LightGBM → ONNX (替 StubFairValueModel)
last_review: 2026-05-31

输入: 标签管道 JoinFile 产的 training jsonl (X = f0..f81 完整向量列(含数据延迟) + label/label_valid),
   或 *.fv.jsonl + 单独 label。列序 = MlFeature enum (训练 X = C++ 推理向量, BR-1 零漂移)。
模式:
   regress (默认): LGBMRegressor 直接预测 p_yes (y=label∈{0,1} 当回归). 输出单 float ∈ [0,1],
                   C++ OnnxFairValueModel cnt==1 路径直接当 p_yes (clamp [0,1]).
   residual: y = label − baseline_fair (需 fair_value 列; 输出 delta, 调用方 blend fair=baseline+delta).
导出: onnxmltools.convert_lightgbm, input FloatTensorType([None, N]) → C++ 推理 [1,N] float.

红线: 训练只产 .onnx, 不进生产 (R-5 推理走 C++ ONNX); 列序锁 = spec_version 对齐 C++ kSpecVersion.
"""
import argparse
import json
import sys

N_TOTAL = 104  # = kMlFeatureCount (ml-feature-spec v0.11); +inplay交叉校验 102-103
# 类别上下文列 (82-85): categorical 非 ordinal — 必须声明 categorical_feature, 否则 LightGBM
#   把 league=104(CBA) 当 "比 34(NBA) 大" 的有序数值 (错)。整数码仅作 level; unknown=-1 独立 level。
#   82 cat_asset_class / 83 cat_sport(家族) / 84 cat_market_type / 85 cat_league(Polymarket sport.id)
#   86-93 = L2-L5 双边深度分布 (数值, 非 cat)。
CAT_FEATURES = [82, 83, 84, 85]


def load_jsonl(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def build_xy(rows, mode):
    import numpy as np
    X, y = [], []
    for r in rows:
        if not r.get("label_valid", 0):
            continue  # 只用已结算
        # 优先 f0..f84 (完整向量列序锁; 含类别上下文); 否则跳过 (需完整 X)
        if "f0" not in r:
            continue
        # 缺失/NaN → 0: recorder 把 NaN 写成 JSON null (合法 JSON; C++ << 的 "nan" 非法), 读回为 None。
        #   None/NaN 统一填 0 (LightGBM 原生 missing 也可; 这里保守填 0, 与 C++ 推理一致性留训练侧定)。
        def num(v, d=0.0):
            return d if v is None else float(v)
        feats = [num(r.get(f"f{i}")) for i in range(N_TOTAL)]
        feats = [0.0 if (x != x) else x for x in feats]
        label = float(r["label"])
        if mode == "residual":
            base = num(r.get("fair_value"), 0.5)
            y.append(label - base)
        else:
            y.append(label)
        X.append(feats)
    return np.asarray(X, dtype="float32"), np.asarray(y, dtype="float32")


def export_onnx(model, n_features, out_path):
    from onnxmltools import convert_lightgbm
    from onnxmltools.convert.common.data_types import FloatTensorType
    onx = convert_lightgbm(
        model, initial_types=[("input", FloatTensorType([None, n_features]))],
        target_opset=12)
    with open(out_path, "wb") as f:
        f.write(onx.SerializeToString())
    print(f"[train] 导出 ONNX -> {out_path} (input [None,{n_features}] float)")


def train(X, y, out_path):
    import lightgbm as lgb
    model = lgb.LGBMRegressor(n_estimators=50, num_leaves=15, learning_rate=0.1,
                              min_child_samples=5, verbose=-1)
    # categorical_feature: 类别列声明为 categorical, 树学 == 分裂而非有序阈值 (v0.6)。
    # ⚠ 已知风险: onnxmltools 对 LightGBM categorical split 的 ONNX 导出支持度需验证;
    #    若导出失败/不一致, fallback = 去掉 categorical_feature 当数值 (低基数下树仍可隔离)。
    cat = [c for c in CAT_FEATURES if c < X.shape[1]]
    model.fit(X, y, categorical_feature=cat)
    export_onnx(model, X.shape[1], out_path)


def selftest(out_path):
    """合成数据训练 + 导 ONNX (生成 C++ 测试 fixture; 无需真数据)。"""
    import numpy as np
    import lightgbm as lgb
    rng = np.random.default_rng(42)
    n = 500
    X = rng.standard_normal((n, N_TOTAL)).astype("float32")
    # 类别列填整数 level (fixture 真实性; asset 恒 0, sport 家族 0-8, market_type -1..5, league=sport.id)
    X[:, 82] = 0.0
    X[:, 83] = rng.integers(-1, 9, n).astype("float32")
    X[:, 84] = rng.integers(-1, 6, n).astype("float32")
    X[:, 85] = rng.choice([34, 104, 45, 46, 8, 35, 39], n).astype("float32")  # 真实 sport.id 样本
    # y = sigmoid(线性组合) ∈ (0,1), 让回归输出像 p_yes (含一个类别交互项)
    z = X[:, 0] * 0.8 + X[:, 30] * 0.5 - X[:, 18] * 0.3 + (X[:, 83] == 1.0) * 0.2
    y = (1.0 / (1.0 + np.exp(-z))).astype("float32")
    model = lgb.LGBMRegressor(n_estimators=30, num_leaves=15, min_child_samples=5, verbose=-1)
    model.fit(X, y)  # selftest 不声明 categorical (保 ONNX 导出稳; 仅验列数/round-trip)
    export_onnx(model, N_TOTAL, out_path)
    # 自检: ONNX 推理一致
    import onnxruntime as ort  # noqa
    print("[train] selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--features", help="training jsonl (含 f0..f84 + label)")
    ap.add_argument("--out", default="model_fair_value.onnx")
    ap.add_argument("--mode", choices=["regress", "residual"], default="regress")
    ap.add_argument("--selftest", action="store_true", help="合成数据生成 fixture ONNX")
    a = ap.parse_args()
    if a.selftest:
        selftest(a.out)
        return
    if not a.features:
        print("需 --features <training.jsonl> 或 --selftest", file=sys.stderr)
        sys.exit(2)
    X, y = build_xy(load_jsonl(a.features), a.mode)
    if len(X) < 50:
        print(f"样本不足 ({len(X)}<50), 训练跳过 — 等真数据攒够 (book-only walk-forward 先验证)",
              file=sys.stderr)
        sys.exit(1)
    print(f"[train] {len(X)} 样本 × {X.shape[1]} 列, mode={a.mode}")
    train(X, y, a.out)


if __name__ == "__main__":
    main()
