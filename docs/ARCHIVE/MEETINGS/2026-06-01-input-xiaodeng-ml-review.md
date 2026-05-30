# ML 视角 review v1 — W5 末 Wave 26 代码审计输入

- **Owner:** 小邓 (ML advisor, F 顾问团)
- **last_review:** 2026-06-01
- **会议:** W5 末 Wave 26 代码 review 大会 (ML 视角)
- **ADR-009:** 小邓顾问模型 = Sonnet
- **范围:** ML-R1~5 红线 enforce check + 32 feature 覆盖 + TrainingLabel join key + 数据 pipeline 状态 + W6-W9 路线 + HC-06 JD 要点

---

## Part 1: ML-R1~5 红线 enforce check 结果

扫描文件:
- `include/stcpp/ml/feature_snapshot.hpp`
- `include/stcpp/ml/training_label.hpp`
- `include/stcpp/ml/hook.hpp` + `src/stcpp/ml/hook.cpp`
- `src/stcpp/bin/paper.cpp` (paper engine 主入口)
- `include/stcpp/risk/risk_gateway.hpp` + `src/stcpp/risk/risk_gateway.cpp`
- `include/stcpp/strategy/signal_iface.hpp`
- `include/stcpp/signer/signer_iface.hpp`
- `include/stcpp/execution/virtual_matcher.hpp`

### ML-R1: ML 不进生产 — PASS

全仓库未发现任何 ONNX Runtime C++ include、pybind11/pyo3 binding、或 ML 推理调用路径。
`hook.hpp` 注释明确: "on_*() noexcept void — 调用方不可假设 hook 返 false 会 reject"。
ML 层对主决策路径零阻断。

### ML-R2: 仅离线训练, 不进 cpp inference — PASS

仓库内无 onnxruntime、lightgbm C++ API、treelite 头文件引用。
feature_snapshot.hpp 注释 ML-R5: "header 不依赖 pybind11 / Python (POD only)"。
Python 工具链在 `.venv/`，不入 git，不 link 任何生产 TU。

### ML-R3: 不影响 if-else 路径 — PASS

paper.cpp (paper engine stub main) 的信号→RM→signer→matcher 链路中**未出现任何 MLDataHook 调用**。
当前 hook 调用点仅存在于 `tests/unit/test_ml_hook.cpp`。
即 W5 末 hook 处于静默待接状态: 接口就位，但 paper engine 主路径尚未调 hook。

**注意 (W6 行动项):** W6 小蒋接 hook 时，must 以 fanout 方式调用 (fire-and-forget, 不阻塞决策返回)。SPSC ring 满 silent drop 机制已在 hook.cpp emit_feature_ 中实现。

### ML-R4: 不替代风控 — PASS

risk_gateway.hpp 的 21 reject short-circuit 全部是 if-else 规则判断。
MLDataHook::on_risk_decision() 接收 RiskDecision 结果，但只做 WAL append，不反向写入 RM 状态或任何 reject 枚举。
RM 的 RmState (5 态) 和 RejectCode (21 枚举) 均无 ML 输出路径介入。

### ML-R5: 4 ts R-20 全程透传 — PASS (带一个待确认项)

FeatureSnapshot 含完整 4 ts 字段 + `ts_chain_ok()` 自检。
TrainingLabel 含完整 4 ts 字段 + `ts_chain_ok()` 自检。
hook.cpp 四个 on_*() 入口均在 emit 前调 `snap.ts_chain_ok()` 做 drop 过滤。

**待确认项 (派给小蒋):** W6 paper engine 接 hook 时，`as_of_ts` 字段的填充时机须 spec lock。建议:
- signal 阶段: `as_of_ts = SignalContext.as_of_ts_ns`
- decision 阶段: `as_of_ts = RiskDecision.decision_ts_ns`
- fill 阶段: `as_of_ts = VirtualFill.fill_ts_ns`
- settle 阶段: `as_of_ts = SettlementEvent.as_of_ts`

以上须由小蒋在 W6 hook 接入 PR 中明确注释，避免 as_of_ts 用本地 now() 替代上游 ts (R-20 红线)。

---

## Part 2: 32 feature 覆盖度审查

### 当前 feature_snapshot.hpp 32 字段一览

