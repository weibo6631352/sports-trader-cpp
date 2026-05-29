# 看板 AI / 模型参数接入 spec v1

owner: 小邓 (ml-advisor, #31)
last_review: 2026-05-29
status: Draft — 待量化部 (小梁) + 观测 API owner (小卢) + 前端 review

关联:
- `include/stcpp/ml/fair_value_model.hpp` (ModelPrediction / FairValueModel / OnnxModelConfig)
- `include/stcpp/ml/feature_snapshot.hpp` (FeatureSnapshot — PIT 锚 + signal_confidence)
- `include/stcpp/ml/model_feature_spec.hpp` (kSpecVersion / FeatureVector)
- `src/stcpp/debug_api/state_provider.hpp` (QuoteParams / StateProvider 契约)
- `src/stcpp/debug_api/demo_state_provider.hpp` (现假值来源)
- `src/stcpp/debug_api/endpoint_quote.cpp` (GET /api/v1/quote 序列化)
- `frontend/src/types.ts` (Quote interface)
- ADR-037 (AI 量化模型架构) / ADR-014 (ML shadow timing) / ADR-038 (观测 API) / ADR-040 (市场结构)

---

## 0. TL;DR (派单回报口径)

现状: `/api/v1/quote` 已暴露 `fair_value / market_mid / edge_bps / kelly_fraction /
signal_strength / model_conf`,但全部是 `DemoStateProvider` 写死的假值,**且 AI 参数缺"出处"** —
没有 model_id、没有模型 PIT as_of_ts(`quote_as_of_ts` 是快照读取时刻,不是 feature 锚)、
没有 calibration/区间、没有 advisory 标记、没有推理健康。

本 spec 干三件事:
1. **定义该上看板的 AI 参数清单 + 来源 + 优先级**(§2)。
2. **给 `ModelPrediction` 补 calibration 字段**,给 `QuoteParams` 补 ML provenance 字段(§3)。
3. **锁可解释性红线**:fair_value + confidence + model_id 必须并排出现,advisory 角标必带(§4)。

**ML-R2 铁律贯穿全文:paper 期模型旁路,看板 AI 区一律标 `advisory`,不下单、不进 RM 决策。**

---

## 1. 现状盘点 (谁产什么,现在是真是假)

| 字段 | 当前来源 | 真/假 | 语义归属 |
|---|---|---|---|
| `fair_value` | DemoStateProvider 写死 | 假 | **ML 模型** (de-vig fair prob,未来 = ModelPrediction.prob) |
| `market_mid` | DemoStateProvider 写死 | 假 | 市场 (book microprice) — 非 ML |
| `edge_bps` | DemoStateProvider 写死 | 假 | 量化 (fair − mid) |
| `kelly_fraction` | DemoStateProvider 写死 | 假 | 量化 (Kelly,已 cap) |
| `suggested_notional` | DemoStateProvider 写死 | 假 | 量化 |
| `signal_strength` | DemoStateProvider 写死 | 假 | 量化 α 信号强度 ∈ [0,1] |
| `model_conf` | DemoStateProvider 写死 | 假 | **ML 模型** 置信度 ∈ [0,1] |
| `quote_as_of_ts` | `q.as_of_ts_ns` | 半真 | **快照读取时刻** — 非模型 PIT 锚 (缺陷) |

**核心缺陷(可解释性红线触发点):**
- `fair_value` 与 `model_conf` 是 ML 输出,但**无 model_id**(违 ML-R8 看板侧落地)。
- 无模型 PIT `as_of_ts`(feature 锚)。看板拿不到"这个 fair 是基于哪一刻的特征算的"。
- `model_conf` 语义模糊:是 calibration 后验?还是占位?`ModelPrediction` 当前**没有 calibration 字段**。
- 无 advisory 标记 — 前端无法区分"这是 ML 旁路建议"还是"这是已生效决策"。**ML-R2 风险**。
- 无推理健康(predict ok 率 / p99 / ONNX 版本)— 看板无法判断 fair 是否可信。

---

## 2. 该上看板的 AI 参数清单 + 来源 + 优先级

分三梯队。P0 = MVP 看板必须;P1 = online learning / 多模型上线后;P2 = drift 体系成熟后。

### 2.1 P0 — 模型核心输出 (MVP 必上,接 ModelPrediction)

| 看板参数 | 来源 | 类型 | 说明 |
|---|---|---|---|
| `model_fair_value` | `ModelPrediction.probs[i]` | double [0,1] | 模型 per-outcome fair prob。**当前 `fair_value` 即此位** |
| `market_mid` | book microprice (BinaryMarketBookView) | double [0,1] | 市场参照系。**ML 不产,但必须与 fair 并排**(红线 §4) |
| `model_confidence` | `ModelPrediction` 新增 `confidence` (§3.1) | double [0,1] | 校准后置信度。**取代语义模糊的 `model_conf`** |
| `fair_ci_lower` / `fair_ci_upper` | `ModelPrediction` 新增 (§3.1) | double [0,1] | fair prob 预测区间 (见 §3.1 区间来源)。看板画误差带 |
| `model_id` | `ModelPrediction.model_id` | string | 模型标识 (stub / onnx blake3 hash)。**可追溯铁律,必带** |
| `model_as_of_ts` | `ModelPrediction.as_of_ts_ns` | int64 epoch_ns | feature PIT 锚。**非快照读取时刻** |
| `spec_version` | `FeatureVector.spec_version` (= kSpecVersion) | string | 特征契约版本 ("ml-feature-spec-v0.1") |
| `model_kind` | `FairValueModel.kind()` → to_string | string | "Stub"/"Onnx"/"Treelite"。看板显式标 Stub = 占位非真模型 |
| `predict_ok` | `ModelPrediction.ok` | bool | 推理是否成功;false → 看板灰显 fair,不画 edge |
| `advisory` | 固定 true (paper 期) | bool | **ML-R2:paper 期恒 true,看板必显 advisory 角标** |

### 2.2 P0 — 量化派生 (非 ML 产,但同框展示,标清归属)

| 看板参数 | 来源 | 归属 | 说明 |
|---|---|---|---|
| `edge_bps` | 量化部 (fair − mid 折 bps,net) | 量化 | **依赖 model_fair_value;必须能溯源到 model_id** |
| `kelly_fraction` | 量化部 (Kelly,已 cap) | 量化 | 同上 |
| `suggested_notional` | 量化部 | 量化 | paper 期看板标 advisory,不触发下单 |
| `signal_strength` | 量化部 α ∈ [0,1] | 量化 | 与 model_confidence **不是一回事**,看板分列勿混 |

> 边界提示:`edge / kelly / signal_strength / suggested_notional` 的**真值来源是量化部 (小梁)**,
> 不是 ML。小邓只负责 `model_fair_value / model_confidence / ci / model_id / model_as_of_ts /
> spec_version / model_kind / predict_ok` 这一组。看板把两组分区展示但**同一卡片并排**(§4 红线)。

### 2.3 P1 — online learning / 多模型 (drift + 版本)

| 看板参数 | 来源 | 说明 |
|---|---|---|
| `model_version` | 模型注册表 (W11+) | 区别于 model_id 的语义版本号 (v0.1 / v0.2) |
| `drift_score` | drift detector (PSI / KL on feature dist) | online learning 上线后;> 阈值看板告警 |
| `drift_status` | enum {ok, warn, breach} | 看板色带:绿/黄/红 |
| `last_retrain_ts` | 训练管道 | 上次 retrain epoch_ns |
| `feature_staleness_ms` | as_of_ts vs now | 特征新鲜度;跨洋链路敏感 (CLAUDE 部署约束) |
| `calibration_curve_ref` | 离线校准报告 link | 可点开看 reliability diagram |

### 2.4 P2 — 推理健康 (MLOps,接 /metrics 而非 /quote)

> 这些是**全局**模型健康,不属单个 quote,应进 `/metrics` (MetricsSnapshot 扩展) 而非 per-condition quote。

| 看板参数 | 来源 | 说明 |
|---|---|---|
| `predict_p99_us` | 推理计时直方图 | ONNX session Run p99 |
| `predict_ok_rate` | 滚动窗口 ok/total | predict 成功率;< 阈值告警 |
| `onnx_runtime_version` | ORT build 版本 | 复盘锚 |
| `model_loaded` | session ready() | 模型是否加载 (false → fallback Stub) |
| `inference_qps` | 滚动计数 | 旁路推理吞吐 |

**MetricsSnapshot 扩展建议(P2,@小卢 + @小郑 观测栈):**
```
// /metrics — ML 推理健康 (P2; 旁路,ML-R2)
double  ml_predict_p99_us{0.0};
double  ml_predict_ok_rate{1.0};
int64_t ml_inference_total{0};
bool    ml_model_loaded{false};
// onnx_runtime_version 走 label,不进 POD (低基数)
```

---

## 3. 数据结构改动 (接真值)

### 3.1 `ModelPrediction` 补 calibration / 区间字段

**结论:需要补。** 当前 `ModelPrediction` 只有 raw `probs[]` + `ok/normalized`,没有置信度、没有区间。
看板要画 confidence 和误差带,且红线要求 fair 必带 confidence,所以**在推理输出契约里加**,
不要让看板/量化部各自瞎算。

建议加到 `include/stcpp/ml/fair_value_model.hpp` 的 `ModelPrediction`(append 不破坏 layout):

```cpp
// ---- 置信度 / 校准 (看板可解释性 + 红线 §4) ----
// confidence: 校准后置信度 ∈ [0,1]。
//   - 树模型 (LightGBM/XGBoost): 走离线 isotonic / Platt 校准后的可靠度,
//     或用 leaf 方差 / quantile 头估计。
//   - NN: 用 MC-dropout / deep ensemble 方差,或 temperature scaling 后 softmax 峰度。
//   - Stub: 固定 0.0 (占位,看板据此显 "Stub 无校准")。
double confidence = 0.0;
bool   calibrated = false;   // confidence 是否经过校准 (Stub=false, 真模型上线后 true)

// per-outcome[0] 预测区间 (与 probs[0] 同尺度, [0,1])。
//   来源: quantile regression 头 / ensemble 分位 / conformal prediction。
//   ci_low <= probs[0] <= ci_high; Stub 给 [probs[0], probs[0]] (零宽 = 无区间)。
double ci_low  = 0.0;
double ci_high = 0.0;

// 校准 / 区间方法标签 (复盘 + 看板 tooltip),如 "isotonic" / "conformal-0.9" / "none"。
std::string_view calib_method = "none";
```

**StubFairValueModel.predict() 配套**:`confidence=0.0; calibrated=false; ci_low=ci_high=probs[0];
calib_method="none"`。明确告诉看板"这是占位,不要把 0.0 confidence 当真模型低置信"。

> 注意 layout:`ModelPrediction` 非 WAL 序列化 POD(不像 FeatureSnapshot 受 concept 约束),
> append 字段安全。但 `string_view calib_method` 必须指向静态字符串生命周期(同现有 `model_id`)。

### 3.2 `QuoteParams` 补 ML provenance 字段

`src/stcpp/debug_api/state_provider.hpp` 的 `QuoteParams` 当前缺模型出处。append 以下字段
(G-FREEZE-W 只增不改名,deprecated `model_conf` 保留做 alias):

```cpp
// ---- ML provenance (小邓 spec v1; 可追溯红线) ----
std::string model_id;            // ModelPrediction.model_id (空 = 无模型/纯量化)
std::string model_kind{"stub"};  // "stub"/"onnx"/"treelite"
std::string spec_version;        // FeatureVector.spec_version
double  model_confidence{0.0};   // 校准后置信度 (取代语义模糊的 model_conf)
bool    model_calibrated{false}; // confidence 是否已校准
double  fair_ci_lower{0.0};      // fair prob 区间下界
double  fair_ci_upper{0.0};      // fair prob 区间上界
bool    predict_ok{false};       // 推理是否成功 (false → 看板灰显 fair)
std::int64_t model_as_of_ts_ns{0};  // feature PIT 锚 (≠ 快照读取时刻!)
bool    advisory{true};          // ML-R2: paper 期恒 true (看板必显角标)

// DEPRECATED: 用 model_confidence。值 = model_confidence (前端切换后 P2 移除)。
double  model_conf{0.0};
```

> 字段名 `model_conf` → `model_confidence` 的迁移:遵循 ADR-040 deprecated alias 模式
> (`market_id` 同款),前端先切 `model_confidence`,旧 `model_conf` 值跟随,P2 删。

### 3.3 endpoint_quote.cpp 序列化补字段

`src/stcpp/debug_api/endpoint_quote.cpp` 在 `if (q.found)` 块内 append(只增):
```
"model_id" / "model_kind" / "spec_version" / "model_confidence" / "model_calibrated"
/ "fair_ci_lower" / "fair_ci_upper" / "predict_ok" / "model_as_of_ts" / "advisory"
```
保留现有字段不动 (G-FREEZE-W)。前端 `Quote` interface (types.ts) 同步加可选字段。

### 3.4 真值接线路径 (DemoStateProvider → 真 provider)

```
FeatureSnapshot (旁路 hook 抓拍, ML-R2)
   └─> model_feature_spec.hpp: FeatureSnapshot → FeatureVector (列序锁 kSpecVersion)
         └─> FairValueModel.predict(fv) → ModelPrediction {probs, confidence, ci, model_id, as_of_ts}
               └─> 量化部 (小梁): fair=probs[i], 算 edge/kelly/signal_strength/notional
                     └─> 真 QuoteStateProvider.quote_params() 组装 QuoteParams (含 §3.2 provenance)
                           └─> /api/v1/quote 序列化 → 看板
```
- DemoStateProvider 现假值**保留做前端联调占位**,但 `model_id="demo-stub"` / `advisory=true` /
  `predict_ok=true` / `model_kind="stub"` 必须显式填,让看板从 demo 阶段就练习渲染出处与角标。
- 真 provider 由谁注入:观测侧 double-buffer snapshot 模式 (@小卢 ADR-038),
  上游 ML 旁路结果 + 量化派生由量化部 (小梁) owner 组装。小邓只交付 ModelPrediction 契约 + 字段语义。

---

## 4. 可解释性红线 (硬约束,违者看板不许上线)

**红线 XD-1 — fair / confidence / model_id 三位一体并排。**
看板任何展示 `model_fair_value` 的地方,**必须同屏并排**显示:
`model_confidence`(或 calibration 状态)+ `model_id` + `model_as_of_ts`。
**禁止**只给 fair 不给出处,**禁止**只给 `edge_bps` 不给其依赖的 fair + model_id。
理由:edge 是 fair − mid 的派生,fair 来自模型;给 edge 不给模型出处 = 不可追溯,
违 CLAUDE §8 "可追溯" + ML-R8。

**红线 XD-2 — market_mid 必与 fair 并排。**
fair_value 单独出现无意义(用户无法判断"贵了还是便宜了")。
`model_fair_value` 与 `market_mid` 必须同卡片并排,edge 显式标 "= fair − mid"。

**红线 XD-3 — advisory 角标必带 (ML-R2)。**
paper 期 AI 区任何参数旁必带 `advisory` 角标(视觉:醒目 badge,如琥珀色 "ADVISORY · 不下单")。
看板不得让 AI fair / suggested_notional 看起来像"已生效决策"。
`mode != live` 时所有 ML 派生量强制 advisory。

**红线 XD-4 — Stub / 未校准显式降级展示。**
`model_kind == "stub"` 或 `model_calibrated == false` 时,看板对 fair / confidence
做**视觉降级**(灰显 + tooltip "占位模型 / 未校准,仅结构演示"),不得与真模型同等视觉权重。

**红线 XD-5 — predict_ok=false 不画 edge。**
推理失败时 fair 不可信,看板灰显 fair、**隐藏 edge / kelly / suggested_notional**
(派生量基于不可信 fair = 误导),显式标 "推理失败"。

---

## 5. 看板呈现建议 (AI 量化区布局)

单 condition 卡片 "AI 量化区" 推荐布局(满足 §4 红线):

```
┌─ AI 量化区  [ADVISORY · 不下单]  ← XD-3 角标 ────────────────┐
│  Fair (模型)   0.662  [▏0.631 ─ 0.688▕]   ← fair + CI 误差带  │
│  Market mid    0.648                       ← XD-2 并排         │
│  Edge          +21.6 bps  (= fair − mid)   ← XD-1 标公式       │
│  Confidence    0.62  ●已校准 (isotonic)    ← XD-1 三位一体     │
│  Kelly         4.2%   Suggested  $850 [advisory]              │
│  Signal α      0.71                                           │
│  ─────────────────────────────────────────────              │
│  model: onnx-fv-v0.2 (a3f9..) · spec ml-feature-spec-v0.1     │ ← XD-1 出处
│  feature as_of: 2026-05-29 14:32:07.412 (staleness 38ms)     │
└──────────────────────────────────────────────────────────────┘
```
- Stub 模式:整区灰显 + "占位模型" 水印 (XD-4)。
- predict_ok=false:仅显 "推理失败 · fair 不可信",edge/kelly/notional 区域隐藏 (XD-5)。
- P1 drift 上线后:卡片右上加 drift 色点 (绿/黄/红)。

---

## 6. 接入点汇总 (文件路径)

| 改动 | 文件 | owner | 优先级 |
|---|---|---|---|
| ModelPrediction 加 confidence/ci/calib | `include/stcpp/ml/fair_value_model.hpp` | 小邓 | P0 |
| StubFairValueModel.predict 配套填新字段 | `include/stcpp/ml/fair_value_model.hpp` | 小邓 | P0 |
| QuoteParams 加 ML provenance 字段 | `src/stcpp/debug_api/state_provider.hpp` | 小卢 (契约 owner) | P0 |
| DemoStateProvider 填 model_id/advisory/kind | `src/stcpp/debug_api/demo_state_provider.hpp` | 小卢 | P0 |
| endpoint_quote 序列化新字段 | `src/stcpp/debug_api/endpoint_quote.cpp` | 小卢 | P0 |
| 前端 Quote interface + AI 区渲染 + 角标 | `frontend/src/types.ts` + 卡片组件 | 前端 | P0 |
| 真 QuoteStateProvider (fair←predict, edge/kelly 量化) | (新, 量化侧注入) | 小梁 | P0 |
| MetricsSnapshot 加 ML 推理健康 | `src/stcpp/debug_api/state_provider.hpp` | 小卢 + 小郑 | P2 |
| drift / model_version 字段 | (online learning 上线后) | 小邓 | P1 |

---

## 7. 协作边界 / 不耻下问

- **小邓 (本 spec owner) 只交付**:ModelPrediction 契约 (含 calibration/ci 字段语义) +
  AI 参数清单 + 红线。**不写**量化派生 (edge/kelly/signal_strength) 真值逻辑。
- **edge / kelly / signal_strength / suggested_notional 真值** → @小梁 (量化部,Kelly/Sharpe 主权)。
- **QuoteParams 契约改动 + 序列化 + DemoProvider** → @小卢 (ADR-038 观测 API owner)。
- **真 provider 注入 + double-buffer snapshot** → @小卢 + @小梁 (量化侧组装)。
- **MetricsSnapshot ML 健康扩展** → @小郑 (观测栈 prometheus/grafana)。
- **calibration 方法选型 (isotonic vs conformal vs ensemble)** → 小邓后续出独立 spec
  (W11+ 真模型上线前,必经 walk-forward backtest 验证校准质量)。
- **前端 AI 区组件 + advisory 角标视觉** → 前端 + @小尤 (UX)。

## 8. ML 红线 compliance 自检

- [x] ML-R1 ML 不进 RM 决策:本 spec 所有 AI 参数旁路展示,不接 OrderIntent。
- [x] ML-R2 paper 期旁路:`advisory` 字段恒 true,XD-3 角标强制。
- [x] ML-R5 推理走 ONNX/Treelite:契约不依赖 Python,ModelPrediction 是 C++ POD-ish。
- [x] ML-R8 必带 model_id + as_of_ts:QuoteParams.model_id + model_as_of_ts_ns 强制 (XD-1)。
- [x] walk-forward:calibration / confidence 质量上线前必经 walk-forward backtest (§7)。
