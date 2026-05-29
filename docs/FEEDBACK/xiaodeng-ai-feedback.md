# 小邓 — Live 系统 AI/模型可观测视角反馈

- owner: 小邓 (ml-advisor, #31)
- last_review: 2026-05-30
- 范围: 实地使用 live 后端 `http://127.0.0.1:8080` + 前端截图/代码, 从 AI 模型可观测 + 可解释性视角评估
- 边界声明: 本反馈是 ML 顾问视角的"可观测/可解释"评估, 不是 v1 引 ML 提案 (v1 不引 ML 仍成立)

---

## 0. TL;DR (给 GM 一句话)

**AI provenance 管线端到端打通了 (字段真值非占位, XD-1~5 前端全落实), 但当前 fair_value 是 rule-based sigmoid baseline 而非 ML 模型, 且 `model_confidence=0` / `CI=±0.05 写死` / `drift·推理延迟·校准状态零可观测` — 链路是真的, 模型是 stub, 别把 stub 的数字当 ML 信号用。**

---

## 1. 实测证据 (live 抓取)

`GET /api/v1/quote/<cid>` (3 个真实 cid, NHL/NBA outright) 实测返回:

```json
{"fair_value":0.5120739806,"market_mid":0.5603699028,"edge_bps":0,
 "kelly_fraction":0,"suggested_notional":0,"signal_strength":0,"model_conf":0,
 "model_id":"paper-fv-baseline","model_kind":"stub","spec_version":"m1-paper-v0.1",
 "model_confidence":0,"model_calibrated":false,
 "fair_ci_lower":0.4620739806,"fair_ci_upper":0.5620739806,
 "predict_ok":true,"model_as_of_ts":1780088403543449000,"advisory":true}
```

判定:
- AI provenance 字段是**真值, 非空/非占位**: `model_id/model_kind/spec_version/predict_ok/advisory/model_as_of_ts` 都从 `QuoteSnapshotHub` 真实快照透传 (`real_state_provider.hpp::to_quote_params`), 没有 demo 假数据回落 (provider 无数据时 `found=false`, 已验证)。
- 但**语义诚实地标了 stub**: `model_kind="stub"`, `model_id="paper-fv-baseline"`, `model_calibrated=false`, `advisory=true`。这点做得对 — 没有冒充 ML。
- `/metrics` 端 **零 AI 指标**: grep `model|fair|infer|calib|confidence|drift` 只命中一个 `stcpp_price_drift_bps`(且=0, 是价格漂移不是模型 drift)。模型侧完全无可观测。

---

## 2. fair_value 到底是谁算的 (核心结论)

**是 rule-based baseline, 不是 ML 模型。** 链路:

- `paper_loop.cpp` Step2 调 `fv_estimator_.estimate()` → `BaselineFairValueModel`(`pricing/fair_value_estimator.cpp`)。
- 算法 = `sigmoid(alpha*score_diff + beta*time_fraction)` 先验 + 0.20 固定权重 microprice Bayesian 混合 + Kahan normalize。**纯解析式, 无学习参数, 无训练数据。**
- ONNX 路径 (`ml/fair_value_model.cpp::make_onnx_fair_value_model`) **W11 前恒返回 nullptr**, 调用方回落 stub。ONNXRuntime 未链入。
- `PublishQuoteSnapshot` 里 provenance 全是**人工硬编码常量**: `model_confidence=0.0`(注释 "M1 stub: 无置信度"), `fair_ci = p_yes ± 0.05`(注释 "±5% 近似 M1"), `model_calibrated=false`。

对 AI 可解释性的影响: 前端"置信 0%"、"CI [x−0.05, x+0.05]" 看着像模型输出, **实际是占位常量, 没有任何统计意义**。诚实但容易误读 — 见 P1-2。

---

## 3. 前端 XD-1~5 落实情况 (逐条核验)

源: `frontend/src/components/EventGrid.tsx` (主) + `TradingPage.tsx` (详情)。**5 条红线全部实现:**

| 红线 | 要求 | 落实 | 证据 |
|---|---|---|---|
| XD-1 | fair + confidence + model_id 三位一体 | ✅ | EventGrid Row1+Row2: 公允值 + 置信% + model_id(hover 显 kind/spec) |
| XD-2 | (CI 区间) | ✅ | `hasCi()` → `[lower–upper]` 小字 + 95% CI hover |
| XD-3 | advisory → 角标 | ✅ | `advisory()` → "仅供参考·不下单" banner |
| XD-4 | 未校准 → 灰显降级 | ✅ | `uncalibCls()` + "未校准" chip |
| XD-5 | predict_ok=false → 不画 edge/kelly/notional | ✅ | `<Show when=predictOk fallback="预测异常">` 门控 |

confidence 还有分级配色 (≥0.7 green / ≥0.5 yellow / <0.5 dim)。**前端可解释性架子做得很扎实。**

⚠️ 截图 `docs/dashboard-final-live-honest.png` 里 10 个 event 行**全部折叠**, 看不到展开后的 provenance 区。截图无法证伪前端渲染, 我是靠读代码 + live JSON 核验的。建议补一张**展开态**截图(尤其 advisory banner + 未校准 chip + 0% 置信配色)进 honest 截图集, 否则 "honest" 截图反而隐藏了最关键的 AI 诚实信号。

---

## 4. AI 可观测缺口清单 (按优先级)

### P0 (上 ML 前必须有, 当前全缺)
- **P0-1 推理延迟可观测缺失**: `/metrics` 无 `model_inference_latency_us`(p50/p99)。跨洋高延迟环境 + 决策延迟敏感, ONNX 推理一旦上线必须有延迟直方图, 否则热路径预算无法守。现在 baseline 是解析式没延迟问题, 但 W11 ONNX 接入前这个指标必须先就位。
- **P0-2 drift 检测零落地**: 无 feature drift / prediction drift / label drift 任何信号。`stcpp_price_drift_bps` 是市场价漂移, 不是模型 drift。ML 必经 walk-forward backtest 是我的边界铁律, 但**线上 drift monitoring 是 backtest 之外的独立要求** — 模型上线后 feature 分布漂移无人值守 = 静默失效。

### P1 (诚实性 / 误读风险)
- **P1-1 model_confidence=0 语义二义**: stub 恒返 0。前端配色逻辑 `<0.5 → conf-low/dim`, 于是 stub 永远显示"低置信红/灰"。这碰巧不误导(确实不可信), 但**真模型上线后若某次返回真 0, 与 stub 的 0 无法区分**。建议 stub 期 confidence 显式渲染为 "N/A"(而非 0%), 把"无置信度"和"低置信度"区分开。
- **P1-2 CI 是写死 ±0.05 不是模型产出**: 前端 hover 标 "95% CI", 但后端是 `p_yes ± 0.05` 常量, 无任何分布假设。建议 stub 期前端 CI 旁加 "(近似)" 标, 或后端 spec_version 已含 `m1-paper` 时前端识别并降级文案。否则 "95% CI" 这个措辞对 stub 是**虚假精度 (false precision)**。
- **P1-3 model_as_of_ts = ingestion_ts 复用**: provenance 里 `model_as_of_ts_ns = feat.ingestion_ts_ns`, 即模型"快照时点"借用了数据摄入时点。stub 无独立模型版本时点尚可, 但 ONNX 上线后 model_as_of 应是**模型加载/blake3 hash 时点**, 不是数据时点 — 否则无法回答"这条预测用的是哪个模型 artifact"。

### P2 (ML 上线前补齐即可)
- **P2-1 model_id 不含 artifact hash**: 现在是人类可读字符串 `paper-fv-baseline`。ADR-037 §ML-R8 要求 onnx model_id = 文件 blake3。stub 期无所谓, 但前端 model_id 列宽/hover 要预留 hash 展示。
- **P2-2 校准状态无可观测**: 仅 `model_calibrated` bool。真上线需要 reliability diagram / Brier / ECE 的离线产出指针(至少 spec_version 能链到校准报告)。
- **P2-3 特征时点 (feature freshness) 不暴露**: quote 有 4-ts 契约(好), 但**喂给模型的 feature snapshot 的时点**没单独暴露。drift 排查时需要知道"预测用的特征有多旧"。`feature_snapshot.hpp` 已有结构, 建议 provenance 加 `feature_as_of_ts`。

---

## 5. 建议 (给量化部/系统部, 非我执行)

1. **W11 ONNX 接入前置条件清单 (我来出 spec, 系统部接)**: 推理延迟直方图 + model_id=blake3 + feature_as_of_ts + drift counter 骨架, 这四项必须先于第一个 ONNX 模型上线。
2. **stub 期文案诚实化 (前端, 小余/产品)**: confidence "0%"→"N/A", CI 加"(近似)"标。低成本, 直接消除 false precision 误读。
3. **drift monitoring 立项 (我 v2 候选)**: 与 NRFI/xRunsScored/价格 NN 同批, 作为 ML 上线的伴生基础设施, 不是事后补。
4. **honest 截图补展开态** (产品): 当前折叠截图掩盖了最关键的 AI 诚实角标。

---

## 6. 边界

- 本反馈仅"可观测/可解释"评估。v1 不引 ML 立场不变, fair_value 用 rule-based baseline 是对的。
- 任何 ML 模型上线**必经 walk-forward backtest** (我的红线), 本反馈的 P0/P1 是 backtest 之外的**线上可观测**要求。
- ETL / 数据管线本身不在我职责, 归小余数据部。
