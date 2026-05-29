# 数据质量校验框架 v1 — 看板 /metrics 真值兜底

- **Owner:** 小董 (data-stats, D 数据基础设施部)
- **last_review:** 2026-05-29
- **派单来源:** 小余 (D 主管)
- **Reviewers (会签):** 小余 (D 主管, 数据供给) / 小梁 (C 主管, Sharpe/阈值主权) / 老韩 (B 主管, RM 零失效主权)
- **关联文档:**
  - `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20 4 时间戳契约)
  - `docs/RESEARCH/xiaodong-stats-validation-attestation-spec-v1.md` (D-STAT-03 GM-PAPER-G 门禁)
  - `docs/RESEARCH/xiaodong-stats-validation-framework-v1.md` (M4.5 gate evaluator)
  - `docs/RESEARCH/data-contract-v1.md` (小邓 data contract)
  - `docs/RESEARCH/xiaoyuan-microstructure-v1.md` (小袁 微观结构实测)
  - `include/stcpp/microstructure/orderbook.hpp` (OrderBookSnapshot / R-20 4ts)
  - `include/stcpp/infra/wal/pit.hpp` (PIT AssertChain)
  - `src/stcpp/debug_api/state_provider.hpp` (MetricsSnapshot D2 字段)

> **小董按:** 本文件是看板 `/metrics` + 回测/看板数据质量的统计兜底规范. 与 D-STAT-03 attestation 侧重点不同: D-STAT-03 服务 GM-PAPER-G 30 日放行门禁; 本文件服务"任何时刻流入 /metrics 看板和回测管道的数据是否干净"的日常 runtime 校验. 脏数据不进回测/看板是底线. 离线工具 Python/Jupyter, 不进生产热路径.

---

## §1 框架范围

### 1.1 覆盖的三层数据

| 层 | 数据流 | 典型脏数据风险 |
|---|---|---|
| **L1 Orderbook** | Polymarket CLOB WSS → `OrderBookSnapshot` | crossed book / size 异常 / microprice 越界 |
| **L2 时间戳链** | 全部数据源 4 ts (R-20) | PIT 穿越未来 / 回填历史违规 |
| **L3 跨源一致性** | Polymarket mid vs Goalserve devig 隐含价 | 价差漂移 → MetricsSnapshot.price_drift_bps 失真 |

### 1.2 产出

1. **数据质量报告 (离线, 每日):** 覆盖率 / outlier 率 / PIT 违规计数
2. **D2 真值阈值:** `MetricsSnapshot` 三字段 (`max_staleness_ms`, `feed_gap_total`, `price_drift_bps`) 的统计基准
3. **脏数据阻断规则:** 哪些记录不得进入回测/看板统计计算

### 1.3 与热路径的边界

- **离线校验 (本文件):** Python / DuckDB / Jupyter, 对历史 WAL + parquet 数据运行
- **在线阻断 (已有):** `pit::AssertChain` / `validate_payload` / `compute_l1_probe` — 由工程侧在热路径 C++ 实现, 本文件不重复, 只负责统计层的离线 audit + 阈值标定

---

## §2 Orderbook 数据质量校验 (L1)

### 2.1 校验维度定义

```
维度 V1: crossed / locked book (best_ask ≤ best_bid)
维度 V2: size 异常 (size_usdc ≤ 0 或超出统计上界)
维度 V3: microprice 越界 (|microprice - mid| > 2 * tick)
维度 V4: 深度覆盖率 (有效档位数 < 3)
```

**参考代码路径:** `include/stcpp/microstructure/orderbook.hpp::compute_l1_probe` 已实现 crossed book 检测和 microprice cap; 本框架从离线角度对历史数据做统计分布扫描.

### 2.2 V1: Crossed / Locked Book

**定义:** `best_ask ≤ best_bid` 时 book 无效, 不得进入任何定价或信号计算.

**小袁实测数据 (N=264 有效双边 markets, 2026-05-28 ~03:00 UTC):**
- 264 条中有效 (best_ask > best_bid) 占比 99.6% (263/264)
- 1 条 crossed 来自 L1 量级近 0 边界 case (outright 极低流动)
- bid_yes + bid_no 总和全部 ≤ 1.0 (0 个套利 hit), 说明 Polymarket 体育无 arb 漏洞

**离线校验逻辑:**

```python
def check_crossed_book(snapshots: pd.DataFrame) -> CrossedBookResult:
    """
    snapshots 须有列: token_id, best_bid, best_ask, as_of_ts_ns, tick_size
    来源: WAL 落盘的 OrderBookSnapshot parquet 导出
    """
    crossed = snapshots[snapshots['best_ask'] <= snapshots['best_bid']]
    locked  = snapshots[snapshots['best_ask'] == snapshots['best_bid']]

    return CrossedBookResult(
        total=len(snapshots),
        crossed_count=len(crossed),
        locked_count=len(locked),
        crossed_rate=len(crossed) / max(len(snapshots), 1),
        crossed_token_ids=crossed['token_id'].unique().tolist(),
        # R-20: 记录违规时的 as_of_ts, 便于追溯
        crossed_ts_range=(crossed['as_of_ts_ns'].min(), crossed['as_of_ts_ns'].max())
        if len(crossed) > 0 else (None, None),
    )
