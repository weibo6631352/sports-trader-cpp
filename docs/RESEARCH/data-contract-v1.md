# Data Contract v1 — 需求倒推

- Owner (主笔): 小邓 (ml-engineer)
- Co-owners (需求方): 小程 (quant-signal) / 小蒋 (quant-backtest) / 小董 (data-stats) / 小袁 (microstructure, 文档 in-flight)
- Co-owners (供给方): 小余 (data-etl) / 小段 (goalserve-api-watch) / 小田#24 (data-warehouse) / 小冯 (api-watch-general) / 老李 (polymarket-protocol)
- 协调: 老胡 (pm)
- Date: 2026-05-28
- 验收人: 老雷 (GM) + 小梁 (financial-expert)
- 关联: 用户 2026-05-28 指令 (需求倒推原则), Wave 7 关键任务
- Status: v1 草稿, Sprint-1 末 (6/12) 联签前定稿
- 关联文档:
  - 需求方: `xiaodeng-ml-roadmap-data-needs-v1.md`, `xiaocheng-signal-catalog-v1.md`, `xiaodong-stats-validation-framework-v1.md`, `xiaojiang-backtest-framework-v0.1.md`
  - 供给方: `laoli-polymarket-api-spec-v1.md`, `xiaoduan-goalserve-api-spec-v1.md`
  - 上下文: `laoqian-mvp-scope-rejection-v1.md`, `laozhou-architecture-v0.2.md`, `xiaoxiao-kelly-slippage-model-v1.md`, `laojiang-latency-budget-v1.md`

> 小邓主笔按: 本文件是 SSOT (Single Source of Truth) 数据契约 v1. 用户 2026-05-28 明确发指令: "ai 和量化人员要提出他们需要什么, 比如要求输入带有时序性, 输出是什么结构, 你们都要商量好. 而不是直接告诉他们我们能提供什么. 要根据他们的需求尽可能的提供给他们需要的条件." 这条指令就是 §1 总则. 我代笔整合 4 位需求方 (小程 / 小蒋 / 小董 / 小袁) 已有文档 + 自己的需求, 出 "待商量清单"给 4 位供给方 (小余 / 小段 / 小田 / 小冯) review 反馈. 我是主笔不是裁判 — 列各方需求, 给协商表, 不替供给方拍板.

---

## 1. 总则 (需求倒推原则)

### 1.1 第一性原则 (用户 2026-05-28 原话)

> "ai 和量化人员要提出他们需要什么, 比如要求输入带有时序性, 输出是什么结构, 你们都要商量好. 而不是直接告诉他们我们能提供什么. 要根据他们的需求尽可能的提供给他们需要的条件."

**翻译成工程语言**:
- 需求方 (AI / 量化 / 回测 / 统计) **先** 写 "我需要什么 schema / 频率 / 时序对齐 / 标签 / 历史深度", 不被供给侧能力反向裁剪
- 供给方 (ETL / Goalserve / DWH / Polymarket API) **后** 评估 "你要的这个, 我能给到几成, 不够的怎么补"
- "供给方告诉需求方'我们能提供什么'" 是反模式 — 容易把需求阉割掉

### 1.2 红线 (不允许)

| 红线 | 来源 | 违反后果 |
|---|---|---|
| **未协商一致前不动手做** (任何数据流上线必须需求方 + 供给方双签) | 用户 2026-05-28 | 数据浪费 + 返工 |
| **回测 / paper / 实盘 三层 feature 同一份代码** (D-04 + GM Wave 6) | 老周 + GM | 信号不可信 |
| **PIT (point-in-time) 严格** — 任何 feature/label 不可穿越未来 | 小邓 §3.6 + 小蒋 §3 | 模型 / 回测无效 |
| **数据 schema 静默变更 = 重大违规** | 全员共识 | 下游全炸 |
| **paper / live 数据混在同一 KV / 同一 topic** | R-11 (小冯 / 老周) | 误下单 / 误算 |
| **历史与实时 schema 必须一致** (回测能复现实盘) | 小蒋 B1 + 小邓 §3.6 | 回测假阳性 |

### 1.3 每条需求都有双 owner

格式: `每条需求 = (owner_demand, owner_supply, gap, deadline)`

- `owner_demand`: 提需求的人 (小邓 / 小程 / 小蒋 / 小董 / 小袁)
- `owner_supply`: 供给方 (小余 / 小段 / 小田 / 小冯 / 老李)
- `gap`: 当前能力 vs 需求的差距 (无 / 轻微 / 中等 / 严重)
- `deadline`: 关闭 gap 的最晚日期

每条需求未签字前都标 `[需 XX confirm]`, 我不替别人拍.

---

## 2. 时序数据需求 (用户高优!)

### 2.1 为什么时序是第一公民

用户原话点名 "**要求输入带有时序性**". 体育交易系统的时序性比股票更严苛:

1. **比赛事件不可逆**: 一个进球发生即定型, 不像股票价格可双向回归 — Goalserve push 延迟 1 秒, 信号失效率显著上升 (小程 P0-02 / P1-03 测过)
2. **多源时序对齐**: Goalserve 事件流 (5-10s 跨洋) + Polymarket book (sub-sec WSS) + Pinnacle (30s 节流) 三源时序差异 5 个数量级
3. **决策窗口短**: inplay 信号 alpha decay 10s-10min, 时序错乱 = alpha 直接消失
4. **PIT 风险**: 跨数据源 timestamp 不一致会让回测穿越未来而不自知

### 2.2 时间粒度分级 (每个数据流必须声明)