| Index | FeatureName | 数据来源 | 状态 |
|---|---|---|---|
| 0 | PM_mid_bid | Polymarket CLOB | 就位 |
| 1 | PM_mid_ask | Polymarket CLOB | 就位 |
| 2 | PM_book_depth_top3_yes | Polymarket CLOB | 就位 |
| 3 | PM_book_depth_top3_no | Polymarket CLOB | 就位 |
| 4 | Pinnacle_p_yes_fair | **见下方 ADR-008 议题** | 命名待改 |
| 5 | Pinnacle_overround | **见下方 ADR-008 议题** | 命名待改 |
| 6 | edge_bps | P0-01 SignalOutput | 就位 |
| 7 | kelly_full | 小肖 Kelly | 就位 |
| 8 | expected_fill_rate | 小肖 SlippageModel | 就位 |
| 9 | slippage_bps | 小肖 SlippageModel | 就位 |
| 10 | live_section | 老彭 LiveSection | 就位 |
| 11 | game_state | 老彭 GameState | 就位 |
| 12 | kickoff_seconds_until | 老彭 Goalserve | 就位 |
| 13 | inplay_minutes | 老彭 Goalserve | 就位 |
| 14 | score_home | 老彭 Goalserve inplay | 就位 |
| 15 | score_away | 老彭 Goalserve inplay | 就位 |
| 16 | period | 老彭 Goalserve | 就位 |
| 17 | vol_24h | Polymarket data API | 就位 |
| 18 | vol_1h | Polymarket data API | 就位 |
| 19 | vol_5m | Polymarket data API | 就位 |
| 20 | spread_bps | CLOB 计算 | 就位 |
| 21 | quote_half_life_ms | 微结构估计 | 就位 |
| 22 | rm_state | RM RmState | 就位 |
| 23 | rm_consec_loss | RM 状态 | 就位 |
| 24 | rm_bankroll | RM 状态 | 就位 |
| 25 | rm_exposure_pct | RM 状态 | 就位 |
| 26 | signal_confidence | P0-01 edge/0.10 | 就位 |
| 27 | ci_lower | 小肖 CI | 就位 |
| 28 | ci_upper | 小肖 CI | 就位 |
| 29 | N_pretrade | 统计计数 | 就位 |
| 30 | N_inplay | 统计计数 | 就位 |
| 31 | N_settled | 统计计数 | 就位 |

### ADR-008 (GM 错 #9) 影响: Pinnacle_* 字段命名

GM 错 #9 已确认: Pinnacle 单独数据源不存在。Goalserve inplay/getodds 已包含 8-9 家 bookmaker (含 bet365 value_eu 等)。小段 v3 结论: Goalserve 单源足够做 fair value 锚。

**当前状态:** feature_snapshot.hpp index 4/5 仍命名 `Pinnacle_p_yes_fair` / `Pinnacle_overround`。
**to_string() 返回值:** 仍是 `"Pinnacle_p_yes_fair"` / `"Pinnacle_overround"`。

**影响评估:**
- enum 整数值 (4, 5) 不变，ONNX/Parquet column index 不受影响
- 字段语义变为: Goalserve 多 book de-vig 后的 fair prob + overround
- to_string() 返回值会进入 Parquet 列名，**W7 序列化前必须改名**，否则 Parquet schema 固化为错误列名

**建议改名:**
- `Pinnacle_p_yes_fair` → `Goalserve_devig_p_yes_fair`
- `Pinnacle_overround` → `Goalserve_overround`
- `to_string()` 对应两行同步改

**时机:** W6 小梁落 ADR-008 multiplicative de-vig 算法决议后，由小梁会签，我 + 小田联改 feature_snapshot.hpp。**W7 Parquet 序列化前 deadline。**

**派单:** @小梁 W6 EOW 前告知 de-vig 算法选型结果，我收到后立即修改命名。

---

## Part 3: TrainingLabel join key 验证

### feature_snapshot_id 字段类型差异 (重要发现)

扫描结果揭示一个字段类型不一致问题:

| 模块 | 字段类型 | 实际值 |
|---|---|---|
| `FeatureSnapshot.feature_snapshot_id` | `uint64_t` | 老周 hash(market_id \|\| as_of_ts \|\| signal_id) |
| `TrainingLabel.feature_snapshot_id` | `uint64_t` | 同上 (join key) |
| `SignalContext.feature_snapshot_id` | `std::string` | hash 字符串? 还是原始 string? |
| `OrderIntent.feature_snapshot_id` | `std::string` | 同上 |

SignalContext 和 OrderIntent 用 `std::string`，FeatureSnapshot 和 TrainingLabel 用 `uint64_t`。
hook.cpp 的 `on_signal_compute(ctx, snap)` 接收两者，但 join 完全依赖 snap 里的 `uint64_t feature_snapshot_id`，ctx 里的 `std::string feature_snapshot_id` 在当前 hook 代码中 `(void)ctx` 忽略掉了。

