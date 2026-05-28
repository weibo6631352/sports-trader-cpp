# ML 路线图 v2 — 同步建数据基础设施 + shadow signal

- Owner: 小邓 (ml-engineer)
- Date: 2026-05-28
- Supersedes: `xiaodeng-ml-roadmap-data-needs-v1.md` (v1 保留作 audit)
- 验收: 老雷 (GM) + 老钱 (cpo) + 老韩 (risk) + 小梁 (financial)
- 关联: `data-contract-v1.md` / `xiaocheng-signal-catalog-v1.md` / `xiaojiang-paper-trading-engine-v0.2-cpp.md` / `xiaojiang-backtest-framework-v0.2-cpp.md` / `xiaodong-stats-validation-framework-v1.md` / `MEETINGS/sprint1-retro/xiaoyu-speech.md` / `laohan-riskmanager-design-v0.2.md` / `ADR/2026-05-28-gm-signoff-paper-trade.md` / `ADR/2026-05-28-gm-signoff-sprint1-retro.md`
- Status: v2 草稿, Sprint-2 W1 (6/15) 启动后 6 周内 (7/24) 出阶段 0 deliverables. 阶段 0 施工图见 `xiaodeng-ml-data-infra-v1.md`.

> 小邓按: v1 立场 "M5 后才开始建数据". 老雷 2026-05-28 决议: paper trading 跑起来后每天产出对齐数据 (虚拟 PnL + 信号触发 + 比分时序 + orderbook + fill/slippage 实际值) 是金矿, 等 M5 = 浪费 4 个月. 新路线 = Sprint-2 W1 同步建数据基设 + paper 期跑 shadow signal, **但红线不动**: ML 仍 0 行进生产决策路径, paper 期 shadow only, 不计入 M4.5 7 gate, 4 类 case "rule 输 ML 赢" ≥ 20% 才有资格进 production.

---

## 1. 立场 (一句话)

**v1 → v2 唯一变化: "M5 后才开始建数据资产" → "Sprint-2 W1 起同步建 + paper 期跑 shadow signal". ML 进生产红线一字不改.**

修订理由 (用户原话): *"按 if/else 起步先跑 MVP, 大模型也要开始布局了. 因为我们有实时可跑的虚拟盘做数据支撑, 也有比分数据, 场次节点等数据."*

paper trading 是天然对齐数据生成器 — signal_event + paper fill + outcome 都是带 PIT 标签的训练样本. universe 级 + bit-identical feature pipeline + 真实 microstructure, 用钱买不到.

---

## 2. 不变的红线 (Sprint-1 已立, v2 新增 ML-R*)

| # | 红线 | 来源 |
|---|---|---|
| R-1, R-3, R-5, R-6, R-13 | 回测/paper/live feature 同份代码 / PIT / 三套 namespace 隔离 / schema bump / as_of_ts 不用 corrected | data-contract §7 |
| **ML-R1** | **ML 不进 RM 决策路径** (RM 永远 rule) | 老韩 RM v0.2 + Sprint-1 Retro |
| **ML-R2** | **paper 期 ML 不进 OrderIntent 路径** | 老雷 2026-05-28 决议 |
| **ML-R3** | **M4.5 7 gate 不看 ML PnL** (G1-G7 只看 rule signal) | 小董 §5 |
| **ML-R4** | **Python 0 行进 live/paper binary** | paper engine v0.2 PR-8 |
| **ML-R5** | **C++ 推理走 ONNX Runtime / Treelite**, 禁 pybind11 embed | 老姜 latency + 老周架构 |
| **ML-R6** | **ML 进 production 红线**: 4 类对比 case 中 "rule 输 ML 赢" ≥ 20% (§5) | 本文档 |
| **ML-R7** | **ONNX 模型 < 100MB** | 老姜跨洋 + RSS |
| **ML-R8** | **每次推理必带 model_id + feature_snapshot_id**, 缺 → drop | data-contract §4.1 |

ML-R6 是本路线图新增核心红线. 不达标 = ML 永久 shadow, 不解锁 production.

---

## 3. 阶段总览