| 数据流 | 时间粒度 | 用途 | demand owner | supply owner | gap |
|---|---|---|---|---|---|
| Polymarket book WSS 增量 | event-driven (tick) | P0-02, P2-07, P2-11 微观信号 | 小程 + 小袁 + 小邓 | 老李 + 小冯 | 无 (WSS 已实测 sub-sec) |
| Polymarket book snapshot (历史) | 1s (inplay) / 30s (pregame T-60min) / 5min (pregame 6h) | ML 训练 + 回测 + 容量分析 | 小邓 §3.2 + 小蒋 §3 | 小余 + 小冯 | **严重 [需 confirm]** — 1s inplay snapshot 历史能否补齐 6 个月? |
| Polymarket trade tick | event-driven | sharp money 识别, slippage 模型 | 小程 F-04 + 小邓 §3.1 + 小袁 | 老李 + 小余 | 轻微 — `taker_side` 字段需老李确认 |
| Polymarket user-channel WSS | event-driven | 我方成交回执, RM 仓位对账 | 老韩 + 小程 | 老李 | 无 |
| Goalserve inplay (按 sport) | REST poll, 实测 p50 2s / p95 7s | P0-02, P1-03, P1-06 | 小程 + 小邓 | 小段 + 小余 | **中等** — Goalserve 跨洋 p95 5-10s, P0-02 期望 1s, 见 §6.1 |
| Goalserve pregame (lineup, schedule) | REST poll, 30s ~ 5min | P0-01 滤波, P1-04 | 小程 F-14, F-15 | 小段 | 轻微 — 5min 间隔够用 |
| Pinnacle 实时赔率 | 30s (实测 [待老李/老彭确认]) | **P0-01 命脉** | 小程 §3 + 小邓 ML-F-X01 | 老李 + 老彭 (路径 A/B/C) | **严重** — 路径未定 (OQ-2) |
| Pinnacle 历史赔率 | 6 月 ~ 1 年 | P0-01 回测 + ML feature | 小程 §3.3 + 小邓 §3.5 | 老彭 (CSV) + 老李 (API) | **严重** — 历史深度 [待 OQ-2] |
| ESPN PBP (历史 + 实时) | event-driven | P0-02 score_model 训练 | 小邓 §3.5 + 小程 | 小段 (新接入) | 中等 — Sprint-2 后接入 |
| Feature store (实时, in-process) | atomic snapshot < 5ms | 策略 hot path + ML 推理 | 小程 F-01 ~ F-17 + 小邓 F-18 ~ F-22 | 小田#24 (DWH 不是 in-process) — **owner 待澄清** | 中等 — in-process feature store owner 未指定 |
| Feature store (历史回放) | KV time-versioned | 回测 + ML 训练 PIT | 小蒋 + 小邓 §3.3 | 小田 + 小余 | 中等 — replay 接口待 spec |
| Label / outcome | UMA settled + Goalserve 终局 + closing mid | 全部 ML / backtest | 小邓 §3.4 + 小蒋 + 小董 | 小余 (历史) + 老李 (实时 UMA) | 轻微 |

### 2.3 时间戳契约 (每条记录强制 4 时间戳)

任何数据流落盘 / 入 KV / 进 feature pipeline 必须带 4 个时间戳:

| 字段 | 类型 | 含义 | 用途 |
|---|---|---|---|
| `event_ts` | int64 ns UTC | 事件发生时间 (上游声明的) | 业务时序, 信号触发用 |
| `data_source_ts` | int64 ns UTC | 数据源推送/产出时间 (Polymarket WSS 帧的 ts, Goalserve response Date header) | 测上游延迟 |
| `ingestion_ts` | int64 ns UTC | 我们接收时间 (system_clock at recv) | 测端到端延迟 (老姜 budget) |
| `as_of_ts` | int64 ns UTC | 数据"可观察"时间 (用于 PIT 校验) | PIT 校验, 通常 = ingestion_ts |

**规则**:
- `event_ts <= data_source_ts <= ingestion_ts <= as_of_ts` (严格不等式, 否则数据有问题, 报警)
- 任何 feature 计算 `f(t)` 只能用 `as_of_ts <= t` 的输入 (PIT)
- backfill / 数据修正后, 新增 `corrected_at_ts`, 训练时按 `as_of_ts` 而不是 `corrected_at_ts`

**例外**:
- Goalserve REST 不返回 event push ts, 用 HTTP response Date header 作 `data_source_ts` (小段确认)
- Polymarket gamma cache 字段 (bestBid/bestAsk) 是缓存值, 不能当 PIT, 必须实时 WSS

### 2.4 PIT (Point-in-Time) 正确性 SOP

**反模式 (绝对禁止)**:
- 用 t+1 的 outcome 当 t 时刻 feature ("本场比赛 home 胜率")
- 用 corrected_at_ts 替代 as_of_ts ("UMA 仲裁改判后回头改训练集")
- 用 closing line 当 pregame feature (closing line 是结果, 不是 input)
- 多次 fit-predict 跨期数据混 (k-fold 不分时间断)

**实施**:
- 任何 feature 入库带 `as_of_ts`, join 时严格 `feature.as_of_ts <= label_window_start`
- 回测引擎强制 PIT mask (小蒋 §3 已设计, 我代笔补 spec)
- ML 训练 pipeline 用 `as_of_ts` filter, CI 测试覆盖 leak case

### 2.5 缺失值约定

| 数据流 | 缺失处理 | 理由 |
|---|---|---|
| Polymarket book stale (last_update > 30s) | mark `is_stale=true`, **不 forward-fill** | stale 是真实信号 (流动性蒸发) |
| Goalserve push 中断 (>15s 无更新) | `is_stale=true` + 触发告警 | 上游断流, ML feature 不可用 |
| Pinnacle 30s 节流间隔内 | forward-fill 至下次更新, 上限 5min | 30s 内 Pinnacle 不动是正常 |
| 比分事件缺漏 (Goalserve 漏一个 score) | 由 PBP cross-check 补 + 标 `is_imputed=true` | ML 训练时 drop, backtest 时算 |
| 历史 backfill 缺日 | NULL, 训练时按 NULL 处理 | 不假装有 |
| Feature store 计算失败 | NaN, ML 推理 fallback rule baseline | 不在错误 feature 上下注 |

**红线**: 任何 forward-fill / interpolate 操作必须落 `imputation_method` 字段, 训练 / 回测 / 监控可追溯.

### 2.6 时序对齐 (Goalserve event ↔ Polymarket book)

**这是 P0-02 / P1-03 信号能否成立的核心**.

**问题**: Goalserve event push (跨洋 p95 5-10s) 与 Polymarket book WSS (sub-sec) 时序差异 4 个数量级. 假设 Goalserve 推 "home 进球 @ t_score", Polymarket book 调整 @ t_pm. 我们看到的顺序可能是:
- 真实: t_score < t_pm (球进了 Polymarket maker 才调价)
- 我们看到: 因 Goalserve 跨洋延迟, recv_ts(score) > recv_ts(pm调整) — 顺序反了

**对齐策略 (待 §5 协商表 confirm)**:

| 策略 | 描述 | 适用 | owner |
|---|---|---|---|
| A. event_ts 主对齐 | 用 Goalserve 声明的 `event_ts` (服务器时间) 对齐 PM `last_update_ts` | 历史回测 (训练) | 小段 + 小蒋 |
| B. ingestion_ts 对齐 | 实战中用 ingestion_ts (我方接收) | 实盘策略决策 | 老姜 + 小段 |
| C. 跨源延迟补偿 | 用滚动 p50 Goalserve 跨洋延迟 (~ 2s) 倒推 event "真实发生" | 仅 P1-03 信号 (依赖时序窗口) | 小程 + 小段 |
| D. 不对齐 (event-stream 各自跑) | 各自时间轴, 不强行融合 | 微观信号 (P2-07 cascade) 仅看 PM 自己时序 | 小程 + 小袁 |

**默认**: A 用于训练, B 用于实盘, C 仅 P1-03 启用. **[需 小段 + 小蒋 + 小程 三方 confirm]**

**P0-02 score_model 输入时序锁** (我提的硬需求):
- `score_state(t)` = (score_diff, time_remaining, possession) 必须用 Goalserve 在 `as_of_ts <= t` 时刻已 push 的最新事件计算
- 不允许用比赛终局 backfill 中间 state (PIT 违例)

---

## 3. 输入 Schema (每个数据流, 历史 + 实时 一致)

### 3.1 Polymarket orderbook snapshot

**字段 (按 §2.2 时间粒度分级)**:

| 字段 | 类型 | 必需 | 用途 | 备注 |
|---|---|---|---|---|
| `market_id` | string | 必需 | 全部 | gamma id (snake), conditionId (链上 bytes32) 并存 |
| `condition_id` | bytes32 hex | 必需 | 链上回溯 | 与 clob /book?token_id 串场用 |
| `token_id_yes` / `token_id_no` | uint256 string | 必需 | 二元 token | 顺序 = outcomes index |
| `event_ts` | int64 ns | 必需 | book 上游声明 update ts | Polymarket `last_update_ts` |
| `data_source_ts` | int64 ns | 必需 | WSS 帧 ts | 测推送延迟 |
| `ingestion_ts` | int64 ns | 必需 | 我方 recv | 测端到端 |
| `as_of_ts` | int64 ns | 必需 | PIT | 默认 = ingestion_ts |
| `bids[0..10].price/size` | array | 必需 | 全部 | < 10 档补 NaN, 不留空 |
| `asks[0..10].price/size` | array | 必需 | 全部 | 同 |
| `mid_price` | float | 必需 | F-01 | 预算 (bid0+ask0)/2 |
| `spread_cents` | float | 必需 | F-03 | 预算 ask0-bid0 |
| `depth_top5_bid_usdc` | float | 必需 | F-02 / ML-F-P06 | 预算 |
| `depth_top5_ask_usdc` | float | 必需 | 同 | |
| `depth_24h_usdc` | float | 必需 | P0-01 流动性 gate | 24h 累计 |
| `is_stale` | bool | 必需 | (as_of_ts - event_ts) > 30s | true 时 ML 推理 skip |
| `tick_size` | float | 必需 | 入价计算 | 高赔冷门可能 0.001 |
| `fee_schedule.taker_rate` | float | 必需 | 净 edge | 0.03 体育 v2 |

**频率分级 (历史)** [需 小余 + 小冯 confirm 落库可行性]:

| 用途 | 频率 | 6 月 NBA 量级估算 (压缩后) |
|---|---|---|
| Pregame baseline (T-6h ~ T-1h) | 5 min | ~ 5 GB |
| Pregame 临开赛 (T-60min ~ tipoff) | 30 s | ~ 8 GB |
| Inplay (tipoff ~ end) | 1 s | ~ 200 GB (压缩 zstd) |
| Tick-by-tick (WSS full event) | event-driven | ~ 800 GB raw, ~ 200 GB compressed |

**owner_supply**: 小余 (落库) + 小冯 (Polymarket WSS 接) + 老李 (字段语义).

### 3.2 Polymarket trade tick

| 字段 | 类型 | 必需 | 用途 |
|---|---|---|---|
| `market_id`, `token_id_yes/no`, `condition_id` | string | 必需 | 标识 |
| `trade_ts` | int64 ns | 必需 | 链上 ts |
| `data_source_ts`, `ingestion_ts`, `as_of_ts` | int64 ns | 必需 | 4 时间戳 |
| `trade_price` | float (cents) | 必需 | 0-100 ¢ |
| `trade_size` | float (USDC) | 必需 | notional |
| `trade_side` | enum {BUY, SELL} | 必需 | taker 方向 | [需 老李 confirm 是否直给] |
| `taker_wallet` | hex | 推荐 | sharp 识别 (P2-11) |
| `maker_wallet` | hex | 推荐 | maker 行为 (P2-07) |
| `trade_hash` | string | 必需 | dedup + 追溯 |
| `block_number` | int64 | 必需 | UMA 仲裁追溯 |

**关键**: `trade_side` 若 API 不直给, 必须 owner_supply (老李 + 小冯) 用 book mid 推断 (price > mid → BUY, else SELL), 并标 `side_inferred=true`.

### 3.3 Polymarket WSS 流

两条 channel, 见老李 §1:

- `/ws/market` (公开订单簿增量): 用于 §3.1 snapshot 推送源
- `/ws/user` (我方订单 / 成交): 用于 RM 仓位对账

**契约**:
- 我方实现必须每条 WSS 帧落 raw payload (compressed) 入 cold storage, 至少 90 天 (老韩 audit + 复盘)
- WSS 断线必须 auto-reconnect + snapshot resync, 每次 resync 推 `resync_event` 入 feature pipeline (下游 invalidate cache)
- WSS 心跳: 无事件不推, 用本地 30s timeout heartbeat 检测 stale [需 老李 confirm 现状]

### 3.4 Goalserve inplay (按 sport)

**实测约束 (小段 v1)**:
- REST polling only, 无 WSS
- 跨洋 p50 2s, p95 7s, max 12s (proxy mode)
- 同 API 混 3 种格式 (JSON / XML / XML→JSON@prefix)
- endpoint 命名有 typo (`bsktbl/nba-shedule` 拼错才能 work)