**这意味着:**
- FeatureSnapshot → TrainingLabel join: 基于 `uint64_t`，一致
- 但 SignalContext.feature_snapshot_id (string) 与 FeatureSnapshot.feature_snapshot_id (uint64) 是否是同一个值的两种表示，尚无 enforcement

**建议 (@老周 + @小程):** 明确 feature_snapshot_id 在 SignalContext 中的语义，统一为 uint64_t，或者在 hook.on_signal_compute() 中增加 `assert(ctx.feature_snapshot_id == std::to_string(snap.feature_snapshot_id))` 校验。W6 前 spec lock。

### Signer / Matcher 链路: feature_snapshot_id 缺失

| 模块 | feature_snapshot_id | 评估 |
|---|---|---|
| `SignalContext` | 有 (std::string) | 就位 |
| `OrderIntent` | 有 (std::string) | 就位 |
| `SignRequest` | **无** | 缺失 |
| `SignResponse` | **无** | 缺失 |
| `VirtualOrder` | **无** | 缺失 |
| `VirtualFill` | **无** | 缺失 |

SignRequest / SignResponse / VirtualOrder / VirtualFill 均未携带 feature_snapshot_id。

**当前 workaround:** hook.on_fill() 接收 (fill, snap) 两个参数，snap 由 paper engine 在调用点注入，不依赖 VirtualFill 透传。这个设计是有意为之 (hook.hpp 注释: "paper engine 由小蒋统一注入")。

**评估:** 对 ML 数据采集可行，但 audit chain 不完整 — 若将来需要从 VirtualFill WAL 反查 feature_snapshot_id，无法实现。

**建议 (@小蒋 W6):** VirtualFill 增加 `std::uint64_t feature_snapshot_id = 0` 字段，paper engine 在 fill 时注入。这是 R-20 + ML-R5 的完整实现，W6 接 hook 时一并做。

---

## Part 4: ML 数据 pipeline 实际状态

### W5 末快照

- hook.hpp + hook.cpp: 就位，20/20 单测通过 (test_ml_hook.cpp 覆盖 M1~M8)
- FeatureSnapshot POD: 32 feature，WalRecord concept 静态断言通过
- TrainingLabel POD: 4 阶段标签，WalRecord concept 静态断言通过
- paper engine 主路径: **尚未调 hook** (paper.cpp stub loop 无 hook 调用)
- paper_mldata.wal: **尚未产生** (hook 静默状态，无真实数据进来)

### W6-W9 路线确认

| 周次 | 里程碑 | 负责人 | 跨域依赖 |
|---|---|---|---|
| W6 | paper engine 接 hook，产生 paper_mldata.wal | 小蒋 + 小邓 | 老王 WAL framework，老周 feature_snapshot_id 生成 |
| W7 | Parquet 序列化，feature_snapshot.hpp 列名改 Goalserve_* | 小田 + 小邓 | 小梁 ADR-008 de-vig 算法决议 |
| W8 | DuckDB 探索，paper_mldata 首次分析 | 小董 + 小邓 | 小田 W7 Parquet 就绪 |
| W9 | LightGBM baseline (离线，不进生产) | 小邓 | 小董 W8 DuckDB 探索结果 |

**M4.5 前 ML 不进生产 (ML-R1 enforce)。** M4.5 后 ONNX cpp inference 评估需要老钱 + 老韩 + 老雷三方决议，当前不列入计划。

---

## Part 5: ML 视角对其他模块的建议

### 5.1 paper engine 小蒋 — as_of_ts 填充时机 spec lock

见 Part 1 ML-R5 待确认项。W6 hook 接入 PR 中，as_of_ts 在每个生命周期阶段的填充逻辑必须注释，不能用本地 now() 替代上游时间戳。

### 5.2 signal P0-01 小程 — feature_snapshot_id 生成统一

建议: `feature_snapshot_id = hash(market_id || as_of_ts_ns || signal_id_u8)`。
老周 v0.6 PIT 锁已指向相同方向。SignalContext.feature_snapshot_id 应由信号计算完成后立即生成，并同步写入 FeatureSnapshot.feature_snapshot_id (uint64)，确保两者同源。

### 5.3 RM 老韩 — 静态 reject 过滤建议

RmState::HALTED 和 RmState::DRAIN 触发的 reject 是运营状态强制拦截，与市场 edge 无关。
从 ML 训练角度，这类样本会污染 "edge → outcome" 学习信号 (因为 reject 原因不是 edge 差，而是系统状态)。

