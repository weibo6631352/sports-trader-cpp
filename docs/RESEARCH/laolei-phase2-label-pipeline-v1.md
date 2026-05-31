# Phase 2 标签管道 (condition → outcome → 训练集 X,y)

> owner: 老雷 (GM) ｜ last_review: 2026-05-31
> 联合评审 Phase 2 P0 (小邓: 标签管道先行, 所有建模前提)。代码: `include/stcpp/ml/label_pipeline.hpp`

## 0. 一句话

把 FeatureRecorder 落的特征行 (X) 与结算结果 (y) 按 `condition_id` 对齐成监督训练集。
y = 该 condition 结算 outcome (YES 赢=1 / NO 赢=0), 来自 `SettlementRecord.settlement_value`。

## 1. 端到端流程

```
[在线 paper daemon]
  FeatureRecorder (独立线程读 quote_hub) → features.jsonl   (X: 每行一决策快照, 含 condition_id + as_of_ts)
  SettlementPoller → SettlementStore                          (condition → closed/winner)

[离线 标签 join, C++ stcpp::ml]
  BuildLabelStore(SettlementMap)          → condition_id → {y, valid}   (只收已结算)
  JoinFile(features.jsonl, store, out, drop_unlabeled=true) → training.jsonl
                                          每行 = X 原样 + ,"label":<0/1>,"label_valid":1

[离线 训练, Python §12.4]
  pandas.read_json(training.jsonl, lines=True) → X (特征列), y (label 列)
  残差框架 (仲裁 B): y_residual = label − fair_value;  LightGBM.fit(X[SET-A], y_residual)
  → 导 ONNX → OnnxFairValueModel (fair_value_model.hpp:332)
```

## 2. 红线纪律

- **CLV/前视 (小蒋)**: y 是 **offline 标签**, 绝不作特征 / 不回喂实时决策 / 不进 QuoteFeatures。
  本管道只产独立训练文件, 与 live 路径物理隔离。
- **监督集**: 只含 closed + 明确赢家 (settlement_value∈{0,1}); 未结算行 drop (或 label_valid=0 半监督)。
- **PIT**: 标签是 condition-level (一场一个 y), join 到该 condition 结算前的所有 X 行。X 的特征是采样时刻的真值 (4ts 透传), y 是结算后才知 — 标准监督标注, 无前视 (训练时才 join, 推理时不知 y)。
- **泄漏列 (仲裁 B)**: from-scratch 框架须砍列 16,60-63 (含 baseline fair); 残差框架合法保留。特征选择在 train 侧 (SET-A/SET-B 配置), C++ 不动。

## 3. API (header-only, 纯函数核 + 文件层)

| 函数 | 作用 |
|---|---|
| `DeriveOutcomeLabel(SettlementRecord)` | settlement_value → {y, valid} |
| `BuildLabelStore(SettlementMap)` | condition → label (只收已结算) |
| `ExtractConditionId(jsonl_line)` | 从特征行抽 condition_id |
| `AppendLabel(line, label)` | X 行末尾插 `,"label":y,"label_valid":b` |
| `JoinLine(line, store, drop)` | 单行 join (测试/流式) |
| `JoinFile(in, store, out, drop)` | 流式: features.jsonl → training.jsonl + JoinStats |

测试: `test_label_pipeline.cpp` LP01-06 (derive/build/extract/append/join/file 端到端)。

## 4. X 完整性 (本轮已补 + 残余)

- **已补**: FeatureRecorder.WriteLine 之前只落 33 字段 (缺第一梯队 alpha)。本轮扩到含
  **b_ofi / g_time_x_lead / g_goal_freshness / g_bm_inplay_fair / 全时序微结构(YES+NO) / x_log_odds /
  sports 动态 / 双边持仓 / resolution** —— 即 QuoteFeatures 全信号列 (契约 18-74)。
- **残余 (0-17)**: 原始 game/book 列 (g_score_diff/g_period/g_elapsed/b_mid/b_spread/b_depth + cross 16-17)
  **不在 QuoteFeatures**, 故 FeatureRecorder 落不到。注: market_mid≈b_mid 已落; score_diff 缺。
  完整 75 列 X 需记 `extract_full` 输出 (PublishQuoteSnapshot 已算 fv) —— 但那需 fv-hub + 独立
  recorder 线程 (避免 loop_thread_ IO)。**列入后续**; 当前 X (18-74 + fair/mid/edge/pos) 已含
  评审第一梯队全部 alpha, 足够跑 book-only Tier-1 walk-forward (小蒋 P0)。

## 5. 下一步 (依赖)

1. 标签 join ✅ (本轮)。X 信号列补全 ✅ (本轮)。
2. 待真数据: paper daemon 跑出 features.jsonl + 结算 → 第一份 (X,y)。
3. book-only Tier-1 walk-forward (小蒋 P0): Polymarket 真数据子集验 baseline IC/CLV。
4. 残差 LightGBM → ONNX → OnnxFairValueModel 实现 (替 stub)。
5. (可选) 完整 75 列: fv-hub + TrainingSampleRecorder 线程, 补 0-17 原始列。
