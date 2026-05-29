# xiaotian-feature-store-schema-v1.md — Feature Store Schema v1.0

- owner: 小田 (dwh-analyst, #24, D 单元 IC)
- last_review: 2026-05-29
- 关联 ADR: 2026-05-29-data-model-strategy-vendor-agnostic.md (ADR-037)
- 关联文档:
  - xiaotian-parquet-partition-v1.md (分区策略 SSOT)
  - data-contract-v1.md (需求倒推契约)
  - xiaodeng-ml-data-infra-v1.md (ML 数据基础设施)
  - include/stcpp/data/parquet_writer.hpp (Parquet ABI stub)
  - include/stcpp/ml/feature_snapshot.hpp (FeatureSnapshot 32 feature enum)
  - include/stcpp/microstructure/orderbook.hpp (OrderBookSnapshot)
  - include/stcpp/data/goalserve_record.hpp (GameRecord / OddsRecord)
  - include/stcpp/data/data_contract.hpp (kBookmakerIds ABI)
- 下游消费者 (需读此 spec):
  - 小段 #37 (Goalserve adapter — 消费契约 §5)
  - 小冯 #?  (orderbook adapter — 消费契约 §5)
  - 小蒋 #20 (回测引擎 — §3.4 point-in-time replay)
  - 小邓 #31 (ML 训练 — §3.3 feature table)

---

## 1. 背景与目标

Feature Store 是离线分析栈的核心数据集市，承接两路 adapter 的归一化输出：

| adapter | owner | 输出类型 | feature store 角色 |
|---|---|---|---|
| Goalserve adapter | 小段 #37 | GameRecord / OddsRecord | 比分/统计/事件/bookmaker 赔率 → features |
| orderbook adapter | 小冯 | OrderBookSnapshot | Polymarket 订单簿微观结构 → features |

Feature Store 设计原则：
1. **vendor-agnostic**: 不硬编码 Goalserve / Polymarket 字段名，通过 adapter 归一化中间表示接入
2. **point-in-time 正确 (防 look-ahead)**: feature 的 as_of_ts 严格 <= 任何消费该 feature 的决策时刻
3. **回测 = 实盘同源**: 同一 feature store Parquet 文件同时喂 backtest 和 paper engine (红线, R-7)
4. **R-20 4 ts 全程贯通**: event_ts <= data_source_ts <= ingestion_ts <= as_of_ts 在所有 table 强制

数据覆盖范围: Polymarket 体育全盘口 (Moneyline / Totals / Spreads / 分节 / 系列赛 / Prop / Outright)，跨 8 sport (Soccer / Basketball / AmFootball / Baseball / Tennis / Hockey / Cricket / Rugby)。

---

## 2. Parquet 分区策略 (SSOT from xiaotian-parquet-partition-v1.md)

### 2.1 分区键

```
sport / event_date / market_type
```

分区路径示例:
```
data/feature_store/
  sport=Soccer/
    event_date=2024-11-15/
      market_type=Moneyline/
        part-0001.parquet
      market_type=Totals/
        part-0001.parquet
  sport=Basketball/
    event_date=2024-11-15/
      market_type=Moneyline/
        part-0001.parquet
```

注意: 本 feature store 用 `event_date` (比赛日期，来自 scheduled_ts_ns 或 event_ts_ns 降精度) 而不是原 paper_mldata 的 `year/week`，原因:
- 回测按比赛日切片更直观 (一场比赛的所有 tick 在同一目录)
- DuckDB `WHERE event_date BETWEEN '2024-11-01' AND '2024-11-30'` partition pruning 友好
- 老彭历史回填对齐比赛日期

`week` 粒度查询由 DuckDB `DATE_TRUNC('week', event_date)` 在 SQL 层完成，无需 Parquet 分区。

### 2.2 文件大小目标

| 指标 | 目标 |
|---|---|
| 单文件 | 64 MB ~ 256 MB |
| row group | 64 MB |
| compression | zstd level=19 |
| encoding | DELTA_BINARY_PACKED (int64 ts) + PLAIN_DICTIONARY (string) |

### 2.3 Retention

| 层级 | 范围 | 存储 | 保留 |
|---|---|---|---|
| 热层 | 当前赛季 + 前 8 周 | 本地 NVMe | 永久 (ML 训练随时取) |
| 冷层 | 2 年内 | S3 (M2+ 迁) | 2 年 |
| 归档 | > 2 年 | Glacier | 5 年 |

---

## 3. Schema 定义

Feature Store 由 3 张 Parquet 表组成：

| 表名 | 来源 | 行粒度 | 主要用途 |
|---|---|---|---|
| `game_snapshot` | Goalserve adapter (小段) | 每次 poll 一行/match | 比分/状态/事件信号 |
| `orderbook_snapshot` | orderbook adapter (小冯) | 每次 WSS 推送一行/market | 订单簿微观结构信号 |
| `feature_row` | join (game + book + bookmaker) | 策略决策点一行 | ML 训练/回测/实盘同源 |

### 3.1 game_snapshot — Goalserve adapter 归一化输出

行粒度: 每次 Goalserve poll 完成后，每场比赛输出一行快照。

| 列名 | 类型 | 来源 | 说明 |
|---|---|---|---|
| **R-20 4 ts** | | | |
| event_ts | INT64 (ns) | GameRecord.ts.event_ts_ns | 比赛事件时间 (比赛开始/进球等) |
| data_source_ts | INT64 (ns) | GameRecord.ts.data_source_ts_ns | scores@ts 直采, UPSTREAM_PAYLOAD 优先 |
| ingestion_ts | INT64 (ns) | GameRecord.ts.ingestion_ts_ns | 本地收到 payload 时刻 |
| as_of_ts | INT64 (ns) | GameRecord.ts.as_of_ts_ns | 本条快照的 point-in-time 锚 |
| ds_origin | UINT8 | DataSourceTsOrigin enum | 0=PayloadScoresTs 1=LastUpdate 2=IngestionFallback |
| **分区键** | | | |
| sport | STRING | GoalserveSport enum name | "Soccer" / "Basketball" / ... |
| event_date | DATE32 | date(scheduled_ts_ns or event_ts_ns) | 比赛日期 (分区用) |
| market_type | STRING | 由 adapter 推断 | "Moneyline" / "Totals" / "Spreads" |
| **业务键** | | | |
| match_id | STRING | GameRecord.match_id | Goalserve match id (adapter 归一化后, 不含 vendor 前缀) |
| league_id | STRING | GameRecord.league_id | |
| home_team | STRING | GameRecord.home_team | |
| away_team | STRING | GameRecord.away_team | |
| **比分** | | | |
| score_home_total | INT32 | ScorePair.home_total | 总分 |
| score_away_total | INT32 | ScorePair.away_total | |
| score_home_p1 ~ p12 | INT32 x12 | ScorePair.home_periods[0..11] | 分节/分盘得分, -1=未开始 |
| score_away_p1 ~ p12 | INT32 x12 | ScorePair.away_periods[0..11] | |
| used_periods | UINT8 | ScorePair.used_periods | 已填充节数 |
| last_completed_period | UINT8 | ScorePair.last_completed_period | 已结束节 (分节盘口结算用) |
| **比赛状态** | | | |
| time_status | UINT8 | TimeStatus enum value | 0=NotStarted 1=InPlay ... 99=Removed |
| period | UINT8 | GameRecord.period (optional) | 当前节 (1-based), 0=无 |
| elapsed_sec | INT32 | GameRecord.elapsed_sec (optional) | 当前节已用秒, -1=无 |
| scheduled_ts | INT64 (ns) | GameRecord.scheduled_ts_ns (optional) | 排定开赛时间, 0=无 |
| **Bookmaker 赔率 (可选列, 来自同 match OddsRecord)** | | | |
| bm_yes_odds_10bet | DOUBLE | OddsRecord slots[0].odds_yes | decimal odds YES; NaN=缺失 |
| bm_no_odds_10bet | DOUBLE | OddsRecord slots[0].odds_no | |
| bm_valid_10bet | BOOL | OddsRecord slots[0].valid | |
| bm_yes_odds_williamhill | DOUBLE | slots[1] | |
| bm_no_odds_williamhill | DOUBLE | | |
| bm_valid_williamhill | BOOL | | |
| bm_yes_odds_bet365 | DOUBLE | slots[2] | |
| bm_no_odds_bet365 | DOUBLE | | |
| bm_valid_bet365 | BOOL | | |
| bm_yes_odds_marathon | DOUBLE | slots[3] | |
| bm_no_odds_marathon | DOUBLE | | |
| bm_valid_marathon | BOOL | | |
| bm_yes_odds_unibet | DOUBLE | slots[4] | |
| bm_no_odds_unibet | DOUBLE | | |
| bm_valid_unibet | BOOL | | |
| bm_yes_odds_betvictor | DOUBLE | slots[5] | |
| bm_no_odds_betvictor | DOUBLE | | |
| bm_valid_betvictor | BOOL | | |
| bm_yes_odds_1xbet | DOUBLE | slots[6] | |
| bm_no_odds_1xbet | DOUBLE | | |
| bm_valid_1xbet | BOOL | | |
| bm_yes_odds_betano | DOUBLE | slots[7] | |
| bm_no_odds_betano | DOUBLE | | |
| bm_valid_betano | BOOL | | |
| bm_market_id | STRING | OddsRecord.market_id | 1x2 / OU_2.5 / AH_-0.5; 空串=无赔率 |
| bm_abi_version | STRING | kBookmakerAbiVersion | "bm-abi-v1.0-8bm" (schema metadata 冗余列) |

PIT 约束: game_snapshot 写入时, as_of_ts 必须 <= 写入时刻 (now_ns)。adapter 不得用未来信息回填历史行。

### 3.2 orderbook_snapshot — orderbook adapter 归一化输出

行粒度: 每次 Polymarket WSS book_update 推送，每个 token (YES/NO) 输出一行。

| 列名 | 类型 | 来源 | 说明 |
|---|---|---|---|
| **R-20 4 ts** | | | |
| event_ts | INT64 (ns) | OrderBookTs.event_ts_ns | WSS 上游事件时间 |
| data_source_ts | INT64 (ns) | OrderBookTs.data_source_ts_ns | WSS payload @ts (ms epoch 转 ns) |
| ingestion_ts | INT64 (ns) | OrderBookTs.ingestion_ts_ns | 本地收字节流时刻 |
| as_of_ts | INT64 (ns) | OrderBookTs.as_of_ts_ns | evaluate 时刻 |
| **分区键** | | | |
| sport | STRING | 由 market_id 解析或 metadata 传入 | |
| event_date | DATE32 | date(event_ts) | |
| market_type | STRING | 由 market_id 解析 | "Moneyline" / "Totals" / "Spreads" |
| **业务键** | | | |
| market_id | STRING | OrderBookSnapshot.market_id | Polymarket condition_id / asset_id |
| token_side | STRING | "YES" or "NO" | 该行代表 YES 还是 NO token 的簿 |
| **最优价格** | | | |
| bid_p0 | DOUBLE | bid[0].price | 最优买价 |
| bid_s0 | DOUBLE | bid[0].size_usdc | L1 买量 USD |
| bid_p1 | DOUBLE | bid[1].price | |
| bid_s1 | DOUBLE | bid[1].size_usdc | |
| bid_p2 | DOUBLE | bid[2].price | |
| bid_s2 | DOUBLE | bid[2].size_usdc | |
| bid_p3 | DOUBLE | bid[3].price | |
| bid_s3 | DOUBLE | bid[3].size_usdc | |
| bid_p4 | DOUBLE | bid[4].price | |
| bid_s4 | DOUBLE | bid[4].size_usdc | |
| ask_p0 | DOUBLE | ask[0].price | 最优卖价 |
| ask_s0 | DOUBLE | ask[0].size_usdc | L1 卖量 |
| ask_p1 ~ ask_p4 | DOUBLE x4 | ask[1..4].price | |
| ask_s1 ~ ask_s4 | DOUBLE x4 | ask[1..4].size_usdc | |
| **微观结构派生** | | | |
| mid | DOUBLE | (bid_p0 + ask_p0) / 2 | 中间价 |
| spread_bps | INT32 | (ask_p0 - bid_p0) / mid * 10000 | OrderBookSnapshot.spread_bps |
| top3_depth_usdc | DOUBLE | OrderBookSnapshot.top3_depth_usdc | ±2 tick 累计深度 |
| tick_size | DOUBLE | OrderBookSnapshot.tick_size | 0.01 / 0.001 |
| microprice | DOUBLE | L1Probe.microprice | capped microprice |
| imbalance | DOUBLE | L1Probe.imbalance | in [-1,1] |
| last_trade_ts | INT64 (ns) | OrderBookSnapshot.last_trade_ts_ns | staleness 判断 |

PIT 约束: data_source_ts 来自 WSS payload timestamp 字段, 禁止用本地 now() 替代 (R-20)。

### 3.3 feature_row — 策略决策点 feature 表 (ML 训练/回测/实盘同源)

行粒度: 每次策略 evaluate 触发点一行。该表是 game_snapshot + orderbook_snapshot 的 point-in-time join 产物，同时包含 TrainingLabel (结算后回填)。

与 FeatureSnapshotRecord (parquet_writer.hpp) ABI 完全对齐，新增 as_of_ts <= join 时刻约束。

| 列名 | 类型 | 来源 | 说明 |
|---|---|---|---|
| **R-20 4 ts** | | | |
| event_ts | INT64 (ns) | FeatureSnapshot.event_ts | |
| data_source_ts | INT64 (ns) | FeatureSnapshot.data_source_ts | |
| ingestion_ts | INT64 (ns) | FeatureSnapshot.ingestion_ts | |
| as_of_ts | INT64 (ns) | FeatureSnapshot.as_of_ts | 决策锚, 所有 feature 必须 <= as_of_ts |
| **分区键** | | | |
| sport | STRING | | |
| event_date | DATE32 | | |
| market_type | STRING | | |
| **PIT 锚 (ML-R8)** | | | |
| feature_snapshot_id | UINT64 | FeatureSnapshot.feature_snapshot_id | join key |
| audit_id | BINARY(16) | FeatureSnapshot.audit_id_bytes | ULID |
| signal_id | UINT8 | FeatureSnapshot.signal_id_u8 | P0-01=1 P0-02=2 ... |
| market_id | STRING | FeatureSnapshot.market_id | |
| **32 Feature 列 (feat_00..feat_31, 顺序与 FeatureName enum 1:1)** | | | |
| feat_00 | FLOAT | PM_mid_bid | PM YES bid (dollar prob) |
| feat_01 | FLOAT | PM_mid_ask | PM YES ask |
| feat_02 | FLOAT | PM_book_depth_top3_yes | top-3 levels YES 总 USDC |
| feat_03 | FLOAT | PM_book_depth_top3_no | |
| feat_04 | FLOAT | Goalserve_devig_p_yes_fair | de-vig fair prob (ADR-008: 不再叫 Pinnacle_*) |
| feat_05 | FLOAT | Goalserve_overround_avg | 跨 8 家 overround 均值 |
| feat_06 | FLOAT | edge_bps | signal edge bps |
| feat_07 | FLOAT | kelly_full | Kelly full (不含 0.25 shrink) |
| feat_08 | FLOAT | expected_fill_rate | SlippageModel |
| feat_09 | FLOAT | slippage_bps | SlippageModel |
| feat_10 | FLOAT | live_section | LiveSection enum cast |
| feat_11 | FLOAT | game_state | bitmask: bit0=live bit1=ended bit2=delayed |
| feat_12 | FLOAT | kickoff_seconds_until | (kickoff_ts - as_of_ts)/1e9, 负=已开赛 |
| feat_13 | FLOAT | inplay_minutes | 已比赛分钟 |
| feat_14 | FLOAT | score_home | |
| feat_15 | FLOAT | score_away | |
| feat_16 | FLOAT | period | 当前 period/quarter/inning |
| feat_17 | FLOAT | vol_24h | 24h 成交量 USDC |
| feat_18 | FLOAT | vol_1h | |
| feat_19 | FLOAT | vol_5m | |
| feat_20 | FLOAT | spread_bps | |
| feat_21 | FLOAT | quote_half_life_ms | |
| feat_22 | FLOAT | rm_state | RmState enum |
| feat_23 | FLOAT | rm_consec_loss | |
| feat_24 | FLOAT | rm_bankroll | |
| feat_25 | FLOAT | rm_exposure_pct | |
| feat_26 | FLOAT | signal_confidence | |
| feat_27 | FLOAT | ci_lower | edge CI 下界 |
| feat_28 | FLOAT | ci_upper | |
| feat_29 | FLOAT | N_pretrade | |
| feat_30 | FLOAT | N_inplay | |
| feat_31 | FLOAT | N_settled | |
| **TrainingLabel (结算后回填, 非决策时刻字段)** | | | |
| label_event_ts | INT64 (ns) | TrainingLabel.event_ts | 比赛真实结束 ts |
| label_data_source_ts | INT64 (ns) | TrainingLabel.data_source_ts | Polymarket settle event ts |
| label_ingestion_ts | INT64 (ns) | TrainingLabel.ingestion_ts | |
| label_as_of_ts | INT64 (ns) | TrainingLabel.as_of_ts | label 写入 ts |
| decision_taken | BOOL | TrainingLabel.decision_taken | signal triggered |
| executed | BOOL | TrainingLabel.executed | RM approved + filled |
| filled_price | DOUBLE | TrainingLabel.filled_price | |
| filled_size_usdc | DOUBLE | TrainingLabel.filled_size_usdc | |
| settlement_outcome | UINT8 | TrainingLabel.settlement_outcome | 0=Pending 1=Win 2=Loss 3=Push 4=Void |
| realized_pnl_usdc | DOUBLE | TrainingLabel.realized_pnl_usdc | 含手续费净 PnL |

缺失语义: NaN (float) / NULL (其他类型)。LightGBM 原生 NaN sparse 支持 (ML-R5)。

### 3.4 Point-In-Time Join 规则 (防 look-ahead)

以下规则对 feature_row 构建强制:

```
对 feature_row 的每一行 (as_of_ts = T):
  1. game_snapshot 行: 选 data_source_ts <= T 的最新行 per match_id
  2. orderbook_snapshot 行: 选 data_source_ts <= T 的最新行 per market_id
  3. TrainingLabel: 结算后异步回填, label_as_of_ts > T (结算事件必在决策后)
  4. 严禁: 将 T+delta 的 game_snapshot 关联到 T 的 feature_row
```

DuckDB PIT join SQL (回测引擎/ML 训练用):

```sql
-- PIT join: game_snapshot + orderbook_snapshot @ as_of_ts = T
-- 用于回测/ML 训练数据生成 (不用于实盘, 实盘走 in-process feature 计算)
SELECT
    f.as_of_ts,
    f.market_id,
    f.sport,
    f.market_type,
    g.score_home_total,
    g.score_away_total,
    g.time_status,
    g.elapsed_sec,
    g.bm_yes_odds_bet365,
    g.bm_no_odds_bet365,
    b.mid,
    b.spread_bps,
    b.top3_depth_usdc,
    b.imbalance
FROM feature_row f
ASOF JOIN game_snapshot g
    ON f.sport = g.sport
    AND f.market_type = g.market_type
    AND g.data_source_ts <= f.as_of_ts
    -- match_id join 由 market_id -> match_id 映射表提供 (小李/小冯 mapping table)
ASOF JOIN orderbook_snapshot b
    ON f.market_id = b.market_id
    AND b.data_source_ts <= f.as_of_ts
WHERE f.event_date BETWEEN '2024-11-01' AND '2024-11-30'
  AND f.sport = 'Soccer'
  AND f.market_type = 'Moneyline'
ORDER BY f.as_of_ts;
```

注: DuckDB `ASOF JOIN` 内置 PIT 语义 (latest row where key <= T), 无需手写 window function。

---

## 4. 数据流时间线

```
Goalserve adapter (小段) →  game_snapshot rows   ─┐
                                                   ├→ feature_row (PIT join, DuckDB/C++ offline)
orderbook adapter (小冯) →  orderbook_snapshot rows ─┘
                                                   │
                                                   ↓
                              feature_store/sport=X/event_date=Y/market_type=Z/*.parquet
                                                   │
                            ┌──────────────────────┴──────────────────┐
                            ↓                                         ↓
                   backtest engine (小蒋)                    ML 训练 (小邓 LightGBM)
                   (C++, 同一 Parquet, R-7)                (Python notebook, 离线)

paper engine →  (实时 in-process feature 计算, 不走 Parquet 热路径)
              →  WAL → 离线 Parquet 写入 (ParquetBatchWriter, 小田 DWH 消费)
```

---

## 5. Adapter 消费契约 (给小段/小冯对齐)

### 5.1 小段 Goalserve adapter → game_snapshot

小段需提供的归一化输出结构 (feature_store_contract.hpp FeatureStoreGameRow):

```
输入:  GameRecord (include/stcpp/data/goalserve_record.hpp)
       + MultiBookOddsRecord (include/stcpp/data/odds_record.hpp) [可选, 同 match]
输出:  FeatureStoreGameRow (include/stcpp/data/feature_store_contract.hpp)
```

**字段约束**:
- `match_id`: 去掉 vendor 前缀后的纯 ID (不含 "gs_" 前缀)
- `sport`: 用 GoalserveSport enum 的 string name ("Soccer" / "Basketball" / ...)
- `market_type`: adapter 推断, 支持 "Moneyline" / "Totals" / "Spreads"
- `event_date`: 从 scheduled_ts_ns (优先) 或 event_ts_ns 降精度到 DATE32 (days since epoch)
- `bm_*` 列: 若本次 poll 无 OddsRecord 则全填 NaN/false; 不阻塞 game_snapshot 写入
- R-20: as_of_ts = 生成 FeatureStoreGameRow 时刻的 ingestion_ts_ns (不得用未来 ts)

**PIT 约束 (关键)**:
- adapter 每次调用 `produce_game_row()` 时, 传入的 GameRecord.ts.as_of_ts_ns 必须 <= now()
- 禁止回填历史: 不得以今天拿到的 OddsRecord 回填昨天的 game_snapshot.as_of_ts

### 5.2 小冯 orderbook adapter → orderbook_snapshot

小冯需提供的归一化输出结构 (feature_store_contract.hpp FeatureStoreBookRow):

```
输入:  OrderBookSnapshot (include/stcpp/microstructure/orderbook.hpp)
       + L1Probe (由 compute_l1_probe 计算, 小袁提供函数)
输出:  FeatureStoreBookRow (include/stcpp/data/feature_store_contract.hpp)
```

**字段约束**:
- `market_id`: Polymarket condition_id 或 asset_id, 与 Polymarket CLOB 保持一致
- `token_side`: "YES" 或 "NO" (大写, 与 orderbook_snapshot 列名一致)
- `sport` / `event_date` / `market_type`: 从外部 market_metadata 表注入 (小冯不自行推断)
- R-20: data_source_ts 来自 WSS payload timestamp 字段 (ms 转 ns), 禁本地 now() 替代
- `bid_p0..ask_s4`: NaN 表示该 level 无深度 (空 level, 不是 0)

**PIT 约束**:
- WSS 推送触发一行; 不得合并不同时刻的推送进一行
- as_of_ts = evaluate 时刻 (在 WSS event loop 外的 strategy 层填写, 非 ingestion 时刻)

### 5.3 接口版本锁

```cpp
// include/stcpp/data/feature_store_contract.hpp
// kFeatureStoreSchemaVersion = "fs-schema-v1.0"
// 变更: ADR + @小段 + @小冯 + @小邓 ack 后 bump version
```

---

## 6. DuckDB 查询基准 (4 场景)

### Q1 — 每日比赛比分 + bookmaker 覆盖率

```sql
SELECT
    sport, event_date,
    COUNT(DISTINCT match_id) AS n_matches,
    AVG(score_home_total + score_away_total) AS avg_total_score,
    AVG(CAST(bm_valid_bet365 AS INT) + CAST(bm_valid_williamhill AS INT)
        + CAST(bm_valid_unibet AS INT)) AS avg_bm_coverage_top3
FROM 'data/feature_store/sport=Soccer/event_date=*/market_type=Moneyline/*.parquet'
WHERE event_date BETWEEN '2024-10-01' AND '2024-10-31'
GROUP BY sport, event_date
ORDER BY event_date;
```

### Q2 — PIT join: 每场比赛决策点的 book + game 对齐验证

```sql
-- 验证 feature_row 中 as_of_ts 确实 >= data_source_ts (PIT 正确性检查)
SELECT
    COUNT(*) AS total_rows,
    SUM(CASE WHEN as_of_ts < data_source_ts THEN 1 ELSE 0 END) AS pit_violations,
    SUM(CASE WHEN as_of_ts < event_ts THEN 1 ELSE 0 END) AS event_order_violations
FROM 'data/feature_store/sport=*/event_date=*/market_type=*/*.parquet'
WHERE event_date >= '2024-11-01';
-- 期望: pit_violations = 0, event_order_violations = 0
```

### Q3 — ML 训练数据导出 (32 feature + label, sport=Soccer Moneyline)

```sql
SELECT
    feature_snapshot_id,
    as_of_ts,
    sport,
    event_date,
    feat_00, feat_01, feat_02, feat_03,
    feat_04, feat_05, feat_06, feat_07,
    feat_08, feat_09, feat_10, feat_11,
    feat_12, feat_13, feat_14, feat_15,
    feat_16, feat_17, feat_18, feat_19,
    feat_20, feat_21, feat_22, feat_23,
    feat_24, feat_25, feat_26, feat_27,
    feat_28, feat_29, feat_30, feat_31,
    settlement_outcome,
    realized_pnl_usdc
FROM 'data/feature_store/sport=Soccer/event_date=*/market_type=Moneyline/*.parquet'
WHERE settlement_outcome != 0
  AND event_date BETWEEN '2024-01-01' AND '2024-12-31'
ORDER BY as_of_ts;
```

### Q4 — orderbook spread 分布 + liquidity 分析 (小袁微观结构)

```sql
SELECT
    sport,
    PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY spread_bps) AS median_spread_bps,
    PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY spread_bps) AS p95_spread_bps,
    AVG(top3_depth_usdc) AS avg_depth_usdc,
    COUNT(*) AS n_snapshots
FROM 'data/feature_store/sport=*/event_date=*/market_type=Moneyline/*.parquet'
WHERE event_date >= '2024-10-01'
  AND time_status = 1  -- InPlay (来自 game_snapshot join 后的字段)
GROUP BY sport
ORDER BY median_spread_bps;
```

---

## 7. 性能基准 (本地 NVMe, DuckDB)

| 查询 | 数据量 | 期望耗时 |
|---|---|---|
| Q1 按日比分聚合 (单月 Soccer) | ~100K 行 | < 200 ms |
| Q2 PIT 正确性全量扫描 | 全量 | < 5 s (partition pruning) |
| Q3 ML export (年度 Moneyline) | ~500K 行 | < 3 s |
| Q4 spread 分布 (所有 sport) | ~1M 行 | < 2 s |

---

## 8. 红线 Enforce

| 红线 | 违反后果 |
|---|---|
| R-20: 4 ts 不等式 event <= data_source <= ingestion <= as_of | P0 (CLAUDE.md §8) |
| PIT: feature.as_of_ts <= 任何消费它的决策时刻 | 模型无效 / 回测假阳性 |
| 回测=实盘同源: 同一 Parquet 喂 backtest + paper | 红线 (CLAUDE.md §8) |
| data_source_ts 来自 UPSTREAM_PAYLOAD | 禁本地 now() 替代 |
| schema 静默变更 | P0, 通知小段/小冯/小邓/小蒋 |
| kBookmakerIds ABI 顺序 | 变更需 ADR + 老彭 ack |
| kFeatureStoreSchemaVersion bump | 变更通知所有下游 |

---

## 9. 未解决问题 (待下游 ack)

| 编号 | 问题 | 待 ack 方 | 截止 |
|---|---|---|---|
| FS-01 | market_id -> match_id 映射表: 由谁维护? 小冯 adapter 还是小段 adapter 还是独立表? | 小冯 + 小段 | Sprint-1 末 |
| FS-02 | orderbook_snapshot 的 sport/event_date/market_type 注入: 小冯从哪里拿 market_metadata? | 小冯 + 老李 | Sprint-1 末 |
| FS-03 | game_snapshot 和 orderbook_snapshot 的 PIT join 在 C++ 层还是 DuckDB 层? (实盘走 in-process, 回测走 DuckDB) | 小蒋 (回测) + 小梁 (策略) | Sprint-2 初 |
| FS-04 | bookmaker 第 9 家 (老彭 W6 EOW 确认) 加入后, bm_* 列增加需 schema bump | 老彭 | W6 EOW |
