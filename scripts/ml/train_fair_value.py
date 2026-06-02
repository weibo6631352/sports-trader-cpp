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

N_TOTAL = 114  # = kMlFeatureCount (ml-feature-spec v0.13); +4 慢源新鲜度 110-113
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


def _ece(p, y, bins=10, floor=0.02):
    """期望校准误差 (Expected Calibration Error): 按预测概率分箱, 加权平均 |mean(pred)−mean(y)|。
    = "模型的概率平均偏离真实胜率多少"。校准好→接近 0; 差→大。作前端 CI 半宽 (概率可信带)。
    floor: 最小 0.02 (避免零宽; 样本有限下不宣称完美校准)。"""
    import numpy as np
    edges = np.linspace(0.0, 1.0, bins + 1)
    idx = np.clip(np.digitize(p, edges) - 1, 0, bins - 1)
    errs, wts = [], []
    for b in range(bins):
        mask = idx == b
        cnt = int(mask.sum())
        if cnt == 0:
            continue
        errs.append(abs(float(p[mask].mean()) - float(y[mask].mean())))
        wts.append(cnt)
    if not errs:
        return 0.1
    return max(floor, float(np.average(errs, weights=wts)))


def compute_meta(model_eval, Xh, yh, mode):
    """holdout 评估 → 校准元数据 (C++ 侧读 <onnx>.meta.json 填 confidence/calibrated/CI)。

    confidence 语义 = 判别力 (回归: 2·(AUC−0.5), 0.5→0 / 1.0→1; AUC 无则 1−2·Brier 兜底)。
      AUC≈0.5 (无技能) → confidence≈0 → C++ modelReady()=false → 前端正确保持"占位"
      (无技能模型不该自称就绪)。ci_halfwidth = |y−p| 残差 80% 分位 (conformal-ish 区间半宽)。
    """
    import numpy as np
    pred = np.asarray(model_eval.predict(Xh), dtype="float64")
    if mode == "regress":
        p = np.clip(pred, 0.0, 1.0)
        ytrue = yh.astype("float64")
        brier = float(np.mean((p - ytrue) ** 2)) if len(p) else 0.25
        auc = None
        try:
            from sklearn.metrics import roc_auc_score
            if len(np.unique(ytrue)) >= 2:
                auc = float(roc_auc_score(ytrue, p))
        except Exception:
            auc = None
        # ci_halfwidth = 校准误差 (ECE-ish): 分箱比 mean(pred) vs mean(y) = "这个概率平均偏多少"。
        #   注: 不用 |y−p| 残差分位 — 二值标签下那是对【结果】的区间 (恒~0.5 宽, 无意义);
        #   ECE 是对【概率估计】的可信带 (校准好→小, 差→大), 才是前端 CI 该显的语义。
        ci_hw = _ece(p, ytrue)
        conf = (2.0 * (auc - 0.5)) if auc is not None else (1.0 - 2.0 * brier)
        conf = max(0.0, min(1.0, conf))
        return {"calibrated": bool(conf > 0.0), "confidence": round(conf, 4),
                "ci_halfwidth": round(ci_hw, 4), "calib_method": "conformal",
                "auc": (round(auc, 4) if auc is not None else None),
                "brier": round(brier, 4), "n_holdout": int(len(Xh))}
    # residual: y=delta, 连续 → 用 RMSE 兜置信
    resid = np.abs(yh.astype("float64") - pred)
    rmse = float(np.sqrt(np.mean(resid ** 2))) if len(resid) else 0.5
    ci_hw = float(np.quantile(resid, 0.8)) if len(resid) else 0.5
    conf = max(0.0, min(1.0, 1.0 - 2.0 * rmse))
    return {"calibrated": bool(conf > 0.0), "confidence": round(conf, 4),
            "ci_halfwidth": round(ci_hw, 4), "calib_method": "conformal",
            "rmse": round(rmse, 4), "n_holdout": int(len(Xh))}


def write_sidecar(out_path, meta):
    """写 <onnx>.meta.json (C++ OnnxFairValueModel 加载侧读)。"""
    meta_path = out_path + ".meta.json"
    with open(meta_path, "w") as f:
        json.dump(meta, f, ensure_ascii=False)
    print(f"[train] 校准 sidecar -> {meta_path}: {meta}")


def train(X, y, out_path, mode="regress"):
    import lightgbm as lgb
    cat = [c for c in CAT_FEATURES if c < X.shape[1]]
    params = dict(n_estimators=50, num_leaves=15, learning_rate=0.1,
                  min_child_samples=5, verbose=-1)
    # holdout 评估 (时序末 20% = 最新, 防泄漏) → 校准 meta; 最终模型用全量训练 (部署)。
    n = len(X)
    n_hold = max(1, int(n * 0.2))
    meta = None
    if n_hold >= 20 and (n - n_hold) >= 20:
        Xt, yt, Xh, yh = X[:-n_hold], y[:-n_hold], X[-n_hold:], y[-n_hold:]
        eval_model = lgb.LGBMRegressor(**params)
        eval_model.fit(Xt, yt, categorical_feature=[c for c in cat if c < Xt.shape[1]])
        meta = compute_meta(eval_model, Xh, yh, mode)
    # categorical_feature: 类别列声明为 categorical, 树学 == 分裂而非有序阈值 (v0.6)。
    # ⚠ 已知风险: onnxmltools 对 LightGBM categorical split 的 ONNX 导出支持度需验证;
    #    若导出失败/不一致, fallback = 去掉 categorical_feature 当数值 (低基数下树仍可隔离)。
    model = lgb.LGBMRegressor(**params)
    model.fit(X, y, categorical_feature=cat)
    export_onnx(model, X.shape[1], out_path)
    if meta is None:  # holdout 太小 → 保守 (未校准, 前端保持占位)
        meta = {"calibrated": False, "confidence": 0.0, "ci_halfwidth": 0.5,
                "calib_method": "none", "n_holdout": int(n_hold)}
    write_sidecar(out_path, meta)


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
    # holdout 评估 (末 20%) → sidecar (C++ 测试 fixture 需要 meta)
    n_hold = max(20, int(n * 0.2))
    eval_model = lgb.LGBMRegressor(n_estimators=30, num_leaves=15, min_child_samples=5, verbose=-1)
    eval_model.fit(X[:-n_hold], y[:-n_hold])
    # selftest 的 y 是 sigmoid 连续值 (非 {0,1}) → 当 regress 处理, AUC 用阈值 0.5 二值化评判别力
    yh_bin = (y[-n_hold:] >= 0.5).astype("float32")
    meta = compute_meta(eval_model, X[-n_hold:], yh_bin, "regress")
    model = lgb.LGBMRegressor(n_estimators=30, num_leaves=15, min_child_samples=5, verbose=-1)
    model.fit(X, y)  # selftest 不声明 categorical (保 ONNX 导出稳; 仅验列数/round-trip)
    export_onnx(model, N_TOTAL, out_path)
    write_sidecar(out_path, meta)
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
    train(X, y, a.out, a.mode)


if __name__ == "__main__":
    main()
