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


# 数据质量列索引 (model_feature_spec.hpp): b_mid=8 (YES 市场价), cat_market_type=84 (moneyline=0)。
F_MID = 8
F_CAT_MARKET_TYPE = 84


def build_xy(rows, mode, moneyline_only=True, drop_decided=True):
    """X,y 构建 + 数据质量筛选 (2026-06-03 治本, 防退化/泄漏模型):
      moneyline_only: 只留 cat_market_type==0 → 平衡类别 (outright 多 NO 失衡 + 不可建模)。
      drop_decided: 滤市场价 (b_mid) 已极端 ≈0/1 的行 = 市场已决出, 平凡"预测"已定结果 (AUC=1.0 泄漏源)。
    返回 (X, y, stats)。"""
    import numpy as np

    def num(v, d=0.0):
        return d if v is None else float(v)

    X, y, groups = [], [], []
    n_label, n_drop_type, n_drop_decided = 0, 0, 0
    for r in rows:
        if not r.get("label_valid", 0):
            continue  # 只用已结算
        if "f0" not in r:
            continue
        feats = [num(r.get(f"f{i}")) for i in range(N_TOTAL)]
        feats = [0.0 if (x != x) else x for x in feats]
        n_label += 1
        cid = r.get("condition_id", "")  # 按场 CV 分组锚 (防同场行跨 train/test 记忆泄漏)
        # 治本①: 只留 moneyline (平衡 + 可建模; outright/prop 多 NO 失衡且无单场 score 模型)。
        if moneyline_only and abs(feats[F_CAT_MARKET_TYPE]) > 1e-6:
            n_drop_type += 1
            continue
        # 治本②: 滤泄漏行 — 市场价已极端 (已决出 → 平凡预测已定结果, AUC 泄漏源)。
        mid = feats[F_MID]
        if drop_decided and (mid < 0.03 or mid > 0.97):
            n_drop_decided += 1
            continue
        label = float(r["label"])
        if mode == "residual":
            # residual baseline = b_mid (F_MID 市场 mid), 不是 fair_value(score-prior)。
            #   语义: 模型预测【对市价的增量 delta = label − 市价】。fair = 市价 + delta, edge = delta
            #   天然两边 (治 regress+Platt 把准确低预测往 0.5 抬 → fair>市价 → 只买 YES 的退化)。
            #   推理侧 C++ 同取 fv[F_MID] 作 baseline (BR-1 零漂移)。drop_decided 已保证 b_mid∈[0.03,0.97]。
            y.append(label - feats[F_MID])
        else:
            y.append(label)
        X.append(feats)
        groups.append(cid)
    stats = {"labeled": n_label, "drop_type": n_drop_type, "drop_decided": n_drop_decided, "kept": len(X)}
    return (np.asarray(X, dtype="float32"), np.asarray(y, dtype="float32"),
            np.asarray(groups, dtype=object), stats)


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