```

**阈值 (基于实测推导):**

| 指标 | 警告阈值 | 阻断阈值 | 依据 |
|---|---|---|---|
| `crossed_rate` | > 0.005 (0.5%) | > 0.01 (1%) | 实测良好期 0.4%, 超 1% 说明 feed 异常 |
| 同一 token 连续 crossed > 30s | WARN | — | staleness 前兆 |

**样本量:** 本阈值基于 N=264 单次快照实测. 生产阶段应累计 ≥ 1000 条不同 token-day 后重新标定.

### 2.3 V2: Size 异常

**定义:** `size_usdc ≤ 0` (无效) 或 `size_usdc > 上界` (疑似数据错误).

**上界推导 (小袁实测):**

| sport | L1_ask$_p90 | depth_±2tick_ask$_p90 | 推荐异常上界 |
|---|---|---|---|
| NBA gameday | $14,512 | — | $100,000 (10× p90) |
| MLB gameday | $5,872 | — | $60,000 |
| Tennis gameday | $11,600 | — | $120,000 |
| Soccer outright | $3,779 | — | $40,000 |

**实施说明:** p90 × 10 作为保守异常上界, 旨在过滤数据解析错误 (如单位误乘 1000 产生 $10M), 不过滤极深流动的真实大单. 若后续 p90 分布变化超 2×, 需重新标定.

```python
def check_size_anomaly(snapshots: pd.DataFrame,
                       size_upper_by_sport: dict = None) -> SizeAnomalyResult:
    """
    size_upper_by_sport: {'NBA': 100000, 'MLB': 60000, 'Tennis': 120000, 'Soccer': 40000}
    默认统一上界 $200,000 (保守兜底, 适用于未分类 sport)
    """
    DEFAULT_UPPER = 200_000.0

    def sport_upper(row):
        if size_upper_by_sport:
            return size_upper_by_sport.get(row['sport'], DEFAULT_UPPER)
        return DEFAULT_UPPER

    snapshots = snapshots.copy()
    snapshots['size_upper'] = snapshots.apply(sport_upper, axis=1)

    invalid_zero  = snapshots[snapshots['best_bid_size'] <= 0]
    invalid_upper = snapshots[snapshots['best_ask_size'] > snapshots['size_upper']]

    return SizeAnomalyResult(
        total=len(snapshots),
        zero_size_count=len(invalid_zero),
        over_upper_count=len(invalid_upper),
        anomaly_rate=(len(invalid_zero) + len(invalid_upper)) / max(len(snapshots), 1),
        zero_size_ts=invalid_zero['as_of_ts_ns'].tolist(),
        over_upper_ts=invalid_upper['as_of_ts_ns'].tolist(),
    )
```

**阈值:**

| 指标 | 警告阈值 | 阻断阈值 |
|---|---|---|
| `zero_size_count` (任何 > 0) | 即 WARN | > 5 条/日 阻断当日统计 |
| `over_upper_count` | > 0.1% | > 0.5% |

### 2.4 V3: Microprice 越界

**定义:** `|microprice - mid| > 2 * tick_size`. 超过 2 tick cap 的 microprice 应已被 C++ 侧 clamp fallback 到 mid. 若离线数据仍出现越界, 说明写入路径未经过 `compute_l1_probe` 或 cap 逻辑被绕过.

**理论背景 (小袁 §2.3):**
- 正常 mainline (best_ask ∈ [0.05, 0.95]): micro-mid p90 ≤ 1349 bps (NBA). 2 tick = 200 bps, p90 远超 2 tick 是因为样本含 outright.
- **对于 mainline 盘口**: micro-mid p90 ≤ 200 bps 是合理预期; outright 单边极厚导致 p90 爆掉, 所以 outright 应单独分桶.

```python
def check_microprice_cap(snapshots: pd.DataFrame) -> MicropricecapResult:
    """
    检查写入 WAL 的 microprice 是否均已经过 ±2tick cap.
    若未 cap, 说明数据写入绕过了 compute_l1_probe.
    
    实测方法: 离线重算 raw microprice, 对比写入值.
    raw_micro = (bid_size * best_ask + ask_size * best_bid) / (bid_size + ask_size)
    """
    snap = snapshots.copy()
    snap['raw_micro'] = (
        (snap['best_bid_size'] * snap['best_ask'] + snap['best_ask_size'] * snap['best_bid'])
        / (snap['best_bid_size'] + snap['best_ask_size'] + 1e-12)
    )
    snap['mid'] = (snap['best_bid'] + snap['best_ask']) / 2.0
    snap['cap'] = 2.0 * snap['tick_size']
    snap['raw_delta'] = snap['raw_micro'] - snap['mid']
    snap['should_be_capped'] = snap['raw_delta'].abs() > snap['cap']
    snap['stored_delta'] = snap['microprice'] - snap['mid']

    # 违规: 应该 cap 但存储值超出 cap (说明写入未 cap)
    violations = snap[
        snap['should_be_capped'] &
        (snap['stored_delta'].abs() > snap['cap'] + 1e-9)
    ]

    return MicropricecapResult(
        total=len(snap),
        should_be_capped_count=int(snap['should_be_capped'].sum()),
        bypass_count=len(violations),
        bypass_rate=len(violations) / max(len(snap), 1),
        bypass_token_ids=violations['token_id'].unique().tolist(),
    )