```
Sprint-2 W1 ─── Sprint-5 末 ─── M4 (paper 启动) ─── M5 ─── M5+
 6/15           ~ 9/26          ~ 11 月            ~ 12 月  战略
  阶段 0        阶段 1           阶段 2            阶段 3
 数据基设       离线 baseline    shadow 实时       决策红线判定
                + ONNX 雏形      与 rule 并行      → 进 prod or 永停
```

| 阶段 | 时间 | 主交付 | ML 进生产? | 验收 |
|---|---|---|---|---|
| 0 | 6/15 → 7/24 (6 周) | 数据 lake + 训练 pipeline + shadow framework spec | **0 行 ML 进 binary** | 老胡 + 小余 + 小蒋 |
| 1 | 7/24 → 9/26 (~9 周) | LightGBM baseline + ONNX C++ 雏形 + 24/25 季回测 | **0 行 ML 进生产** | 小蒋 + 小董 + 老韩 |
| 2 | M4 paper 启动 ~ M5 (~3 月) | shadow signal 与 rule 并行, 4 类 case 报告 | **shadow only**, 单独账本 | 老雷 + 老钱 + 老韩 |
| 3 | M5 后 (战略) | ML 主导 + rule 兜底 (条件 ML-R6 满足) | **条件解锁** | 老雷 + 老钱 + 老韩 + 小梁 |

---

## 4. 阶段 0 ~ 3 详细

### 4.1 阶段 0 (6/15 → 7/24, 6 周, Sprint-2/3) — 不写 ML, 先建训练资产

**8 个 deliverables (D0-1 ~ D0-8) 详见 `xiaodeng-ml-data-infra-v1.md`. 此处摘要**:

| # | 物件 | 截止 | 联签 |
|---|---|---|---|
| D0-1 | 数据 lake schema 定稿 (parquet + PIT) | 6/19 | 小余 |
| D0-2 | 训练 pipeline 工具栈 (.venv) | 6/19 | 老吴 |
| D0-3 | Feature store offline+online 双面设计 | 6/26 | 小田 + 小蒋 |
| D0-4 | Shadow signal C++ 接口 spec | 6/26 | 小蒋 + 老周 |
| D0-5 | mlflow vs 自研选型 (默认 mlflow) | 7/3 | 老吴 + 老周 |
| D0-6 | DuckDB 探索 notebook | 7/10 | 小董 review |
| D0-7 | PIT leakage CI (`ml/tools/check_pit_leakage.py`) | 7/17 | 小董 + 小蒋 |
| D0-8 | 阶段 0 验收报告 | 7/24 | 老胡 协调 |

**阶段 0 NOT 做**: 训练任何模型 / 写 C++ 推理代码 / 接外部新数据源.

**阶段 0 风险**:
- in-process feature store owner 未定 (小余 §8 收口 1) — **高**, 6/12 联签会必须指派, 否则 D0-3 阻塞
- 1s inplay book snapshot 历史不可得 (C-01) — 中, 退化到 30s baseline
- Pinnacle 数据无 (OQ-2) — 高 (非 ML 域), 影响 P0-01 rule baseline 本身

### 4.2 阶段 1 (7/24 → 9/26, ~9 周, Sprint-3/4/5)

**目标**: 离线训 LightGBM baseline, 导 ONNX, C++ 推理跑通 parity. **0 行进生产**.

| # | 物件 | 截止 |
|---|---|---|
| D1-1 | LightGBM baseline (P0-01 ML 候选 + score_model GBM) | 8/14 |
| D1-2 | Walk-forward 5-fold 回测 (复用小蒋 BR-1 feature pipeline) | 8/21 |
| D1-3 | 24/25 season 回测报告 + SHAP (老韩 read) | 8/28 |
| D1-4 | ONNX 导出 + Python↔C++ parity (误差 < 1e-5) | 9/4 |
| D1-5 | `src/ml/onnx_inference.cpp` 雏形 (paper engine v0.2 留位 `libstcpp_ml_inference.a`) | 9/11 |
| D1-6 | ONNX C++ 推理 latency bench (老姜 < 50ms p99) | 9/18 |
| D1-7 | 阶段 1 收尾: 哪些 candidate 进阶段 2 shadow | 9/26 |