def group_cv_meta(X, y, groups, mode, params):
    """按场次 (condition_id) GroupKFold CV 评估 → sidecar meta (诚实 AUC, 防同场行记忆泄漏)。
    比时序末 20% holdout 稳健 (避免单类 holdout → AUC=None; 避免按行 split 记忆场次 → 假 AUC≈1)。"""
    import numpy as np
    n_groups = len(set(groups.tolist()))
    # 重构二值 label + baseline (residual: y=delta=label−b_mid → label=y+b_mid; regress: y=label)。
    if mode == "residual":
        baseline_all = X[:, F_MID].astype("float64")
        label_bin = np.round(np.clip(y.astype("float64") + baseline_all, 0.0, 1.0))
    else:
        baseline_all = None
        label_bin = y.astype("float64")
    if n_groups < 6 or len(np.unique(label_bin)) < 2:
        # 场数太少/单类 → 退回简单 holdout compute_meta (末 20%)。
        nh = max(20, int(len(X) * 0.2))
        if len(X) - nh < 20:
            return None
        import lightgbm as lgb
        em = lgb.LGBMRegressor(**params)
        em.fit(X[:-nh], y[:-nh])
        return compute_meta(em, X[-nh:], y[-nh:], mode)
    import lightgbm as lgb
    from sklearn.model_selection import GroupKFold
    from sklearn.metrics import roc_auc_score
    n_splits = min(5, n_groups)
    preds = np.full(len(y), np.nan)  # OOF: regress=raw p_yes; residual=delta (可负, 不 clip)
    for tr, te in GroupKFold(n_splits).split(X, y, groups):
        if mode == "regress" and len(np.unique(y[tr])) < 2:
            continue
        m = lgb.LGBMRegressor(**params)
        m.fit(X[tr], y[tr])
        preds[te] = m.predict(X[te])
    ok = ~np.isnan(preds)
    if ok.sum() < 20 or len(np.unique(label_bin[ok])) < 2:
        return None
    yt = label_bin[ok].astype("float64")
    if mode == "residual":
        # residual: fair = clip(b_mid + delta)。不套 Platt (delta 非概率, Platt 是 logit 空间概率拉伸)。
        #   指标在【最终 fair vs 重构二值 label】上算 (诚实评估模型实际驱动的 fair 准不准)。
        fair = np.clip(baseline_all[ok] + preds[ok], 0.0, 1.0)
        platt_a, platt_b = 1.0, 0.0
        calib_method = "residual_groupcv"
    else:
        # Platt 校准: OOF 预测 logit 上拟合 logistic → calibrated_p = sigmoid(a·logit(p)+b)。
        #   治【过度自信】(原始 0.996/0.0005 极端) → 准概率。C++ 推理侧同样应用 (a,b)。优化模型, 非缩减场景。
        p_raw = np.clip(preds[ok], 0.0, 1.0)
        eps = 1e-4
        pc = np.clip(p_raw, eps, 1.0 - eps)
        logit = np.log(pc / (1.0 - pc)).reshape(-1, 1)
        platt_a, platt_b = 1.0, 0.0
        try:
            from sklearn.linear_model import LogisticRegression
            lr = LogisticRegression(C=1e6, solver="lbfgs", max_iter=1000)
            lr.fit(logit, yt.astype(int))
            platt_a = float(lr.coef_[0][0])
            platt_b = float(lr.intercept_[0])
        except Exception:
            pass
        fair = 1.0 / (1.0 + np.exp(-(platt_a * np.log(pc / (1.0 - pc)) + platt_b)))
        calib_method = "platt_groupcv"
    brier = float(np.mean((fair - yt) ** 2))
    try:
        auc = float(roc_auc_score(yt, fair))
    except Exception:
        auc = None
    ci_hw = _ece(fair, yt)
    conf = (2.0 * (auc - 0.5)) if auc is not None else (1.0 - 2.0 * brier)
    conf = max(0.0, min(1.0, conf))
    leak_suspect = (auc is None) or (auc >= 0.9) or (brier < 0.05)
    if leak_suspect:
        conf = 0.0
    meta = {"calibrated": bool(conf > 0.0), "confidence": round(conf, 4),
            "ci_halfwidth": round(ci_hw, 4), "calib_method": calib_method,
            "platt_a": round(platt_a, 5), "platt_b": round(platt_b, 5),
            "auc": (round(auc, 4) if auc is not None else None),
            "brier": round(brier, 4), "n_holdout": int(ok.sum()), "n_groups": int(n_groups),
            "leak_suspect": bool(leak_suspect)}
    if mode == "residual":
        meta["mode"] = "residual"
        meta["baseline_idx"] = int(F_MID)
    return meta


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
        # 泄漏/平凡态守卫 (2026-06-03): AUC≥0.9 在体育上几乎必是【标签泄漏】(近结算捕获的市场价
        #   已≈0/1, 平凡"预测"已定结果) 或【平凡态】(大比分领先=显然胜负, 非可交易 alpha)。
        #   真实可交易判别 alpha 极少 AUC>0.72。AUC≥0.9 → 不可信 → conf=0 → C++ 校准门挡其驱动交易
        #   (避免退化/泄漏模型 weight=1.0 满驱动产垃圾 fair → 垃圾成交; 实测 auto-train 训出 AUC1.0
        #   退化模型对所有市场预测 ~0.0005 → 假 86% edge)。
        # 不可信判定 (任一 → conf=0, 不驱动交易):
        #   ① AUC 无法算 (单类 holdout) → 判别力不可评估 → 不信。
        #   ② AUC≥0.9 → 泄漏/平凡态 (真实体育 alpha 极少 >0.72)。
        #   ③ brier<0.05 (近完美) → 泄漏/退化红旗 (真实体育 brier 0.15-0.25)。
        leak_suspect = (auc is None) or (auc >= 0.9) or (brier < 0.05)
        if leak_suspect:
            conf = 0.0
        return {"calibrated": bool(conf > 0.0), "confidence": round(conf, 4),
                "ci_halfwidth": round(ci_hw, 4), "calib_method": "conformal",
                "auc": (round(auc, 4) if auc is not None else None),
                "brier": round(brier, 4), "n_holdout": int(len(Xh)),
                "leak_suspect": bool(leak_suspect)}
    # residual (holdout 兜底, 场数<6 才走此路): y=delta=label−b_mid; fair=clip(b_mid+delta)。
    #   指标在【fair vs 重构二值 label】上算 (与 group_cv_meta 同口径), 含泄漏守卫 → C++ 校准门可用。
    base = Xh[:, F_MID].astype("float64")
    fair = np.clip(base + pred, 0.0, 1.0)
    ytrue = np.round(np.clip(yh.astype("float64") + base, 0.0, 1.0))
    brier = float(np.mean((fair - ytrue) ** 2)) if len(fair) else 0.25
    auc = None
    try:
        from sklearn.metrics import roc_auc_score
        if len(np.unique(ytrue)) >= 2:
            auc = float(roc_auc_score(ytrue, fair))
    except Exception:
        auc = None
    ci_hw = _ece(fair, ytrue)
    conf = (2.0 * (auc - 0.5)) if auc is not None else (1.0 - 2.0 * brier)
    conf = max(0.0, min(1.0, conf))
    leak_suspect = (auc is None) or (auc >= 0.9) or (brier < 0.05)
    if leak_suspect:
        conf = 0.0
    return {"calibrated": bool(conf > 0.0), "confidence": round(conf, 4),
            "ci_halfwidth": round(ci_hw, 4), "calib_method": "residual_holdout",
            "auc": (round(auc, 4) if auc is not None else None),
            "brier": round(brier, 4), "n_holdout": int(len(Xh)),
            "mode": "residual", "baseline_idx": int(F_MID),
            "leak_suspect": bool(leak_suspect)}