```

**阈值:**

| 指标 | 警告阈值 | 阻断阈值 | 含义 |
|---|---|---|---|
| `bypass_rate` | > 0 | > 0.001 | 任何 bypass 都是 C++ 路径异常, 须立即查 |
| `should_be_capped_count / total` | 参考值 | — | 小袁实测 outright 中 cap 触发率约 15-20%, 正常范围 |

**统计说明:** 若 `bypass_count = 0`, 说明写入路径均经过 `compute_l1_probe` cap. `should_be_capped_count / total` 的分布本身是数据特征, 不触发阻断.

---

## §3 PIT 时间戳校验 (L2)

### 3.1 R-20 四时间戳不等式

**红线定义 (ADR R-20):**

```
event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ≤ now()
```

对应 C++ 实现: `include/stcpp/infra/wal/pit.hpp::AssertChain` / `DiagnoseViolation`.

### 3.2 离线 PIT 扫描

**目标:** 扫描 WAL 历史记录, 抓出回填/补录导致的时间戳倒置行 (历史违规行). 这类违规热路径不一定能拦截 (如历史 ETL 回填), 必须离线扫.

```python
from enum import IntEnum

class PitViolationType(IntEnum):
    OK                    = 0
    EVENT_TS_ZERO         = 1   # event_ts <= 0
    DS_BEFORE_EVENT       = 2   # data_source_ts < event_ts
    INGESTION_BEFORE_DS   = 3   # ingestion_ts < data_source_ts
    AS_OF_BEFORE_INGESTION= 4   # as_of_ts < ingestion_ts
    AS_OF_IN_FUTURE       = 5   # as_of_ts > now() + FUTURE_GRACE_NS

FUTURE_GRACE_NS = 60 * 1_000_000_000  # 60s 容忍 (网络时钟偏移)

def classify_pit_violation(row: dict, now_ns: int) -> PitViolationType:
    e  = row['event_ts_ns']
    ds = row['data_source_ts_ns']
    ig = row['ingestion_ts_ns']
    ao = row['as_of_ts_ns']

    if e <= 0:                    return PitViolationType.EVENT_TS_ZERO
    if ds < e:                    return PitViolationType.DS_BEFORE_EVENT
    if ig < ds:                   return PitViolationType.INGESTION_BEFORE_DS
    if ao < ig:                   return PitViolationType.AS_OF_BEFORE_INGESTION
    if ao > now_ns + FUTURE_GRACE_NS: return PitViolationType.AS_OF_IN_FUTURE
    return PitViolationType.OK

def scan_pit_violations(records: pd.DataFrame,
                        now_ns: int = None) -> PitScanResult:
    """
    records: 含 event_ts_ns, data_source_ts_ns, ingestion_ts_ns, as_of_ts_ns 列
    来源: 任何 WAL parquet 导出 (audit_record / orderbook_snapshot / goalserve_record)
    """
    import time
    if now_ns is None:
        now_ns = int(time.time() * 1e9)

    records = records.copy()
    records['pit_violation'] = records.apply(
        lambda r: classify_pit_violation(r.to_dict(), now_ns), axis=1
    )

    violations = records[records['pit_violation'] != PitViolationType.OK]

    # 违规类型分布
    vtype_counts = violations['pit_violation'].value_counts().to_dict()

    # 违规量级: ds_before_event 中 data_source_ts - event_ts 的分布
    ds_event_gaps = violations[
        violations['pit_violation'] == PitViolationType.DS_BEFORE_EVENT
    ].apply(lambda r: r['data_source_ts_ns'] - r['event_ts_ns'], axis=1)

    return PitScanResult(
        total_records=len(records),
        violation_count=len(violations),
        violation_rate=len(violations) / max(len(records), 1),
        violation_by_type=vtype_counts,
        ds_before_event_count=int((violations['pit_violation']
                                    == PitViolationType.DS_BEFORE_EVENT).sum()),
        # 统计回填深度: 负值 = data_source_ts 早于 event_ts 多少纳秒
        ds_event_gap_p50_ns=float(ds_event_gaps.median()) if len(ds_event_gaps) > 0 else 0.0,
        ds_event_gap_p99_ns=float(ds_event_gaps.quantile(0.99)) if len(ds_event_gaps) > 0 else 0.0,
        future_as_of_count=int((violations['pit_violation']
                                  == PitViolationType.AS_OF_IN_FUTURE).sum()),
        violation_record_ids=violations.index.tolist()[:200],  # 最多报 200 条, 防 log 爆炸
    )