**阶段 1 验收门槛** (任一不达 → candidate 弃):
- Brier OOS < 0.18 (vs rule baseline 0.20)
- 5 fold walk-forward 中 4 fold OOS hit rate > rule v1
- OOS / IS Sharpe ratio ≥ 0.6
- DSR > 0.95 AND PBO < 0.25 (小董 §6)
- C++ ONNX 推理 p99 < 50ms, 模型 < 100MB

### 4.3 阶段 2 (M4 paper 启动 ~ M5, ~3 月) — shadow 实时

| # | 物件 | 截止 |
|---|---|---|
| D2-1 | shadow signal 经 paper engine v0.2 接口注入 (`MLSignalEngine` + `ShadowSignalSink`) | M4 同日 |
| D2-2 | Bucket D-shadow-ml 单独账本 (与 A/B/C-shadow-random 并列) | M4 同日 |
| D2-3 | ML vs rule 对比 dashboard (小郑 D1) | M4 + 1 周 |
| D2-4 | 月度 ML 健康度报告 (PSI / KL / calibration ECE) | M4 + 1 月 |
| D2-5 | **4 类 case 对比红线判定报告** (§5) | M5 (3 月 shadow 后) |

**阶段 2 关键约束**:
- shadow signal schema 全同 rule signal (data-contract §4.1), 仅 `bucket=D-shadow-ml` + `model_id` 非空区分
- 不进 RiskGateway, 不算 paper PnL, 不进 M4.5 7 gate (ML-R2 + ML-R3)
- 仍走 BR-1 同份 feature pipeline (对比公平)
- 必填 `model_id` + `feature_snapshot_id` (ML-R8)

### 4.4 阶段 3 (M5 后, 战略级)

**触发**: 阶段 2 报告满足 ML-R6 AND 老雷 + 老钱 + 老韩 + 小梁 四方签字. 任一不满足 → ML 永久 shadow.

| # | 物件 |
|---|---|
| D3-1 | ML 信号进 RiskGateway (rule 兜底永不删, ML-R1 仍生效) |
| D3-2 | online learning (per-match incremental refit) |
| D3-3 | drift detector 生产化 (PSI > 0.3 自动 fallback rule) |
| D3-4 | A/B (champion-challenger) 框架 |

---

## 5. ML-R6 红线 — 4 类 case 判定

**判定窗口**: 阶段 2 shadow 3 个月, 触发次数 ≥ 500 (统计功效).

**4 类 case** (按 ML signal 与 rule signal 在同 `(market_id, as_of_ts ± 30s)` 触发):

| Case | rule 决策 | ML 决策 | 落地结果 | 计入红线? |
|---|---|---|---|---|
| 1 | trigger | trigger 同向 | 一致 | 否 (中性) |
| 2 | trigger | skip / 反向 | 分歧 | **是** |
| 3 | skip | trigger | ML 多看 | **是** |
| 4 | trigger | trigger 同向 size 差 > 2x | size 分歧 | 否 (后续研究) |

**ML-R6 ratio 计算**:
```
N_disagree = count(case_2) + count(case_3)
N_rule_loss_ml_win =
    count(case_2 中 outcome 站 ML 这边)
  + count(case_3 中 ML signal 实际盈利 in shadow PnL)
ML-R6 ratio = N_rule_loss_ml_win / N_disagree
红线: ML-R6 ratio >= 20%
```

**为什么 20%**:
- 50% = random, 必须显著高于 random
- < 20% = 上 prod 风险大于收益
- 20-30% = 有显著 alpha, rule 兜底
- > 30% = ML 主导, rule 退居 fallback
- **不允许 > 50% 替代 rule** (老韩否决, 解释性永不放弃)

**与小董统计标准对接**:
- "outcome 站 ML 这边" 用 CLV (vs Pinnacle closing) 判定, 不用瞬时 mid (太抖)
- bootstrap CI 95% 必须不跨 20% (小董 §5.4 同款)
- 跨 sport / regime (常规赛 vs 季后赛) 分层, 任一层 < 15% 整体不通过

**老韩 / 老钱 永不放弃的红线**:
- 即使 ML-R6 通过, RM 仍永远 rule (ML-R1)
- ML 出 size, RM 仍按 Kelly + cap 重算
- 信号缺 / 模型 stale / drift 红任一 → 切 rule