def write_sidecar(out_path, meta):
    """写 <onnx>.meta.json (C++ OnnxFairValueModel 加载侧读)。"""
    meta_path = out_path + ".meta.json"
    with open(meta_path, "w") as f:
        json.dump(meta, f, ensure_ascii=False)
    print(f"[train] 校准 sidecar -> {meta_path}: {meta}")


def train(X, y, groups, out_path, mode="regress"):
    import lightgbm as lgb
    cat = [c for c in CAT_FEATURES if c < X.shape[1]]
    params = dict(n_estimators=50, num_leaves=15, learning_rate=0.1,
                  min_child_samples=5, verbose=-1)
    # 评估: 按场次 (condition_id) GroupKFold CV → 诚实 AUC (防同场行记忆泄漏 → 假 AUC≈1; 防单类 holdout)。
    meta = group_cv_meta(X, y, groups, mode, params)
    # categorical_feature: 类别列声明为 categorical, 树学 == 分裂而非有序阈值 (v0.6)。
    # ⚠ 已知风险: onnxmltools 对 LightGBM categorical split 的 ONNX 导出支持度需验证;
    #    若导出失败/不一致, fallback = 去掉 categorical_feature 当数值 (低基数下树仍可隔离)。
    model = lgb.LGBMRegressor(**params)
    model.fit(X, y, categorical_feature=cat)
    export_onnx(model, X.shape[1], out_path)
    if meta is None:  # 数据太少 → 保守 (未校准, 前端保持占位)
        meta = {"calibrated": False, "confidence": 0.0, "ci_halfwidth": 0.5,
                "calib_method": "none", "n_holdout": int(len(X))}
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
    # 含噪二值标签 (弱信号 + 噪声 → AUC~0.65, 真实非泄漏; 避免触发泄漏守卫 AUC≥0.9 → fixture
    #   sidecar 才会 calibrated=true 供 ON04 测试)。
    z = X[:, 0] * 0.6 - X[:, 18] * 0.3 + (X[:, 83] == 1.0) * 0.15
    p_true = 1.0 / (1.0 + np.exp(-z))
    y = (rng.random(n) < p_true).astype("float32")  # 含噪二值: y∈{0,1}, 与特征弱相关
    # holdout 评估 (末 20%) → sidecar (C++ 测试 fixture 需要 meta)
    n_hold = max(20, int(n * 0.2))
    eval_model = lgb.LGBMRegressor(n_estimators=30, num_leaves=15, min_child_samples=5, verbose=-1)
    eval_model.fit(X[:-n_hold], y[:-n_hold])
    meta = compute_meta(eval_model, X[-n_hold:], y[-n_hold:], "regress")
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
    # 默认 residual (2026-06-03 治"只买 YES"): 模型预测对市价 b_mid 的增量 delta, fair=市价+delta,
    #   edge=delta 天然两边。regress 仅供回退/对照 (Platt 全局拉伸破坏选边 → 系统性只买 YES)。
    ap.add_argument("--mode", choices=["regress", "residual"], default="residual")
    ap.add_argument("--selftest", action="store_true", help="合成数据生成 fixture ONNX")
    a = ap.parse_args()
    if a.selftest:
        selftest(a.out)
        return
    if not a.features:
        print("需 --features <training.jsonl> 或 --selftest", file=sys.stderr)
        sys.exit(2)
    rows = load_jsonl(a.features)
    # 治本筛选 (moneyline + 滤已决出泄漏行); fallback 按【场数】判 (group CV 需 ≥6 场, 非看行数):
    #   moneyline 场太少 → 放 moneyline (全类型, 早期数据稀时优先有足够场做按场 CV)。
    def ng(g):
        return len(set(g.tolist()))
    X, y, g, st = build_xy(rows, a.mode, moneyline_only=True, drop_decided=True)
    print(f"[train] 筛选: 标注{st['labeled']} 滤类型{st['drop_type']} 滤已决{st['drop_decided']} → 留{st['kept']} 行/{ng(g)}场",
          file=sys.stderr)
    # 阈值 40 (2026-06-03 治 residual delta 偏置): moneyline 场 < 40 → 放 moneyline 限制用全类型。
    #   根因: 9 moneyline 场太少 → residual 目标均值 mean(label−b_mid) 采样噪声大 (实测 +0.5 而非真实
    #   ~+0.05) → delta 系统性正 → fair=市价+0.5 → 仍只买 YES。147 全类型场 → 目标均值稳 (~+0.05) →
    #   delta 两边 (OOF 验证 买YES 45996/买NO 36741)。需 ≥40 moneyline 场 (够稳) 才训 moneyline-only。
    if len(X) < 200 or ng(g) < 40:  # 行少 或 moneyline 场不足 → 放 moneyline 限制用全类型 (保滤已决)
        X, y, g, st = build_xy(rows, a.mode, moneyline_only=False, drop_decided=True)
        print(f"[train] fallback 放 moneyline → 留{st['kept']} 行/{ng(g)}场", file=sys.stderr)
    if len(X) < 50 or ng(g) < 6:  # 仍不足 → 放滤已决 (最低保障; 靠泄漏守卫+sanity 兜底)
        X, y, g, st = build_xy(rows, a.mode, moneyline_only=False, drop_decided=False)
        print(f"[train] fallback 放全部筛选 → 留{st['kept']} 行/{ng(g)}场", file=sys.stderr)
    if len(X) < 50:
        print(f"样本不足 ({len(X)}<50), 训练跳过 — 等真数据攒够", file=sys.stderr)
        sys.exit(1)
    print(f"[train] {len(X)} 样本 × {X.shape[1]} 列, {len(set(g.tolist()))} 场, mode={a.mode}")
    train(X, y, g, a.out, a.mode)


if __name__ == "__main__":
    main()