**建议:** hook.on_risk_decision() 对 state_halted / state_drain 触发的 REJECTED 标记一个 `is_state_reject = true` 字段，或在 FeatureSnapshot 中用 rm_state 字段 (index 22) 区分 (HALTED=2, DRAIN=4 可直接过滤)。
离线训练时，过滤 `rm_state IN (2, 4)` 的 REJECTED 样本，只学习有效的边缘决策。

具体过滤策略 W8 DuckDB 探索后确认，现阶段不改代码，仅留注释。

### 5.4 小董 M4.5 gate — ML 训练数据与 gate evaluator join 关系

GateEvaluator 输入 (GateMetrics) 与 ML 训练数据 (FeatureSnapshot + TrainingLabel) 都源自 paper engine 产生的 WAL。

**建议数据流:** paper_mldata.wal → DuckDB → 同一个 DuckDB 数据库同时提供:
1. `features` 表 → ML 训练 (小邓 W9 LightGBM)
2. `per_trade_pnl` / `equity_curve` → GateMetrics 输入 (小董 M4.5 gate evaluator)

这样 gate 判断和 ML 探索共用同一份数据，避免 join 歧义。W6 起建 DuckDB schema 时统一设计。@小董 W6 start 时对齐。

---

## Part 6: HC-06 ml-data-engineer JD 要点

**审批状态:** 老雷已批准 HC-06 position。

**起草节点:** 6/06 EOW，联署方: 小余 + 小邓 + 小林。
**目标入职:** 8/15，兜底 9/15。

### JD 核心要素

**职位:** ML Data Engineer (HC-06)

**职责:**
- 构建并维护 paper_mldata.wal → Parquet → DuckDB 离线数据 pipeline
- 与小邓协作运行 Python LightGBM baseline 训练循环
- 负责 Parquet schema 版本管理 (与小田 DWH schema 对齐)

**跨单元协作:**
- A 单元: 老王 WAL framework 接口文档阅读; WAL 格式变更时对齐
- D 单元: 小田 Parquet/DWH schema 设计 joint owner
- F 顾问团: 直接向小邓汇报 ML 数据需求

**硬性要求:**
- Python 3 (pandas/polars/duckdb 熟练)
- Parquet 格式 + Arrow 内存模型理解
- DuckDB SQL 查询分析能力
- 能读懂 C++ POD struct 定义 (不写 cpp，但要理解 FeatureSnapshot 字段布局)
- **ML-R1 遵守: 不写 cpp 生产代码，不做 pyo3 binding，不做长跑服务**

**加分:**
- Polymarket / prediction market 行业背景
- Goalserve XML/JSON 数据格式经验
- LightGBM / XGBoost 训练 pipeline 经验
- walk-forward backtest 框架经验

**不要求:**
- C++ 开发能力 (阅读即可)
- 实盘交易系统经验

---

## Part 7: ML-R1~5 W6 起执行计划

| 红线 | W6 动作 | 执行人 |
|---|---|---|
| ML-R1 | hook 接入后仍确认主路径不阻塞，通过 unit test 验证 | 小蒋 + 小邓 |
| ML-R2 | W7 Parquet 序列化纯用 C++ WAL reader (老王 framework) + Python 离线消费，不混 | 小田 + 小邓 |
| ML-R3 | paper engine 主路径 (signal→RM→signer→matcher) hook 调用是 fanout，return 值不阻塞 | 小蒋 |
| ML-R4 | RM reject 枚举不接受任何 ML 输出；hook.on_risk_decision 仍只读 | 老韩 确认 |
| ML-R5 | as_of_ts 填充时机 spec lock，VirtualFill 加 feature_snapshot_id | 小蒋 W6 PR |

---

## 总结: 关键 action items

| 优先级 | 行动 | owner | deadline |
|---|---|---|---|
| P0 | as_of_ts 填充时机 spec lock + VirtualFill 加 feature_snapshot_id | 小蒋 | W6 hook 接入 PR |
| P0 | SignalContext / FeatureSnapshot feature_snapshot_id 类型统一 (string vs uint64) | 老周 + 小程 | W6 EOW |
| P1 | Pinnacle_* → Goalserve_devig_* 字段改名 (等小梁 ADR-008 de-vig 算法) | 小邓 + 小田 | W7 Parquet 序列化前 |
| P1 | HC-06 JD 起草 (小余 + 小邓 + 小林) | 三方联署 | 6/06 EOW |
| P2 | DuckDB schema 统一设计 (ML 训练 + Gate 评估共库) | 小董 + 小邓 | W6 start |
| P2 | rm_state IN (HALTED, DRAIN) 样本过滤策略确认 | 小邓 | W8 DuckDB 探索后 |