---

## 6. Shadow Signal 框架架构

```
                          stcpp_trader 主进程 (paper / live)
                                          |
                                          ▼
                            +----------------------------+
                            | L3 STRATEGY (C++, 同一份)  |
                            |  ├─ Rule Signal Engine     |
                            |  └─ ML Signal Engine       |  ← paper 期 only
                            |      (libstcpp_ml_inference.a)
                            +-----+----------------+-----+
                                  |                |
                       rule signal|                |shadow signal (异步)
                                  ▼                ▼
                       +---------------+   +-------------------+
                       | SignalSink    |   | ShadowSignalSink  |
                       | bucket=A/B/   |   | bucket=D-shadow-ml|
                       | C-shadow-     |   | (单独 topic)      |
                       |   random      |   +-------+-----------+
                       +-------+-------+           |
                               |                   ▼
                               ▼              +----------------+
                       +---------------+      | shadow_audit   |
                       | RiskGateway   |      | WAL (R-11 隔离)|
                       | (rule only)   |      +-------+--------+
                       +-------+-------+              |
                               |                      ▼
                               ▼              +----------------+
                       (OrderIntent)         | shadow_pnl     |
                       (live + paper)        |  ledger        |
                                             | (M4.5 不看)    |
                                             +-------+--------+
                                                     |
                                                     ▼ (异步, dashboard)
                                             +----------------+
                                             | ML vs rule     |
                                             | (小郑 D1)      |
                                             +----------------+
```

**关键约束**:
- ML Signal Engine 在 paper / backtest mode 启用, **live mode 阶段 3 才启用** (CMake `-DENABLE_ML_INFERENCE` flag)
- shadow signal 异步出, 不阻塞 rule path
- 共享 `FeatureAssembler` (BR-1), 不重新算 feature
- 单独 `shadow_audit.wal` (R-11 隔离, 与 paper_audit / risk_audit 并列)

**接口 spec** (具体 C++ 类型见 `xiaodeng-ml-data-infra-v1.md` §5):
- `IMLSignalEngine::tick(feature_snapshot) -> optional<MLSignalCandidate>`
- `IShadowSignalSink::emit(MLSignalCandidate)`
- `IModelRegistry::current_model(signal_id) -> shared_ptr<OnnxModel>` (SIGHUP hot reload, atomic ptr swap)

**paper engine v0.2 已留位**: §3.1 `stcpp_ml_inference` 库链入 `stcpp_trader`, 阶段 0 D0-4 我侧出 mock implementation 编译通过即可.

---

## 7. 训练数据 schema (与小余 + 小田对接)

### 7.1 训练数据三大来源 (paper 启动后)

| 来源 | 字段 | 频率 | owner |
|---|---|---|---|
| signal_event (历史) | data-contract §4.1 全字段 | 每信号一条 | 小蒋 paper engine |
| paper_trade (历史, 含 sim fill) | data-contract §4.6 paper_trade | 每 fill 一条 | 小蒋 paper engine |
| feature snapshot (历史) | data-contract §4.3 全 28 features | 每决策点 | 小田 + 小余 落 parquet |

### 7.2 训练 join key + PIT

```
training_row = (feature_snapshot[as_of_ts],
                signal_event[emit_ts = as_of_ts ± 30s],
                label[label_ts > as_of_ts + τ_min])
```

PIT 校验: `feature_snapshot.as_of_ts <= signal_event.emit_ts <= label.label_ts`, 任何违反 → CI fail (ML-R8 + R-3).

### 7.3 落库布局 (见 `xiaodeng-ml-data-infra-v1.md` §1)

```
data_lake/
├── raw/              # 小余直管 (polymarket_book / trade / goalserve / pinnacle)
├── features/         # 小田 → 小余 落 parquet
├── signals/          # 小蒋 paper engine WAL → 小余
│   └── shadow_signal/   # R-11 隔离
├── labels/           # 小余 (UMA settled + Goalserve cross-check)
└── ml/               # 小邓 (training_dataset_snapshot + models + shadow_pnl)
```

### 7.4 不耻下问 — 给小余 / 小田 / 老胡

