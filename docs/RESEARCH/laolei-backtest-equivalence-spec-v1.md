# 多输入回测等价性 — 项目 spec v1

> owner: 老雷 (GM) · last_review: 2026-06-01 · 性质: 红线#3 闭合工程计划 (三方架构评审 P0 配套)
> 触发: 2026-06-01 全系统评审 (老周架构 / 老姜性能 / 老郭红线) — 老郭头号发现:
>   **回测=实盘红线 (§8 #3) 当前只覆盖 6 个决策输入里的 1 个。**
> 执行: 小蒋 (回测 owner) 主实现 + GM 写边界代码 (主干) + 老郭红线 review + 老周架构 review。

---

## 1. 问题 (实证, 非空谈)

`ReplayDriver` 只 `hub.Publish()` 到 **OrderBookSnapshotHub (book)** 一个输入。而 `PaperLoop::TickOne` 决策依赖 **6 个独立输入** (各自一个 store):

| # | 输入 | TickOne 读取 | 回放? | 不回放的后果 |
|---|---|---|---|---|
| 1 | CLOB book | `hub_.Read` | ✅ | — |
| 2 | **Goalserve 比分/时钟** | `score_store_->GetSnapshot()` | ❌ | `has_real_fair=false` → stub fair → **全程零成交,永不进 in-play 分支** |
| 3 | **inplay bet365 sharp 赔率** | `es.inplay_bet365_*` (附 #2) | ❌ | sharp-anchor (ResolveFair 第二优先级) 回测永不触发 |
| 4 | **live_stats (g_*_diff)** | `LiveStatsFor` | ❌ | 微观体育特征全 NaN, 训练/回测分布 ≠ 实盘 |
| 5 | **resolution (REST 结算)** | `ResolutionFor` | ❌ | **持仓永不结算 → realized PnL 恒 0** |
| 6 | catalog (fee/cat/parent/line) | `tick_catalog_` | ⚠️ ctor 默认 | totals/spreads 派生定价 invalid → 派生盘口回测全 fail-closed |

**裁定 (老郭):** 红线#3 当前 = "决策函数同一 (真) + 决策输入等价 (假, 只 1/6)"。
**当前任何回测 PnL/Sharpe 是在测一个【不下单不结算的空系统】** → 违反价值观#3 (数字说话) 的前提。已在 `replay_driver.hpp` 头钉护栏 (replay_coverage=BOOK_ONLY)。

---

## 2. 目标 + 验收标准

**目标:** 回测能等价复现实盘决策路径 —— 同一 `TickOne` 在历史数据上**会下单、会结算、走 sharp-anchor**。

**验收 (数字说话):**
1. 回测在含 in-play 比分的历史段上 `orders_attempted > 0` 且 `fills > 0` (当前=0)。
2. 回测 `positions_settled > 0` 且 realized PnL ≠ 0 (当前恒 0)。
3. **等价性断言**: 同一段历史, 回测的 per-tick `p_fair` provenance (`FairSrc`) 分布 ≈ 实盘 (sharp_inplay 占比可比), 不全是 market_devig。
4. `replay_coverage` 升级 BOOK_ONLY → FULL_6INPUT。

---

## 3. 架构: DecisionInputSnapshot 边界 (老郭唯一主动推的抽象)

**根因 (老郭):** 6 输入 6 个独立 store, replay 只接 1 个。**正解 = 抽统一"决策输入快照"边界, 给 replay 单一注入点。**

当前 `TickAll` 入口已冻结 5 个 RCU 快照 (`tick_score_snap_`/`tick_event_map_`/`tick_catalog_`/`tick_resolution_`/`tick_live_stats_`) + `hub_`。把这 6 个来源收敛为:

```cpp
// include/stcpp/paper/decision_input.hpp (新)
struct DecisionInputSnapshot {
    const polymarket::clob_wss::OrderBookSnapshotHub* book_hub;  // #1 (per-token Read)
    std::shared_ptr<const data::ScoreMap>          score;       // #2 + #3 (inplay odds 附比分)
    std::shared_ptr<const data::livescore::LiveStatsMap> live_stats;  // #4
    std::shared_ptr<const ResolutionMap>           resolution;  // #5
    std::shared_ptr<const PaperCatalog>            catalog;     // #6
    std::shared_ptr<const ConditionEventMap>       event_map;   // 映射桥
};

// PaperLoop 注入接口 (live impl 读 store, replay impl 读历史帧)
class IDecisionInputProvider {
  public:
    virtual ~IDecisionInputProvider() = default;
    virtual DecisionInputSnapshot BuildTickInputs() = 0;  // TickAll 入口调一次
};
```

- **Live impl** (`LiveDecisionInputProvider`): 现有逻辑 (LoadEventMap/GetSnapshot/...) 搬进来, 行为逐位不变。
- **Replay impl** (`ReplayDecisionInputProvider`): 按历史帧时间轴推进, 每 tick 返回那一刻的 6 输入快照。
- `TickAll` 不再直接调 Load*, 改 `inputs_ = provider_->BuildTickInputs()`; `ResolutionFor`/`LiveStatsFor`/`MarketCatFor` 读 `inputs_`。**这一步落地时顺势把 R-3 评审里"5 个 tick_* 散成员"收成一个 struct (老周 D-2 输入处理混乱的解药)。**

---

## 4. 分阶段 (按"分叉杀伤力"排, 老郭定序)

| 阶段 | 内容 | 归口 | 工作量 |
|---|---|---|---|
| **P0 原始帧捕获** | `RawInputRecorder` (复用 FeatureVectorRecorder jthread+jsonl 模式): live 跑时落盘每 tick 的 book(双边4ts) + score(含 inplay odds) + resolution + catalog line, 按 as_of_ts 对齐。**先有可回放数据**(现 quotes.jsonl 是特征非原始帧)。 | GM + 小余(ETL) | 2-3d |
| ✅ **P1 DecisionInputSnapshot 边界** | **已落地 (commit bf49ec1, 2026-06-01)**: 5 个非 book 输入合 `DecisionInputSnapshot tick_inputs_` + `SetReplayInputs()` 注入 seam; 1339/1339 ctest 绿, 行为逐位不变。 | GM (主干) | ✅ done |
| **P2 比分+resolution 回放** | ReplayImpl 喂 #2 比分 + #5 resolution (解锁 in-play 分支 + 结算)。验收 1+2。 | 小蒋 | 2-3d |
| **P3 catalog + sharp + live_stats 回放** | 喂 #6 catalog(line) + #3 sharp(附#2) + #4 live_stats。验收 3+4。 | 小蒋 | 2-3d |
| **P4 等价性回归门** | 历史段 replay vs 实盘 FairSrc 分布断言 (CI gate); replay_coverage→FULL。 | 小蒋 + 老郭 review | 1d |

**关键: P1 (DecisionInputSnapshot) 可先做, 它是 P2-P4 的注入点地基, 且行为不变低风险。P0 捕获并行起 (live 比赛时才出数据)。**

---

## 5. 反过度设计护栏 (三方评审一致)

- ❌ **不要**把动态态 (score/resolution/live_stats) 和静态态 (catalog) 混成一个生命周期 —— DecisionInputSnapshot 只是**聚合引用**, 各 store 仍各自 RCU 高频换 (老周边界铁律)。
- ❌ **不要**为回测造合成数据当"回测引擎" —— `kSynthetic` 正弦合成测不出决策正确性 (不下单), 降级为 hub 单测 fixture。
- ❌ **不要**增量帧 diff —— 全量帧回放, 幂等好测 (老郭)。
- ✅ 捕获格式对齐 4ts 契约 (R-20): event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts, 回放层不覆盖上游 ts。

---

## 6. 为什么这是 P0 (上线阻断)

老郭红线裁定: **补齐前, 回测 PnL/Sharpe 不得作为 MVP 上线依据** (违红线#3 精神)。而我们整季的北极星是"单策略年化 PnL ≥ $5M, Sharpe ≥ 1.5" —— **没有等价回测, 这些数字无从验证**。这比任何性能优化 (老姜已证无危机) 都该先解决。

---

## 7. 进度

- ✅ **P1 DecisionInputSnapshot 边界** — 已落地 (commit bf49ec1)。注入 seam `SetReplayInputs` 就位, 回测可单点喂帧。
- ⏭ **下一步**: **P0 RawInputRecorder** (GM+小余, live 比赛时累积原始帧) → **P2 比分+resolution 回放** (小蒋, 喂帧解锁回测"会下单/会结算")。P0 与 P2 谁先取决于是否已有可回放历史帧; 当前 quotes.jsonl 是特征非原始帧, 故 **P0 捕获是 P2 的数据前提**, 应先起。