```

### 3.3 PIT 违规计数阈值

**注:** 以下阈值基于统计理论推导, 当前无生产实测数据. 首月实盘跑通后须用真实基线重新标定.

| 违规类型 | 合理基线 | 警告阈值 | 阻断阈值 (拦截进回测) | 说明 |
|---|---|---|---|---|
| `EVENT_TS_ZERO` | 0 | > 0 | > 0.01% | 上游 payload 无 ts → R-20 违规 |
| `DS_BEFORE_EVENT` | 0 | > 0.01% | > 0.1% | ETL 回填/时钟偏移 |
| `INGESTION_BEFORE_DS` | 0 | > 0.01% | > 0.05% | 跨洋链路时钟漂移 (参见 p50 量级) |
| `AS_OF_BEFORE_INGESTION` | 0 | > 0 | > 0.01% | 系统内处理乱序, 严重 |
| `AS_OF_IN_FUTURE` | 0 | > 0 | > 0 | 数据穿越未来, P0 |
| 综合 `violation_rate` | < 0.05% | > 0.1% | > 0.5% | 离线兜底 |

**Goalserve 特殊说明:** Goalserve REST 无 WSS push ts, `data_source_ts` 退化为 HTTP Date header (`DataSourceTsOrigin::PayloadLastUpdate`). 这类记录的 `DS_BEFORE_EVENT` 误报率较高 (HTTP Date 精度仅秒级且偏迟), 扫描时须分源过滤:

```python
# 过滤 Goalserve IngestionFallback 记录时放宽 DS-Event 容忍 (30s)
GOALSERVE_DS_EVENT_GRACE_NS = 30 * 1_000_000_000

def adjusted_pit_check(row: dict, now_ns: int) -> PitViolationType:
    if row.get('ds_origin') == 'IngestionFallback':
        # 对 Fallback 记录只检查 zero + future
        if row['event_ts_ns'] <= 0: return PitViolationType.EVENT_TS_ZERO
        if row['as_of_ts_ns'] > now_ns + FUTURE_GRACE_NS: return PitViolationType.AS_OF_IN_FUTURE
        return PitViolationType.OK
    return classify_pit_violation(row, now_ns)
```

### 3.4 回填历史违规行追踪

**回填场景:** ETL 历史补录 (如 Goalserve 补齐节假日空缺) 往往让 `ingestion_ts` = 补录时间, 但 `event_ts` = 历史比赛时间. 这会产生 `ingestion_ts >> event_ts` 但不违反不等式, 然而 `as_of_ts - event_ts` 延迟极大, 特征已无效.

**追踪指标:**

```python
def backfill_detection(records: pd.DataFrame,
                       backfill_lag_threshold_hours: float = 6.0) -> BackfillResult:
    """
    backfill 判定: ingestion_ts - event_ts > threshold
    (正常跨洋最大 10-30s; >6h 基本可断定是历史回填)
    """
    threshold_ns = int(backfill_lag_threshold_hours * 3600 * 1e9)
    records = records.copy()
    records['ingest_event_lag_ns'] = records['ingestion_ts_ns'] - records['event_ts_ns']
    backfilled = records[records['ingest_event_lag_ns'] > threshold_ns]

    return BackfillResult(
        total=len(records),
        backfill_count=len(backfilled),
        backfill_rate=len(backfilled) / max(len(records), 1),
        backfill_lag_p50_h=float(backfilled['ingest_event_lag_ns'].median() / 3600e9)
        if len(backfilled) > 0 else 0.0,
        backfill_lag_max_h=float(backfilled['ingest_event_lag_ns'].max() / 3600e9)
        if len(backfilled) > 0 else 0.0,
    )