**字段需求** (跨 sport 通用部分):

| 字段 | 来源 | 必需 | 用途 |
|---|---|---|---|
| `goalserve_match_id` | endpoint | 必需 | join key |
| `polymarket_market_id` | 我方 mapping table | 必需 | 跨源 join | [需 小段 + 老李 confirm mapping 维护方] |
| `event_ts` | Goalserve clock / event timestamp | 必需 | 业务 ts |
| `data_source_ts` | HTTP response Date header | 必需 | 替代 push ts |
| `ingestion_ts` | system_clock | 必需 | 端到端测 |
| `as_of_ts` | = ingestion_ts | 必需 | PIT |
| `home_score` / `away_score` | scores | 必需 | F-07, P0-02 |
| `time_remaining_seconds` | clock | 必需 | F-08, P0-02 |
| `period` | clock | 必需 | NBA 1-4+OT, NFL 1-4 |
| `possession` | event | 必需 (NBA/NFL) | F-S03, P0-02 |
| `event_type` | enum | 必需 | F-09, P1-03/P1-06 (goal/foul/red_card/injury/timeout) |
| `event_magnitude` | float | 推荐 | NBA 3pt vs 罚球, P1-03 ML |
| `is_clutch` | derived | 必需 | F-S04 |
| `lineup.starters/inactives` | endpoint (pregame) | 必需 (P1-04) | [需 小段 confirm 哪个 endpoint] |
| `lineup.publish_ts` | endpoint | 必需 | P1-04 时延窗口起点 |

**ETL 路径表** (小段 §2.3 硬编码, 不能瞎猜):
- `bsktbl/nba-shedule` (注意 typo), `bsktbl/nba-scores`, `bsktbl/inplay`
- `football/nfl-scores`, `football/inplay`
- `baseball/usa`, `baseball/inplay`
- `soccer/inplay` (XML), `soccernew/inplay` (JSON 包装)

**owner_supply**: 小段 (字段实测) + 小余 (落库 normalize 3 格式).

### 3.5 Pinnacle / The Odds API (P0-01 命脉)

**目前最大 gap** — 路径未定 (OQ-2).

**字段需求**:

| 字段 | 必需 | 用途 |
|---|---|---|
| `match_id` (Pinnacle) | 必需 | 跨源 join (需 mapping 表) |
| `polymarket_market_id` | 必需 | mapping (P0-01 配对核心) |
| `american_odds_home/away` | 必需 | no-vig 计算 |
| `decimal_odds_home/away` | 必需 | 同 (冗余防止换算 bug) |
| `pinnacle_update_ts` | 必需 | stale 检测 (5min 上限) |
| `data_source_ts` / `ingestion_ts` / `as_of_ts` | 必需 | 4 时间戳 |
| `betting_status` | 推荐 | open/closed/suspended |
| `max_bet` | 推荐 | Pinnacle limit 反映 sharp 程度 |

**频率**: 30s 节流 (Pinnacle 实测), 历史 6 月 ~ 1 年.

**路径决策表 (小程 §3.4)**:

| 路径 | 状态 | 成本 | owner |
|---|---|---|---|
| A. Pinnacle 官方 API | [待 老李 评估跨洋稳定性] | 中 | 老李 |
| B. The Odds API ($500/月) | [备选, OQ-14 老钱批] | $500/月 | 老李 + 老彭 |
| C. 老彭手工 + 历史 CSV | 仅 closing line | 0 | 老彭 |

**默认**: 优先 A, 1 周拉不通走 B, C 作 sanity check.

### 3.6 历史 + 实时一致性 (硬红线)

**任何数据流必须保证**:
- 实时 schema 字段集 ⊇ 历史 schema 字段集 (历史可少, 实时不可少)
- 字段命名 + 类型 + 单位完全一致 (不允许 cents vs dollars 混)
- 时间戳全部 4 个 (event/source/ingestion/as_of)
- 缺失值约定一致 (NaN / NULL / `is_stale` / `imputation_method` 统一)

**违反 = backtest 与实盘 PnL 不可对账 = 整个系统不可信**.

CI 检查 (小蒋 + 小余 协作):
- 每日跑 schema diff (实时落盘 vs 历史 schema), 任何 drift 报警
- replay 测试: 取近 24h 实时数据, 按历史 schema 重新算 feature, diff 必须为零

---

## 4. 输出 Schema

### 4.1 信号产出 (signal_event)

策略层 / ML 推理出的"建议" (不是订单, 订单在下游 OMS):

```yaml
signal_event:
  signal_id: SIG-P0-01           # 不可变 ID, 与 xiaocheng catalog 一致
  signal_version: "@v1.2"        # 参数/特征版本
  emit_ts: 2026-10-29T20:14:35.123456789Z   # ns UTC
  as_of_ts: 2026-10-29T20:14:35.000000000Z  # feature 计算的 PIT 时刻
  market_id: 0xabc...
  side: BUY | SELL
  edge_cents: 2.3                # 期望净 edge
  p_fair: 0.572                  # 我们认为的公允概率 (calibrated)
  p_market: 0.549                # market mid 推 implied prob
  kelly_size_usdc: 320           # 1/4 Kelly (小肖模型)
  confidence: 0.81               # 信号置信度 (ML) 或 1.0 (rule)
  feature_snapshot_id: "fs_..."  # 引用 §4.3 feature snapshot, 可追溯
  model_id: "score-model@lgbm-a1b2c3d-20260911T1200Z"  # 若 ML 出, rule 信号为 null
  rule_baseline_pnl_expected: 1.5   # 与 ML 对比用 (shadow)
  ttl_ms: 30000                  # 信号有效期
  meta:
    bucket: A | B | C-shadow | random   # A/B test bucket
    is_paper: false              # paper / live 分离 (R-11)
```

**红线**:
- `feature_snapshot_id` 必填, 否则信号不可复盘
- `as_of_ts` 必填, 否则 PIT 不可验
- `signal_version` + `model_id` 任一变更必须 bump 版本号

### 4.2 标签 (label) 输出

训练 + 回测 用的 outcome:

```yaml
label:
  market_id: 0xabc...
  event_id: <goalserve_match_id>
  label_type: outcome_binary | final_score | closing_mid | forward_return_30s | forward_return_5min | settle_price
  label_ts: <事件发生 ts>        # 实际终局或 forward window 结束
  label_value: float | int
  label_source: uma_settled | goalserve_score | pm_book_at_t
  is_confirmed: true | false     # UMA 仲裁挑战期 2h 内可能改
  confirmation_ts: ...
  meta:
    survivorship_filter: false   # 必须 false, 我们要 universe 级
```

**SOP**:
- 优先 `uma_settled` 作为 outcome label (小邓 §3.4)
- Goalserve 终局比分仅作为 cross-check, 不一致 > 0.5% 报警
- `forward_return_t+τ` 在 label_ts 后 τ 时间窗才能落, 中途不可见 (PIT)

### 4.3 特征 snapshot (PIT)

```yaml
feature_snapshot:
  snapshot_id: "fs_..."           # UUID
  as_of_ts: <ns UTC>
  market_id: ...
  features:
    F-01_mid_price: 0.572
    F-02_depth_24h: 85000
    F-03_spread_cents: 0.4
    F-05_pinnacle_novig: 0.555
    F-06_pinnacle_age_sec: 28
    F-07_score_diff: -3
    F-08_time_remaining: 542
    ...
    ML-F-X02_pm_pinnacle_basis_zscore_24h: 1.85
    ...
  feature_schema_version: "fs_schema@v1.3"
  imputation_methods:             # 任一 forward-fill / interpolate 落账
    F-05: forward_fill_30s
  is_complete: true               # 若 false, 哪些 feature 缺
  computed_in: backtest | paper | live   # 三选一, 必须一致 (BR-1)
```

**红线**:
- feature schema 版本变更必须 bump `feature_schema_version`
- backtest / paper / live 同一 snapshot_id 应该可复现完全相同的 feature value (浮点容差 < 1e-9)

### 4.4 回测结果 (backtest_run)

```yaml
backtest_run:
  run_id: "bt_..."                # UUID
  signal_id: SIG-P0-01
  signal_version: "@v1.2"
  params_hash: "sha256:..."       # 关键! DSR n_trials 防作弊用
  code_sha: <git sha>
  data_snapshot_id: <小余 数据集 id>
  ts_start, ts_end: ...
  IS_window: [t1, t2]
  OOS_window: [t3, t4]
  metrics:
    n_trades: 540
    hit_rate: 0.572
    edge_cents_mean: 1.8
    sharpe_annual: 1.34
    max_drawdown: 0.062
    OOS_IS_sharpe_ratio: 0.71
    deflated_sharpe: 0.97        # 小董 §6.1
    PBO: 0.18                    # 小董 §6.2
    calibration_ECE: 0.024
  trade_log_path: "...parquet"
  feature_log_path: "...parquet" # 与实盘共用 schema (§4.3)
  audit_log_path: "..."          # n_trials 防漂移用
```

**红线** (小蒋 + 小董):
- `params_hash` 必须 enforce, 任何参数变更产生新 hash (反 p-hacking)
- `data_snapshot_id` 不可变, 否则不可复现
- backtest 必须跑 RM (BR-2), 不许 mock

### 4.5 统计监控 / drift (stats_monitoring)

```yaml
stats_monitoring:
  eval_date: 2026-10-29
  signal_id: SIG-P0-01
  signal_version: "@v1.2"
  window_days: 14
  bayesian_posterior:
    mu_post: 1.21               # 小董 §4
    ci_low_95: 0.62
    ci_high_95: 1.78
    alpha_decay_level: GREEN    # GREEN/YELLOW/RED/BLACK
  drift:
    feature_psi:                # 每 feature 一份 PSI
      F-01: 0.08
      F-05: 0.21                # warning
    prediction_kl_div: 0.07
    label_drift: 0.05
  m45_gate (paper):
    G1_pnl: {pass: true, p_value: 0.034}
    G2_sharpe: {pass: true, value: 1.42, ci_lower: 0.41}
    ...
  PSD (paper vs live):
    value: 1.1
    ci_low_95: 0.4
    ci_high_95: 1.8
    status: in_range
```

**接口** (小郑 dashboard):
- 推 Pushgateway 每日 00:30 UTC (小董 §4.4)
- metric 命名空间 `stcpp_l5_signal_*`, `stcpp_l5_m45_gate_*`, `stcpp_l5_psd_*`

### 4.6 Paper / Shadow PnL (R-11 强制与生产分离)

**红线 R-11**: paper / shadow / live PnL 必须三套独立账户 + 三个 topic, 不允许同 KV 同 namespace.

```yaml
paper_trade:                    # 与下面 live_trade schema 完全一致, 仅 is_paper=true
  trade_id: ...
  bucket: A | B | C-shadow | random
  is_paper: true
  signal_id, signal_version, feature_snapshot_id: ...
  expected_fill_price: 0.572    # paper 假设
  expected_slippage_bps: 8
  expected_latency_ms: 280
  actual_fill_price: null       # paper 无 actual
  pnl_usdc: 1.23
  paper_bankroll_open: 50000
```

```yaml
live_trade:
  trade_id: ...
  is_paper: false
  signal_id, signal_version, feature_snapshot_id: ...
  expected_fill_price: 0.572
  expected_slippage_bps: 8
  expected_latency_ms: 280
  actual_fill_price: 0.575     # live 必填
  actual_slippage_bps: 12
  actual_latency_ms: 312
  actual_fill_pct: 0.85        # 部分成交
  pnl_usdc: 0.94
  chain_tx_hash: 0x...
```

**PSD (Paper-Prod Sharpe Deviation)** 分析依赖此对齐 (小董 §7).

---

## 5. 数据源协商表 (核心 — 待 6/12 联签)

格式: `需求方 | 需求项 | 供给方 | 当前能力 | gap | 解决方案 | 截止`

