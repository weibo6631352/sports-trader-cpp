# xiaotian-parquet-partition-v1.md — Parquet 分区策略 + DuckDB 起步

- owner: 小田 (dwh-analyst, #24, D 单元 IC + A 单元 cross)
- last_review: 2026-05-28
- 关联: xiaodeng-ml-data-infra-v1.md / laopeng-bookmaker-history-backfill-v1.md / xiaoyu-manager-mandate-v1.md
- spec 来源: 小余 D-W5-04 mandate + 小邓 ML hook W4 + 老彭 W6 Wave 29 历史回填 + 小冯 IngestRaw W6

---

## 1. 背景与目标

离线分析栈处理三条数据流:

| 数据流 | 来源 | 量级 | 时间线 |
|---|---|---|---|
| paper_mldata (FeatureSnapshot + TrainingLabel) | paper_mldata.wal (小邓 ML hook) | W7+ paper engine 跑起来后 | W7+ |
| 历史 bookmaker odds (MultiBookOddsRecord) | 老彭 CSV 8家 × 2年 × 8 sport | ~750 万行 raw → ~100GB | W6 EOW |
| IngestRaw 原始帧 | 小冯 IngestRaw WAL (PM_WSS + GS_*) | M2+ 生产增量 | M2+ |

估算:
- 历史 750 万行 × 8 outcome cols × ~200B/row raw ≈ 12GB raw CSV
- 加 FeatureSnapshot 32 float × 4B × 5000 条/天 × 730 天 ≈ 340MB raw
- Parquet zstd-19 压缩比 ~4:1 → 总 Parquet ~ **3-4GB (W6 scope)**
- M2+ IngestRaw blob 按需归档, 单独分区 (本 spec 不含)

---

## 2. 分区策略

### 2.1 分区键

```
sport / market_type / year / week
```

示例路径 (W6 stub / 真实小 dataset):
```
data/paper_mldata/
  sport=Soccer/
    market_type=Moneyline/
      year=2024/
        part-0001.parquet   # week 列存 Parquet 内, 不做 Hive 目录
```

示例路径 (W7+ 大 dataset, 750 万行以上):
```
data/paper_mldata/
  sport=Soccer/
    market_type=Moneyline/
      year=2024/
        week=20/
          part-0001.parquet
```

分区键选择理由:
- `sport`: 各 sport 查询独立, 8 sport 分区 pruning 直接砍 87.5% scan
- `market_type`: Moneyline / Totals / Spreads 三分, 与 DuckDB G2 query 模式吻合
- `year`: 年粒度满足 2 年历史 + rolling retention 操作
- `week` (W7+ 大 dataset): 小董 G2 Sharpe bootstrap 按周聚合; 老彭 calibration 按周触发

week 分区扩容判断: pyarrow 单次写入分区上限 1024. 8 sport × 3 market_type × 2 year × 52 week = 2496 > 1024, 超限. W6 stub 阶段 week 只作 Parquet 列, 不作 Hive 目录. W7+ 若按 year 分批写入则可用 week 分区 (8×3×1×52=1248, 仍超, 建议用 month 替代 week 或分批写).

不用 `bookmaker_id`: 已 wide format 横铺 8 列, 不需 per-bm 分区.

### 2.2 文件大小目标

| 指标 | 目标值 | 依据 |
|---|---|---|
| 单 Parquet 文件 | 128 MB ~ 256 MB | 小余 W5 mandate |
| row group size | 64 MB | Parquet 最优 IO 单元; DuckDB 并行读取粒度 |
| row group 数/文件 | 2 ~ 4 | 128MB / 64MB = 2; 256MB / 64MB = 4 |
| zstd compression level | 19 | 小余 W5 mandate; 离线写入延迟无所谓 |
| column encoding | DELTA_BINARY_PACKED (int64 ts) + PLAIN_DICTIONARY (string) | pyarrow 默认; zstd-19 再压 |

### 2.3 Retention 策略

| 层级 | 范围 | 存储 | 保留时长 |
|---|---|---|---|
| 热层 | 当前 + 前 4 周 | 本地 NVMe | 永久 (ML 训练随时取) |
| 冷层 | 4 周 ~ 2 年 | S3/对象存储 (M2+) | 2 年 |
| 归档 | > 2 年 | Glacier / 深度归档 | 5 年 (监管) |

W6 阶段全部落本地 `data/` 目录, M2+ 迁 S3 由小余迁移脚本处理.

---

## 3. Schema 定义

### 3.1 FeatureSnapshot Parquet Schema

32 feature 列按 `FeatureName` enum 顺序锁死 (ML-R5: 训练侧 column index = enum 值).

| 列名 | 类型 | 来源 | 说明 |
|---|---|---|---|
| event_ts | INT64 (ns) | FeatureSnapshot.event_ts | R-20 ts-1 |
| data_source_ts | INT64 (ns) | FeatureSnapshot.data_source_ts | R-20 ts-2 |
| ingestion_ts | INT64 (ns) | FeatureSnapshot.ingestion_ts | R-20 ts-3 |
| as_of_ts | INT64 (ns) | FeatureSnapshot.as_of_ts | R-20 ts-4 |
| feature_snapshot_id | UINT64 | FeatureSnapshot.feature_snapshot_id | ML-R8 join key |
| audit_id | BINARY(16) | FeatureSnapshot.audit_id_bytes | ULID |
| signal_id | UINT8 | FeatureSnapshot.signal_id_u8 | |
| market_id | STRING | FeatureSnapshot.market_id | |
| sport | STRING | 分区键 (写入时从 market_id 解析) | 重复存列便于 DuckDB filter |
| market_type | STRING | 分区键 (Moneyline/Totals/Spreads) | |
| year | INT32 | 分区键 (from as_of_ts) | |
| week | INT32 | 分区键 (ISO week from as_of_ts) | |
| feat_00 ~ feat_31 | FLOAT | features[0..31] | 顺序与 FeatureName enum 1:1 |

注: feat_00 = PM_mid_bid, feat_04 = Goalserve_devig_p_yes_fair, feat_05 = Goalserve_overround_avg
(ADR-008 cascade: Pinnacle_* → Goalserve_devig_* 列名已在 FeatureName enum 体现)

缺失值: NaN (LightGBM 原生 NaN sparse 支持, ML-R5)

### 3.2 TrainingLabel Parquet Schema

| 列名 | 类型 | 来源 |
|---|---|---|
| event_ts | INT64 (ns) | TrainingLabel.event_ts |
| data_source_ts | INT64 (ns) | TrainingLabel.data_source_ts |
| ingestion_ts | INT64 (ns) | TrainingLabel.ingestion_ts |
| as_of_ts | INT64 (ns) | TrainingLabel.as_of_ts |
| feature_snapshot_id | UINT64 | join key (ML-R8) |
| audit_id | BINARY(16) | |
| decision_taken | BOOL | |
| executed | BOOL | |
| filled_price | DOUBLE | VirtualFill.fill_price |
| filled_size_usdc | DOUBLE | |
| settlement_outcome | UINT8 | SettlementOutcome enum cast |
| realized_pnl_usdc | DOUBLE | |
| sport | STRING | 分区键 |
| market_type | STRING | 分区键 |
| year | INT32 | 分区键 |
| week | INT32 | 分区键 |

### 3.3 MultiBookOddsRecord (历史回填) Parquet Schema — Wide Format

Wide format: 8 列 yes_odds + 8 列 no_odds + 8 列 valid flag (vs long format 省 join)

| 列名 | 类型 | 说明 |
|---|---|---|
| event_ts | INT64 (ns) | R-20 ts-1 |
| data_source_ts | INT64 (ns) | R-20 ts-2 |
| ingestion_ts | INT64 (ns) | R-20 ts-3 |
| as_of_ts | INT64 (ns) | R-20 ts-4 |
| ds_origin | UINT8 | DataSourceTsOrigin enum |
| sport | STRING | 分区键 (GoalserveSport 名) |
| market_type | STRING | 分区键 |
| year | INT32 | 分区键 |
| week | INT32 | 分区键 |
| match_id | STRING | Goalserve match id |
| market_id | STRING | 1x2 / OU_2.5 / AH_-0.5 |
| crc32c | UINT32 | integrity |
| audit_id | BINARY(16) | ULID |
| yes_odds_10bet | DOUBLE | slots[0].odds_yes |
| no_odds_10bet | DOUBLE | slots[0].odds_no |
| valid_10bet | BOOL | slots[0].valid |
| yes_odds_williamhill | DOUBLE | slots[1].odds_yes |
| no_odds_williamhill | DOUBLE | slots[1].odds_no |
| valid_williamhill | BOOL | |
| yes_odds_bet365 | DOUBLE | slots[2].odds_yes |
| no_odds_bet365 | DOUBLE | slots[2].odds_no |
| valid_bet365 | BOOL | |
| yes_odds_marathon | DOUBLE | slots[3].odds_yes |
| no_odds_marathon | DOUBLE | slots[3].odds_no |
| valid_marathon | BOOL | |
| yes_odds_unibet | DOUBLE | slots[4].odds_yes |
| no_odds_unibet | DOUBLE | slots[4].odds_no |
| valid_unibet | BOOL | |
| yes_odds_betvictor | DOUBLE | slots[5].odds_yes |
| no_odds_betvictor | DOUBLE | slots[5].odds_no |
| valid_betvictor | BOOL | |
| yes_odds_1xbet | DOUBLE | slots[6].odds_yes |
| no_odds_1xbet | DOUBLE | slots[6].odds_no |
| valid_1xbet | BOOL | |
| yes_odds_betano | DOUBLE | slots[7].odds_yes |
| no_odds_betano | DOUBLE | slots[7].odds_no |
| valid_betano | BOOL | |

kBookmakerIds ABI 锁: 顺序与 data_contract.hpp::kBookmakerIds[0..7] 1:1.

---

## 4. 数据流时间线

```
W6 EOW:
  老彭 历史 CSV → xiaotian_parquet_export.ipynb → Parquet
  分区路径: data/paper_mldata/sport=*/market_type=*/year=202*/week=*/

W7+:
  paper engine 跑通 → paper_mldata.wal → ParquetBatchWriter (cpp stub → HC-06 真接)
  → 增量 Parquet (append new week partition)

M2+:
  小冯 IngestRaw WAL → 独立 data/ingest_raw/ Parquet (本 spec 预留, 不含实现)
```

---

## 5. DuckDB 查询测试场景 (3+)

### Q1 — 每周 G2 Sharpe bootstrap (小董 gate)

```sql
-- 周粒度 PnL 汇总, 供 G2 Sharpe bootstrap (小董 M4.5 gate §G2)
SELECT
    year,
    week,
    sport,
    COUNT(*) AS n_bets,
    AVG(realized_pnl_usdc) AS mean_pnl,
    STDDEV_SAMP(realized_pnl_usdc) AS std_pnl,
    (AVG(realized_pnl_usdc) / NULLIF(STDDEV_SAMP(realized_pnl_usdc), 0))
        * SQRT(52.0) AS sharpe_annualized
FROM 'data/paper_mldata/**/*.parquet'
WHERE market_type = 'Moneyline'
  AND settlement_outcome IN (1, 2)  -- Win=1, Loss=2
GROUP BY year, week, sport
ORDER BY year, week, sport;
```

### Q2 — 信号 P0-01 hit rate / edge per sport (老彭校准)

```sql
-- feat_06 = edge_bps (FeatureName::edge_bps index 6)
SELECT
    sport,
    market_type,
    COUNT(*) AS n,
    AVG(feat_06) AS mean_edge_bps,
    SUM(CASE WHEN settlement_outcome = 1 THEN 1 ELSE 0 END)::DOUBLE
        / COUNT(*) AS hit_rate,
    STDDEV_SAMP(feat_06) AS std_edge_bps
FROM 'data/paper_mldata/**/*.parquet'
WHERE year = 2025
  AND market_type = 'Moneyline'
  AND feat_06 > 200.0    -- edge > 200 bps 触发阈值
GROUP BY sport, market_type
ORDER BY mean_edge_bps DESC;
```

### Q3 — ML training data export (小邓 LightGBM baseline)

```sql
-- 导出 32 feature + label, 供 LightGBM offline 训练
-- (等 W7+ paper engine 真跑, W6 stub 验证 schema 可读性)
SELECT
    feature_snapshot_id,
    as_of_ts,
    sport,
    market_type,
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
FROM 'data/paper_mldata/**/*.parquet'
WHERE year = 2025
  AND market_type = 'Moneyline'
  AND settlement_outcome != 0  -- 排除 Pending
ORDER BY as_of_ts;
```

### Q4 — Bookmaker coverage per week (老彭 ADR-008 校验)

```sql
-- 检查每周有效家数 < 3 的行 (kMinValidBookmakers = 3)
SELECT
    year, week, sport, market_type,
    COUNT(*) AS n_records,
    SUM(valid_10bet::INT + valid_williamhill::INT + valid_bet365::INT
      + valid_marathon::INT + valid_unibet::INT + valid_betvictor::INT
      + valid_1xbet::INT + valid_betano::INT) / COUNT(*) AS avg_valid_bm_count
FROM 'data/paper_mldata/**/*.parquet'
GROUP BY year, week, sport, market_type
HAVING avg_valid_bm_count < 3
ORDER BY year, week;
```

---

## 6. 性能 Baseline

测量环境: MBP M-series, DuckDB 1.5.3, 本地 NVMe

| 查询 | 预期 (5000 stub records) | 预期 (750 万 records) |
|---|---|---|
| Q1 weekly Sharpe | < 50 ms | < 2 s (partition pruning) |
| Q2 hit rate per sport | < 30 ms | < 1 s |
| Q3 ML export 10k rows | < 100 ms | < 5 s (全扫 + year filter) |
| Q4 bookmaker coverage | < 50 ms | < 2 s |

DuckDB 查询优化手册 (W7+ 真数据到了再校准):
- 分区 pruning: `WHERE year=? AND sport=?` 触发 Hive partition pushdown
- row group filter: `WHERE feat_06 > 200` 触发 Parquet statistics skipping
- COPY TO / read_parquet glob: DuckDB 1.5 自动并行化 `**/*.parquet`

---

## 7. HC-06 入职 Onboarding 输入 (ml-data-engineer 8/15)

HC-06 接手后的第一个 sprint 任务:
1. 将 ParquetBatchWriter stub (include/stcpp/data/parquet_writer.hpp) 替换为真实 Apache Arrow 实现
2. 接 paper_mldata.wal WAL replay → Parquet batch write (W7+)
3. 实现增量 append (按 week partition, 不重写历史)
4. 压测: 10k records/s write throughput (zstd-19 离线写入非阻塞)
5. 与小邓对接: 确认 ML training data export schema (feat_00..feat_31 列顺序 ABI 锁)

依赖提前准备:
- Apache Arrow C++ (FetchContent 或 vcpkg, W7 确认)
- 参考 xiaotian_parquet_export.ipynb Python 版本作为 spec ground truth

---

## 8. 红线 Enforce

| 红线 | 处理方式 |
|---|---|
| R-20 4 ts | Parquet 必含 event_ts / data_source_ts / ingestion_ts / as_of_ts 4 列, 写入前校验 ts_chain_ok() |
| ML-R2 Python offline | notebook 不进 cpp 生产; ParquetBatchWriter stub 只做 ABI 占位 |
| ADR-008 cascade | feat_04 = Goalserve_devig_p_yes_fair (不再叫 Pinnacle_*) |
| R-7 paper/live 共享 | ParquetBatchWriter W7+ 真实现时 paper + live build 共享 lib |
| 数据源 schema 静默变更 | kBookmakerIds ABI 版本写入 Parquet metadata (schema_version 字段) |