```

**处理规则:** 回填记录在回测中须标记 `is_backfilled=True` 并排除出特征计算 (PIT 核心原则: 回测时刻不可见未来补录的数据).

---

## §4 跨源一致性校验 (L3)

### 4.1 Polymarket mid vs Goalserve devig 隐含价

**背景:** P0-01 信号核心逻辑: `edge = |PM_mid - p_yes_fair_avg|`, 阈值 5¢ (EDGE_THRESHOLD). `price_drift_bps` 是 MetricsSnapshot 中反映两源价格偏差的实时指标. 需要从统计上标定:
1. 正常交叉漂移分布 (给 `price_drift_bps` 设合理告警阈值)
2. 脏数据导致的虚假漂移 (crossed book / stale feed / Goalserve 断流)

**漂移定义:**

```
drift_bps = (PM_mid - GS_devig_fair) × 10000
```

其中:
- `PM_mid = (best_bid + best_ask) / 2` (Polymarket orderbook 快照)
- `GS_devig_fair = p_yes_fair_avg` (Goalserve ≥3 家 bookmaker multiplicative de-vig 均值, ADR-008)

### 4.2 漂移分布参数推导

**数据来源约束:** 当前 (2026-05-29) 尚无生产 Goalserve 实时 feed 和 Polymarket WSS 历史对齐数据. 以下参数基于:
1. 小袁微观结构实测 (264 markets, N=264 snapshots)
2. 文献参考: Betfair/Pinnacle vs 预测市场 (prediction market) 价差分布 (Snowberg & Wolfers 2010, favorite-longshot bias)
3. ADR-008 multiplicative de-vig 中间值 (`overround_avg` 分布)
4. **需要首批真实数据验证 (见 §4.5)**

**先验分布建模:**

设 `drift_bps ~ Mixture(Normal(μ_clean, σ_clean), tail_process)`:

- **干净数据 (无 stale / 无 feed 断流):**
  - 假设 `μ_clean ≈ 0` (有效市场, 两源收敛)
  - `σ_clean`: 由 bookmaker overround spread 决定. 小袁实测等效 vig 中位 1¢ (100 bps), 跨洋传输延迟 p50 2s / p95 7s (小段 v3 §2.3 实测). 若 Goalserve 延迟 7s, PM price 在此期间移动约 0.5-1 tick (50-100 bps), 故 `σ_clean` 估计 ≈ 200 bps
  - **先验 95th percentile: |drift_bps| < 500 bps** (2.5σ ≈ 500 bps)

- **脏数据 (stale Goalserve / crossed PM book):**
  - 价差可扩大 10×-100×, drift 可达 3000-8000 bps

**先验汇总 (须首批数据标定后替换):**

| 分位 | 先验 |drift_bps| | 基于 |
|---|---|---|
| p50 | < 100 bps | overround ≈ 1¢ |
| p90 | < 400 bps | 跨洋 p95 延迟 7s × price_move |
| p95 | < 600 bps | 2.5σ |
| p99 | < 1500 bps | tail events (inplay 进球) |

### 4.3 price_drift_bps 告警阈值推导

**MetricsSnapshot 字段 (src/stcpp/debug_api/state_provider.hpp):**

```cpp
struct MetricsSnapshot {
    double price_drift_bps{0.0};  // Price drift vs reference basis points
    // ...
};
```

**阈值体系:**

| 阈值级别 | |drift_bps| 范围 | 行动 | 统计依据 |
|---|---|---|---|---|
| **GREEN** (正常) | < 500 bps | 无告警 | 先验 p95 < 600, 取 500 留 buffer |
| **YELLOW** (警告) | 500 - 1500 bps | 检查 Goalserve 是否 stale | 先验 p99 ≈ 1500 |
| **RED** (异常) | 1500 - 5000 bps | 暂停依赖 GS devig 的信号 | 5× overround, 极异常 |
| **BLACK** (P0) | > 5000 bps | 两源 feed 严重错乱 → 老韩介入 | 超出所有合理先验 |

**重要声明:** 以上阈值基于先验推导 + 实测延迟数据, **非实盘标定结果**. 首批 paper trading 运行 2 周后须:
1. 累计 ≥ 500 条 (PM_mid, GS_devig) 对齐快照
2. 用实测 `drift_bps` 序列重新估计 `(μ, σ, p95, p99)`
3. 若实测 p95 与先验偏差 > 2×, 更新阈值并走 ADR 通知下游

**用 Bayesian 更新先验的方法 (小样本期):**

```python
def update_drift_threshold(prior_p95: float,
                           observed_drifts: np.ndarray,
                           prior_weight: float = 0.5) -> dict:
    """
    prior_weight: 先验权重 (N < 200 时取 0.5; N > 500 时取 0.1)
    """
    n = len(observed_drifts)
    if n < 10:
        return {'p95': prior_p95, 'source': 'prior_only', 'n': n}

    obs_p95 = float(np.percentile(np.abs(observed_drifts), 95))

    # 加权融合 (先验 + 实测)
    w_prior = prior_weight * 200 / (prior_weight * 200 + n)  # 等效先验样本 200
    w_obs = 1 - w_prior
    posterior_p95 = w_prior * prior_p95 + w_obs * obs_p95

    return {
        'p95': posterior_p95,
        'source': 'bayesian_update',
        'n': n,
        'obs_p95': obs_p95,
        'prior_p95': prior_p95,
        'w_obs': w_obs,
        'warn_if_obs_prior_ratio_above': 2.0,
        'obs_prior_ratio': obs_p95 / max(prior_p95, 1),
    }