| # | 需求方 | 需求项 | 供给方 | 当前能力 | gap | 解决方案 | 截止 |
|---|---|---|---|---|---|---|---|
| C-01 | 小邓 | Polymarket 1s inplay book snapshot 历史 6 月 | 小余 + 小冯 | 现状未实测 [需 confirm] | **严重** (可能没历史源, 需爬) | (a) 小冯 NOW 起实时录 1s (b) 历史从第三方买 (Polymarket data archive?) (c) M+24 周才有 6 月 | 6/26 (实测量级) |
| C-02 | 小邓 + 小程 | Pinnacle 30s 实时 + 6 月历史 | 老李 + 老彭 | 路径未定 (OQ-2) | **严重** | A (官方) / B (The Odds API $500/月) / C (老彭 CSV closing only) | 6/12 |
| C-03 | 小程 + 小邓 | Goalserve inplay 1s 粒度 | 小段 | REST p50 2s, p95 7s | **中等** (跨洋限制) | (a) US proxy + colo (老吴) (b) 上游 push (Goalserve 无 WSS, 不可行) (c) 接受 2-3s 粒度, P0-02 信号窗调宽 | 6/12 |
| C-04 | 小邓 + 小程 | ESPN PBP 历史 + 实时 | 小段 | 未接入 | 中等 (公开 API, Sprint-2 接) | 小段 7/3 完成接入 + rate limit 实测 | 7/3 |
| C-05 | 小程 + 小蒋 | trade_side 字段 (taker 方向) | 老李 + 小冯 | API 是否直给 [待 confirm] | 轻微 | 直给 OR 用 mid 推断 + 标 inferred | 6/12 |
| C-06 | 小邓 + 小袁 | taker_wallet / maker_wallet (sharp 识别) | 老李 + 小冯 | 链上数据公开 | 轻微 | data-api `/trades?user=` 或 onchain 解析 | 6/19 |
| C-07 | 小邓 + 小程 | F-18 ~ F-22 (我额外加的 5 个 feature) | 小田#24 (in-process feature store) | owner 未指派 [需 confirm] | **中等** | 待澄清 in-process feature store owner (小田 是 DWH, 可能不是同一人) | 6/12 |
| C-08 | 小邓 + 小蒋 | feature store atomic snapshot < 5ms 拉取 | 小田 / 待定 | 未实现 | 中等 | M+2 (Sprint-2) C++ feature pipeline 雏形 + pyo3 binding (小蒋 §2.3) | 7/24 |
| C-09 | 全员 | 4 时间戳契约 (event/source/ingestion/as_of) | 小余 + 小段 + 小冯 | 部分有 (Goalserve 缺 event_ts) | 中等 | 小段: HTTP Date 作 data_source_ts; 小余: 落库 schema 强制 4 ts | 6/19 |
| C-10 | 小邓 + 小蒋 | PIT-correct feature replay (回测用 ts 重算 feature) | 小田 + 小余 | 未实现 | **严重** | 小田 feature store time-versioned KV; 小蒋 replay loader 强制 as_of_ts mask | 7/24 |
| C-11 | 小邓 | 历史 outcome label (UMA settled, 6 月) | 小余 + 老李 | UMA 在链上可拉 | 轻微 | 小余 灌库, 老李 提供 settled 字段语义 | 6/26 |
| C-12 | 小董 | shadow random-entry bucket | 小蒋 (paper engine) | 未实现 | 中等 | 小蒋 paper engine v1 内置 shadow bucket (R-11 隔离) | M+4 |
| C-13 | 小邓 + 小董 | 实时 prediction log (input/output 反查) | 小田 + 小邓 | 未实现 (M5 后) | M5 阻塞 | M5 后才上, NOW 不做 | T+24 周 |
| C-14 | 小蒋 + 小邓 | replay 接口 (给定 ts, 回放当时数据) | 小宋 + 小田 | 小宋 v0.1 有 EventRecorder | 轻微 | 复用小宋 ReplayDriver + parquet → MessagePack adapter | 7/10 |
| C-15 | 小邓 + 小程 | Polymarket ↔ Goalserve ↔ Pinnacle market mapping | 小段 + 老李 + 老彭 | 不存在 | **中等** | NOW 起 owner_supply 维护 mapping CSV, M+2 入 DWH | 6/19 |
| C-16 | 全员 | schema 变更 ADR + 24h 通知 | 老周 + 小米 (doc-curator) | ADR 流程已有 | 轻微 | 任何 §3 schema 变更走 ADR + 通知 dist-list | NOW |
| C-17 | 小邓 | ONNX 模型部署 size < 100MB | 老姜 + 老周 | 未实测 | 中等 | M+24 周后实测 baseline 模型 | T+26 周 |
| C-18 | 小袁 + 小程 | book WSS event-driven full tape (P2-07 cascade detect) | 小冯 + 老李 | 实测 sub-sec, 但落盘策略未定 | 中等 | 小冯 raw frame 落 cold storage (90 天 + audit) | 6/26 |
| C-19 | 小袁 (TBD) | order arrival / cancel event 拆分 | 小冯 + 老李 | [待 小袁 confirm 需求, microstructure v1 in-flight] | TBD | 等小袁 v1 出 | TBD |
| C-20 | 小董 + 小邓 | UMA 仲裁挑战期 (2h) 内 label 不算 confirmed | 老李 + 小余 | UMA 流程已知 | 轻微 | label.is_confirmed 字段 + 训练 only 用 confirmed | 6/19 |

**签字栏在 §10**.

---

## 6. 缺口清单 (top gaps)

按严重程度排序, top 3 阻塞 v1 Data Contract 签字:

### 6.1 Top Gap 1: Pinnacle 历史 + 实时数据 (C-02, OQ-2)

**严重程度**: 阻塞 P0-01 (MVP 首发信号, KR-C 红线)
- 无 Pinnacle = SIG-P0-01 不成立 = MVP 无可上线信号 = 项目失败
- 路径 A/B/C 任意一个 work 即可, 但 NOW (Sprint-1) 必须有一条 work
- **决策点**: 6/12 老李给出路径 A 跨洋稳定性实测, 不达标走 B ($500/月 老钱批 OQ-14)

**对接人**: 老李 (路径 A 实测) + 老彭 (路径 C CSV) + 老钱 (OQ-14 预算)

### 6.2 Top Gap 2: Goalserve inplay 时序粒度 vs P0-02 信号窗口 (C-03)

**问题**: Goalserve REST p50 2s / p95 7s, P0-02 score-price-mismatch 期望 1s 状态更新, gap 中等.

**影响**:
- 不能直接接受 5-10s 状态延迟 — P0-02 alpha decay 10s 量级, 信号失效
- 但 Goalserve 无 WSS, 跨洋延迟硬限制