| # | 问题 | 对象 |
|---|---|---|
| MQ-D1 | feature snapshot 落 parquet 触发: 每信号触发时 (省 99%) vs 每 1s 全量 (backtest replay 完整) | 小余 + 小田 |
| MQ-D2 | `shadow_signal/` partition 是否 R-11 合规 (与 paper / live 同库不同 partition) | 小余 + 老周 |
| MQ-D3 | in-process feature store owner = 小田 #24? 6/12 联签会必须定 | 老胡 + 老雷 |

---

## 8. 模型推理 latency budget (与老姜对接)

| 阶段 | budget | 说明 |
|---|---|---|
| Feature assembly (RCU snapshot) | < 5 ms | 不阻塞 hot path |
| ONNX inference (1 sample, < 15 features, LightGBM 500 trees) | < 10 ms | ONNX Runtime C++ |
| Calibration + threshold | < 1 ms | Platt 闭式 |
| Shadow logging (async) | 0 ms (后台线程) | |
| **总计** | **< 50 ms p99** | hard cap |

**与老姜 latency-budget v1 对齐**:
- 策略 hot path 总预算 500 us (KR-A-3) — ML 推理 50ms **不在这条 hot path 上**
- ML 推理在决策路径 (200-500 ms 跨洋大头), 50ms 合理
- shadow 不阻塞 rule signal — rule 先出, ML 异步出 (后台线程)

**不耻下问 — 给老姜 + 老周**:

| # | 问题 |
|---|---|
| MQ-L1 | ONNX Runtime C++ 在 US colo 实测 baseline latency? 9/18 D1-6 出 bench, 需老姜 review |
| MQ-L2 | shadow signal 是否走 OrderIntent latency budget? 我倾向不走 (不下单) |
| MQ-L3 | ONNX vs Treelite 实测在小模型 (< 500 trees) latency 差异 — D1-6 出数据 |

---

## 9. 修订记录

| 版本 | 日期 | 修订 | 主笔 |
|---|---|---|---|
| v1.0 | 2026-05-28 (上午) | 初版, M5 后才开始建数据 | 小邓 |
| v2.0 | 2026-05-28 (下午) | 老雷决议: Sprint-2 W1 同步建 + paper 期 shadow signal; ML-R1~ML-R8 + ML-R6 红线 | 小邓 |
| v2.1 | 7/24 (预计) | 阶段 0 验收灌入实测 | 小邓 |
| v3.0 | M5 (预计) | 阶段 2 shadow 3 月报告 + ML-R6 判定 | 小邓 |

---

## 10. 完成汇报 (给老雷)

**v2 已完成**:

1. **阶段 0 (6/15 ~ 7/24, 6 周) 8 个 deliverables**: D0-1 数据 lake schema + D0-2 训练栈 + D0-3 feature store + D0-4 shadow signal 接口 + D0-5 mlflow 选型 + D0-6 DuckDB 探索 + D0-7 PIT leakage CI + D0-8 验收. 0 行 ML 进 binary. 施工图见 `xiaodeng-ml-data-infra-v1.md`.

2. **阶段 1 (7/24 → 9/26, ~9 周) 时间表**: LightGBM baseline → walk-forward → ONNX 导出 → C++ 推理雏形 → latency bench. 阶段 1 末 decide 哪些进阶段 2 shadow.

3. **ML vs rule 4 类 case 红线 (ML-R6)**: shadow 3 月, "rule 输 ML 赢" ≥ 20% (bootstrap 95% CI 不跨 20%, 分 sport / regime 任一层 ≥ 15%) 才进 production. 不达 = ML 永久 shadow.

4. **8 条 ML 红线 (ML-R1 ~ ML-R8)**: ML 不进 RM / paper 期不进 OrderIntent / M4.5 7 gate 不看 ML PnL / Python 0 行进 binary / ONNX 推理 / 模型 < 100MB / 推理必带 model_id + feature_snapshot_id / 20% 红线.

5. **完整对接**: 数据 schema (小余/小田) + paper engine 接口 (小蒋) + 统计标准 (小董) + C++ 推理 (老周/老姜) + 风控 (老韩) 全联签.

— 小邓, 2026-05-28