```

### 4.4 漂移分布离线扫描

```python
def scan_price_drift(pm_snapshots: pd.DataFrame,
                     gs_devig_snapshots: pd.DataFrame,
                     join_tolerance_ns: int = 10 * 1_000_000_000  # 10s
                     ) -> DriftScanResult:
    """
    pm_snapshots:  [market_id, as_of_ts_ns, mid_price, data_source_ts_ns, ...]
    gs_devig:      [market_id, as_of_ts_ns, p_yes_fair_avg, data_source_ts_ns, ...]

    时序对齐: 按 market_id 做 ASOF JOIN (PM 时刻取最近 GS 快照),
    仅用 |pm.as_of_ts - gs.as_of_ts| <= join_tolerance_ns 的对.
    """
    # 伪代码 - 实际用 DuckDB ASOF JOIN (性能更好)
    # SELECT p.market_id, p.as_of_ts_ns,
    #        (p.mid_price - g.p_yes_fair_avg) * 10000 AS drift_bps
    # FROM pm_snapshots p
    # ASOF JOIN gs_devig g
    #   ON p.market_id = g.market_id AND p.as_of_ts_ns >= g.as_of_ts_ns
    # WHERE ABS(p.as_of_ts_ns - g.as_of_ts_ns) <= join_tolerance_ns

    merged = asof_join(pm_snapshots, gs_devig_snapshots,
                       on='market_id', ts_col='as_of_ts_ns',
                       tolerance_ns=join_tolerance_ns)
    merged['drift_bps'] = (merged['mid_price'] - merged['p_yes_fair_avg']) * 10000

    abs_drift = merged['drift_bps'].abs()

    return DriftScanResult(
        n_pairs=len(merged),
        drift_mean_bps=float(merged['drift_bps'].mean()),
        drift_std_bps=float(merged['drift_bps'].std(ddof=1)),
        drift_p50_bps=float(abs_drift.quantile(0.50)),
        drift_p90_bps=float(abs_drift.quantile(0.90)),
        drift_p95_bps=float(abs_drift.quantile(0.95)),
        drift_p99_bps=float(abs_drift.quantile(0.99)),
        exceed_yellow_rate=float((abs_drift > 500).mean()),
        exceed_red_rate=float((abs_drift > 1500).mean()),
        exceed_black_rate=float((abs_drift > 5000).mean()),
        # 分 sport 分布
        by_sport=merged.groupby('sport')['drift_bps'].agg(['mean', 'std',
            lambda x: x.abs().quantile(0.95)]).to_dict(),
        # P0-01 信号触发边界: edge > 500 bps (5¢ = EDGE_THRESHOLD)
        pct_within_signal_threshold=float((abs_drift < 500).mean()),
    )