**解决方案 (待 6/12 联签)**:
- **首选**: 老吴 (cross-region) US 节点 proxy + colo, 实测能否压到 p95 2s 内
- 备选: 接受 2-3s 粒度, P0-02 触发窗口拉到 30s (小程 + 小梁 评估 alpha 损失)
- 备选: 双源 (ESPN PBP + Goalserve), ESPN 可能更快, 用 ESPN 主 / Goalserve 校验

**对接人**: 小段 (实测) + 老吴 (colo) + 小程 (信号窗口调整 if 走备选)

### 6.3 Top Gap 3: 1s inplay book snapshot 历史 6 月 (C-01)

**问题**: ML 训练 + 回测都依赖 6 月 NBA 1s 历史 book snapshot, 但 Polymarket 不一定有 archive.

**风险**:
- 若历史不可得, ML 阶段 1 (T+24 周 baseline LightGBM) 推迟 6 月 (要等实时录到 6 月数据)
- v1 backtest P0-01 不依赖 1s 粒度 (pregame 5min 即可), 不阻塞 MVP
- 但 P0-02 / P2-07 阻塞

**解决方案**:
- (a) 小冯 NOW 起开实时录 (无论是否上 v1 MVP, 数据先攒) — 6 个月后自然有
- (b) 第三方数据服务 (Polymarket 是否有官方 archive? 第三方爬虫服务?) — 老李 + 老彭 评估
- (c) 接受 ML 阶段 1 起步在 T+24+6=T+30 周, 不在 T+24 周

**对接人**: 小冯 (实时录) + 小余 (落库容量预算 ~ 200 GB) + 老李 (官方 archive 询问)

### 6.4 其他缺口 (中等)