```

---

## §5 D2 真值阈值汇总

D2 三字段对应 `MetricsSnapshot` (生产) 和看板 `/metrics` endpoint. 以下给出离线推导的基准值:

### 5.1 max_staleness_ms

**定义:** `max((as_of_ts - data_source_ts) / 1e6)`, 跨所有活跃 token, 取最大值 (ms).

**数据来源:** 小段 v3 §2.3 Goalserve 跨洋实测 + 小袁 WSS 实测.

| 场景 | 正常期望 | 警告阈值 | 阻断阈值 |
|---|---|---|---|
| Polymarket WSS (跨洋) | < 2000 ms (p95) | > 5000 ms | > 30000 ms |
| Goalserve REST poll (跨洋) | < 7000 ms (p95) | > 15000 ms | > 60000 ms |
| **综合 max_staleness_ms** | < 8000 ms | > 15000 ms (YELLOW) | > 60000 ms (RED) |

**demo 参考值:** `demo_state_provider.hpp` 中 `max_staleness_ms = 38.0` 是 demo 占位, 不作为真实基线.

### 5.2 feed_gap_total

**定义:** WAL 中 WSS 序列号跳变计数 (per-token `gap_count` 累计). 单调递增计数器.

**阈值推导:** feed gap 为离散事件, 用泊松过程建模.

- **正常期望:** 网络断线率 < 0.1%/小时, 每天 24 小时约 0.024 次断线期望
- **生产目标 (对齐小卢 ADR-038 WSS SLO):** 每天 gap_count 增量 < 5
- **警告:** 单日增量 > 10 (异常频繁重连)
- **阻断 (影响回测):** 某 token 连续 gap_count > 3 (可能该 token 的 book state 已不可信)

| 指标 | 正常期 | 警告 | 阻断 |
|---|---|---|---|
| 日增量 `feed_gap_total` | < 5 | 5-20 | > 20 |
| 单 token 连续 gap | 0 | 1-3 | > 3 |
| gap 期间持续时间 | < 5s (reconnect) | 5-60s | > 60s |

### 5.3 price_drift_bps

见 §4.3. 汇总:

| 级别 | 范围 | 行动 |
|---|---|---|
| GREEN | < 500 bps | 正常 |
| YELLOW | 500-1500 bps | 检查 GS feed |
| RED | 1500-5000 bps | 暂停 devig 信号 |
| BLACK | > 5000 bps | P0 老韩介入 |

**标定状态:** 先验推导, 须 ≥ 500 对齐快照后重新标定.

---

## §6 综合数据质量报告格式

每日 UTC 02:30 (对齐 D-STAT-03 attestation 节奏) 产出:

```json
{
  "report_date": "2026-11-15",
  "generated_at_utc": "2026-11-15T02:32:11Z",
  "source": "xiaodong-dqv-v1",
  "window_hours": 24,

  "L1_orderbook": {
    "total_snapshots": 148234,
    "V1_crossed_book": {
      "pass": true,
      "crossed_count": 6,
      "crossed_rate": 0.0000405,
      "locked_count": 0,
      "threshold_warn": 0.005,
      "threshold_block": 0.01
    },
    "V2_size_anomaly": {
      "pass": true,
      "zero_size_count": 0,
      "over_upper_count": 2,
      "anomaly_rate": 0.0000135
    },
    "V3_microprice_cap": {
      "pass": true,
      "bypass_count": 0,
      "bypass_rate": 0.0,
      "should_be_capped_count": 18203,
      "capped_rate": 0.1228
    }
  },

  "L2_pit_timestamps": {
    "total_records": 312891,
    "violation_count": 14,
    "violation_rate": 0.0000447,
    "by_type": {
      "EVENT_TS_ZERO": 0,
      "DS_BEFORE_EVENT": 8,
      "INGESTION_BEFORE_DS": 2,
      "AS_OF_BEFORE_INGESTION": 0,
      "AS_OF_IN_FUTURE": 0
    },
    "ds_event_gap_p50_ns": -1200000000,
    "ds_event_gap_p99_ns": -4500000000,
    "backfill_count": 23,
    "backfill_rate": 0.0000735,
    "pass": true,
    "threshold_warn": 0.001,
    "threshold_block": 0.005
  },

  "L3_cross_source_drift": {
    "n_pairs": 8743,
    "drift_mean_bps": 12.4,
    "drift_std_bps": 187.3,
    "drift_p50_bps": 88,
    "drift_p90_bps": 334,
    "drift_p95_bps": 489,
    "drift_p99_bps": 1102,
    "exceed_yellow_rate": 0.038,
    "exceed_red_rate": 0.006,
    "exceed_black_rate": 0.0002,
    "calibration_status": "prior_only",
    "prior_p95_bps": 600,
    "obs_p95_bps": 489,
    "bayesian_posterior_p95_bps": 544,
    "threshold_recommendation": {
      "yellow": 500,
      "red": 1500,
      "black": 5000,
      "basis": "prior+N=8743"
    }
  },

  "D2_metrics_thresholds": {
    "max_staleness_ms": {
      "warn": 15000,
      "block": 60000,
      "calibration": "prior (小段 v3 + 小袁 WSS 实测)"
    },
    "feed_gap_total_daily_increment": {
      "warn": 10,
      "block": 20,
      "calibration": "prior (泊松建模)"
    },
    "price_drift_bps": {
      "yellow": 500,
      "red": 1500,
      "black": 5000,
      "calibration": "prior+bayesian_N8743"
    }
  },

  "overall_quality": "PASS",
  "blocking_backtest": false,
  "notes": "8 DS_BEFORE_EVENT 来自 Goalserve IngestionFallback, 在容忍范围内 (30s grace)."
}
```

---

## §7 脏数据阻断规则 (回测/看板入口门控)

脏数据记录**不得进入**回测特征计算和看板统计:

| 规则 | 触发条件 | 处理 |
|---|---|---|
| R-B1 | V1 crossed book (`best_ask <= best_bid`) | 该 snapshot 排除, 标 `dq_reject=CROSSED` |
| R-B2 | V2 zero size (`best_bid_size <= 0` OR `best_ask_size <= 0`) | 排除, 标 `dq_reject=ZERO_SIZE` |
| R-B3 | L2 `AS_OF_IN_FUTURE` PIT 违规 | 排除, 标 `dq_reject=FUTURE_TS`, emit P0 |
| R-B4 | L2 `AS_OF_BEFORE_INGESTION` | 排除, 标 `dq_reject=TS_ORDER_VIOLATED` |
| R-B5 | 回填记录 (`ingestion_ts - event_ts > 6h`) | 标 `is_backfilled=True`, 回测中屏蔽特征 |
| R-B6 | L3 drift `> 5000 bps` 的对齐对 | 该对不参与 devig edge 计算, 仅记录 |
| R-B7 | 当日 `crossed_rate > 1%` (日级) | 该日整日标 FEED_QUALITY_FAIL, 不参与统计 |
| R-B8 | 当日 `pit_violation_rate > 0.5%` | 同上 |

**NOT 阻断 (仅告警):**
- V3 microprice cap bypass: 说明写入路径问题, 但数据本身不一定错误, 告警 + 标记
- L3 drift YELLOW/RED: 触发告警, 但不阻断已有 snapshot (策略层自行判断是否使用)

---

## §8 工具实施规划

### 8.1 离线工具路径

```
tools/data_quality/
  orderbook_quality_checker.py   # §2 L1 校验 (V1/V2/V3)
  pit_scanner.py                  # §3 L2 PIT 扫描
  drift_scanner.py                # §4 L3 跨源漂移分布
  daily_dq_report.py             # §6 综合报告生成器
  threshold_calibrator.py        # §4.3 Bayesian 阈值更新
notebooks/
  dq/xiaodong-dq-calibration-<date>.ipynb  # 首批数据标定探索