- C-07 in-process feature store owner 未指派 (小田#24 是 DWH, 不是 in-process). **[需 老胡 + 老雷 澄清 — 这位是不是小田#24, 还是另有其人]**
- C-10 PIT-correct feature replay 未实现, 阶段 1 ML 训练前必须 ready (M+6 内)
- C-15 三源 market mapping (Polymarket ↔ Goalserve ↔ Pinnacle) NOW 起维护, 否则 cross-source feature 算不出
- C-19 小袁 microstructure v1 还没出, 我无法替他列字段需求, 标 TBD

### 6.5 历史数据采集成本预算 (给老钱 / 老雷)

| 项 | 来源 | 预估月成本 | 6 月一次性成本 |
|---|---|---|---|
| Pinnacle (路径 B The Odds API) | 第三方 | $500/月 | $3000 |
| Polymarket 历史 archive | [待 老李 询价] | TBD | TBD |
| Goalserve 当前订阅 | 已有 | (已纳入) | 0 |
| ESPN PBP | 公开免费 | 0 | 0 |
| 538 Elo CSV | 公开免费 | 0 | 0 |
| Rotoworld lineup webhook | 商业 | [TBD M5 后] | 0 |

**总 NOW 预算请求 (Sprint-1 ~ Sprint-3)**: ≈ $1500 (路径 B 3 个月) + Polymarket archive TBD. **[需 老钱 OQ-14 + 老雷 批]**

---

## 7. 红线 (公司不允许的事)

| # | 红线 | 来源 | 检测 |
|---|---|---|---|
| R-1 | 回测与实盘 feature 双套逻辑 | D-04 + GM Wave 6 | CI: 同一 snapshot_id 在 backtest / paper / live 必须算出 bit-identical feature value |
| R-2 | 数据 schema 静默变更 | 全员共识 | CI: schema diff daily, 任何 drift 报警 + ADR 强制 |
| R-3 | PIT 违例 (穿越未来) | 小邓 §3.6 + 小蒋 BR-3 | CI: 训练前扫 future-leak (`feature.as_of_ts > label.label_ts` 的 row 必须 zero) |
| R-4 | 时序对齐错乱 (跨源 ts 用错) | §2.6 | CI + replay 测试 |
| R-5 | paper / shadow / live 数据混 | R-11 (小冯 + 老周) | 部署: 三套独立 KV namespace + topic, 不允许复用 |
| R-6 | feature schema 未 bump 版本而值变 | §4.3 | CI: feature_schema_version hash 校验 |
| R-7 | n_trials 隐性漂移 (跑了 100 次只写 1 次) | 小董 §6.4 | audit log enforce, DSR 计算自动数 |
| R-8 | label leakage (closing line 当 pregame feature) | §2.4 + 小邓 §3.6 | code review + 单元测试 |
| R-9 | survivorship bias (只看触发过的比赛) | 小邓 §3.4 | 小余: 数据仓 universe 级 (含未下单比赛) |
| R-10 | UMA 未 confirm 的 label 当训练正样本 | C-20 | label.is_confirmed=false 必须 drop |
| R-11 | OOS 偷看 (任何 hyperparameter search 用 OOS) | 小蒋 BR-3 + 小董 §3.3 | embargo + audit log |
| R-12 | API key 落 log / metric / proxy | 小段 §2.1 + 老沈 | 必须 REDACTED, code review enforce |
| R-13 | 用 corrected_at_ts 替代 as_of_ts | §2.3 | CI: 训练 join 强制按 as_of |
| R-14 | C++ feature pipeline 用 Python 重写 (BR-3 / BR-4) | 小蒋 + 老周 | PR 强扫: 新增 feature 必须同 PR 改 C++ + Python binding |

---

## 8. 变更管控

### 8.1 数据 schema 升级 ADR 流程

任何 §3 / §4 schema 字段增删改, 走以下流程:

1. 提议方 (任意 owner) 起 ADR 草稿 `docs/ADR/data-schema-XXX.md`
2. **24h 通知 dist-list** (全部 §0 co-owners) — 用 git PR + 邮件双轨
3. **冷却 48h** — 任何 owner 可在此期间反对 (1 vote = block)
4. 通过后 bump 版本号: schema `@vN+1`, 全部下游 `schema_version` 字段同步
5. 旧版保留 90 天 (CI 自动测试 backward compat)
6. 小米 (doc-curator) 归档 ADR 入 INDEX.md

### 8.2 模型 / 信号版本管控

- `signal_id` 不可变 (永久), `signal_version @vN` 任何参数变更 bump
- `model_id = {name}@{arch}-{git_sha7}-{trained_ts}` 强制唯一
- registry.yaml 单一文件 (model_id → onnx_path + feature_spec + metrics)
- hot reload: SIGHUP + atomic ptr swap, 校验 feature_schema match

### 8.3 doc-curator 协作

- 小米 (#40 doc-curator) 每月 review docs 健康度, schema 文件归档进 INDEX.md
- 本 Data Contract v1 是 SSOT, 任何分散信息 (小程 catalog / 小董 stats / 小蒋 backtest 等) 与本文件冲突时 **以本文件为准**
- 小米发现冲突立 INCIDENT ticket, 走 §8.1 流程

---

## 9. 协商节奏

### 9.1 Sprint-1 末 (2026-06-12) v1 联签

**强制到场** (任一缺席视为反对):
- 需求方: 小邓 / 小程 / 小蒋 / 小董 / 小袁
- 供给方: 小余 / 小段 / 小田 / 小冯 / 老李
- 协调: 老胡
- 验收: 老雷 + 小梁

**会议议程** (老胡定时间, 建议 90 min):
- 30 min: §5 协商表逐条 walk through, gap 表态 (yes/no/need-time)
- 30 min: §6 top 3 gaps 决策 (Pinnacle 路径 / Goalserve 粒度 / 历史 book)
- 20 min: §7 红线 + §8 变更管控 review
- 10 min: 签字 (§10)

### 9.2 隔周三 "数据契约站会"

- 主席: 我 (小邓) 或老胡
- 必到: 小余 + 小段 + 小冯 + 小田 (供给侧)
- 选到: 当周有需求变更的需求方
- 30 min, 议程:
  - 上周 gap 关闭进度
  - 本周新增 gap / schema 提议
  - 阻塞决议

### 9.3 月度 review

- 每月末由小董 + 小米 产出 "data contract health report"
- 内容: gap 数量趋势 / schema 变更次数 / CI violation 次数 / PIT 违例数 / R-1 ~ R-14 红线违反次数
- 老雷月度站会 5min summary

### 9.4 重大变更 (v2+) 触发

- 任一供给方接入新数据源 → 必须更新本文件 § 3
- v1 信号上线后 4 周, 出 v1.1 (灌入实测数据, OQ 闭环)
- M5 解锁 ML scope 时, 出 v2 (新增 prediction log / drift / champion-challenger 章节)

---

## 10. 签字栏 (待 2026-06-12 联签)

### 10.1 需求方

- [ ] 小邓 (ml-engineer) — 主笔, ML 数据需求 (§3.1-3.7 of `xiaodeng-ml-roadmap-data-needs-v1.md`)
- [ ] 小程 (quant-signal-research) — 信号要的数据 (§5 of `xiaocheng-signal-catalog-v1.md`)
- [ ] 小蒋 (quant-backtest) — backtest 要的数据 (§3 of `xiaojiang-backtest-framework-v0.1.md`)
- [ ] 小董 (data-stats) — 统计验证要的数据 (§5, §6 of `xiaodong-stats-validation-framework-v1.md`)
- [ ] 小袁 (microstructure) — **微观结构需求 TBD (v1 待小袁 microstructure-v1 出后补)**

### 10.2 供给方

- [ ] 小余 (data-etl) — 历史数据落库 + ETL pipeline
- [ ] 小段 (goalserve-api-watch) — Goalserve 字段 + 跨洋延迟 + 3 格式 normalize
- [ ] 小田#24 (data-warehouse) — DWH schema **[need confirm: 是否同时负责 in-process feature store? 还是另有 owner?]**
- [ ] 小冯 (api-watch-general) — Polymarket WSS / book snapshot 实时录
- [ ] 老李 (polymarket-protocol-expert) — Polymarket API spec + UMA settled label

### 10.3 协调 + 验收

- [ ] 老胡 (pm) — 跨部门会议节奏 + gap 跟踪
- [ ] 老雷 (GM) — 战略验收 (含 OQ-14 预算)
- [ ] 小梁 (financial-expert) — 业务验收 (信号路线对齐)

### 10.4 知会方 (非强制签字, 但需 ack)

- 老周 (cpp-chief-architect) — D-04 + ADR 流程
- 老韩 (risk-engineer) — RM audit + R-11 隔离
- 老姜 (latency-engineer) — 时序对齐 + colo 决策
- 老钱 (cpo-product-strategy) — MVP scope + OQ-14 预算
- 小郑 (observability) — drift dashboard 接口
- 小米 (doc-curator) — ADR 归档
- 老吴 (cross-region) — US proxy / colo 部署
- 老彭 (betting-industry) — Pinnacle 路径 + sharp money 标注

---

## 附录 A: 关键术语

- **PIT (Point-in-Time)**: 任何 feature/label 计算只用"彼时彼刻就能拿到的数据", 不穿越未来
- **as_of_ts**: 数据"可观察"时间, PIT 校验的主键
- **DSR (Deflated Sharpe Ratio)**: Bailey & López de Prado 2014, 修正 selection bias 的 Sharpe
- **PBO (Probability of Backtest Overfitting)**: CSCV 估计的过拟合概率
- **PSD (Paper-Prod Sharpe Deviation)**: 实盘 vs paper Sharpe 差 (小董 §7)
- **PSI (Population Stability Index)**: feature drift 度量, > 0.2 yellow, > 0.3 red
- **CLV (Closing Line Value)**: 与 Pinnacle closing line 对比的 edge 度量
- **R-11**: paper / shadow / live 三层数据隔离红线 (小冯 + 老周 定义)
- **D-04**: 回测 / paper / 实盘共用 feature pipeline 红线 (老周架构 v0.2)

## 附录 B: 修订记录

| 版本 | 日期 | 修订 | 主笔 |
|---|---|---|---|
| v1.0 草稿 | 2026-05-28 | 初版, Wave 7 关键任务, 需求倒推原则确立 | 小邓 |
| v1.0 联签 | 2026-06-12 (预计) | 联签生效 | 全员 |
| v1.1 | T+8 周 (预计) | 灌入 OQ-1 ~ OQ-8 实测 + Pinnacle 路径定稿 | 小邓 |
| v2.0 | T+24 周 (预计) | ML scope 解锁后扩展 | 小邓 |

---

**v1 草稿收尾**. 本文件是 SSOT, 任何分散信息以本文件为准. 6/12 联签前未签字的 [需 XX confirm] 标记必须闭环. 我作为主笔不替供给方拍板, 给协商表 + 缺口清单, 协调由老胡, 战略验收由老雷.

— 小邓 (主笔), 2026-05-28