```

**工具语言:** Python (`.venv` statsmodels/scipy/numpy/pandas/pyarrow/duckdb), DuckDB SQL. Jupyter notebook 用于探索, 跑完归档. 均不进生产热路径.

### 8.2 前置依赖

| 依赖 | Owner | 状态 | 本框架影响 |
|---|---|---|---|
| WAL parquet 导出接口 | 小余 (ETL) | 需确认 | L1/L2 数据来源 |
| Goalserve 实时 feed 稳定 | 小冯 | 8-15 前 | L3 漂移基线有效 |
| PM orderbook 历史 snapshot | 小冯 + 小余 | 严重 gap (data-contract C-01) | L1/L3 历史深度 |
| GS devig snapshot 落库 | 小余 + 小段 | 进行中 | L3 对齐 |
| 老唐 `AET_DATA_QUALITY_REJECT` schema | 老唐 v1.2 | 需协商 | §3 reject audit emit |

### 8.3 标定节点

| 节点 | 时间 | 行动 |
|---|---|---|
| **首批标定** | paper 运行 2 周后 (估 2026-11 中) | 累计 ≥ 500 对齐快照, 更新 L3 阈值 |
| **月度复盘** | 每月末 | 重跑 §4.4 DriftScanResult, 若 obs/prior 比值 > 2 → 提 ADR 更新阈值 |
| **季度重标** | 每季末 | 结合 (L1/L2/L3) 全历史数据重标所有阈值, 小梁 + 小余会签 |

---

## §9 样本量与统计置信度声明

本文件中所有阈值的置信度等级:

| 维度 | 当前依据 | 样本量 | 置信度 | 标定优先级 |
|---|---|---|---|---|
| V1 crossed book 阈值 | 小袁 264 markets 单次实测 | N=264 | 低 (单次快照) | 首月标定 |
| V2 size 上界 | 实测 p90 × 10 | N=264 | 低 | 首月标定 |
| V3 microprice cap bypass | 理论 (C++ 路径保证) | — | 高 (结构保证) | 监控即可 |
| L2 PIT 阈值 | R-20 红线 (零容忍) | — | 高 (业务逻辑) | 不变 |
| L3 drift 先验 | 文献 + 延迟实测推导 | N=0 (未见跨源对齐数据) | 极低 | 首批 paper 数据最高优先 |
| D2 staleness_ms | 小段 v3 跨洋实测 | 实测 p95 7s | 中 | 2 周稳定后验证 |
| D2 feed_gap | 泊松建模 | — | 低 | 首月标定 |

**致下游使用者 (小梁 / 小余 / 老韩):** 上述标记"极低"/"低"置信度的阈值在首批真实数据到位之前是工作假设. 任何依赖这些阈值做风控决策的系统应设双重保险 (先验阈值 + 人工复查). 标定完成后我会发更新版本.

---

## §10 开放问题

| # | 问题 | 决策人 | DDL |
|---|---|---|---|
| OQ-DQ1 | WAL parquet 导出格式 + 频率 (按日? 按小时?) 由谁维护 | 小余 | 协商 |
| OQ-DQ2 | L3 跨源对齐的 ASOF JOIN 容忍窗口 (10s vs 30s) 对 devig 信号的影响 | 小程 (信号) + 小梁 | 首批标定时确认 |
| OQ-DQ3 | Goalserve `IngestionFallback` 记录是否参与 L3 漂移统计 (可能引入系统性偏差) | 小段 (供给) + 小董 | 协商 |
| OQ-DQ4 | 老唐 audit schema v1.2 接 `AET_DATA_QUALITY_REJECT=32` 时间线 | 老唐 + 老郭 | 7-31 前 |
| OQ-DQ5 | L3 首批 ≥ 500 对齐快照的最早可能时间 (取决于 GS feed 落库进度) | 小余 + 小冯 | 首批 paper 后 |

---

## 附录 A: 与现有文档对齐点

| 文档 | 对齐点 |
|---|---|
| `xiaodong-stats-validation-attestation-spec-v1.md` §2 | D-STAT-03 Class-1~4 与本文 L1/L2/L3 互补; D-STAT-03 面向 GM-PAPER-G 30 日窗口, 本文面向日常 runtime |
| `data-contract-v1.md` §2.5 | 缺失值约定: `is_stale=true` 不 forward-fill; 本文 R-B1/B7 实施此约定 |
| `ADR R-20` §3 | 4 ts 强制覆盖范围; 本文 §3 实施离线 PIT 扫描 |
| `orderbook.hpp::compute_l1_probe` | V1/V3 的在线阻断已实现; 本文是离线统计层补充 |
| `p0_01_goalserve_devig.hpp` | L3 漂移计算用的 `p_yes_fair_avg` 来自此信号; 阈值 500 bps 对应信号触发条件 `EDGE_THRESHOLD=0.05` |

---

**v1 完成 — 2026-05-29, 小董.**

*本文件覆盖 orderbook L1 / PIT L2 / 跨源 L3 三维数据质量校验框架 + D2 真值阈值推导. L3 price_drift_bps 阈值为先验推导, 首批 paper 数据到位后最高优先标定. 实施工具待 ETL schema 锁 (小段 7-31) 后启动. 小余 (ETL 供给) + 小梁 (Sharpe 主权) + 老韩 (RM 零失效) 会签后生效.*
