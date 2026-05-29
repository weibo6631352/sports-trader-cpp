# Stats Validation Framework + 数据完整性 Attestation Spec v1

- **Owner:** 小董 (D 数据基础设施部, stats-inference-advisor)
- **last_review:** 2026-05-29
- **Tickets:** D-STAT-03 (stats validation framework, DDL 8-31) + D-STAT-04 (数据 attestation, DDL 10-15)
- **派单来源:** 小余 (D 主管) §8.1 GM 推进指令 v2
- **Reviewers (会签):** 小余 (D 主管, 数据供给) / 小梁 (C 主管, Sharpe 阈值主权) / 老韩 (B 主管, RM 零失效主权) / 老唐 (audit chain R-20 对齐)
- **关联文档:**
  - `docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md` §3 GM-PAPER-G 门禁
  - `docs/RESEARCH/xiaodong-stats-validation-framework-v1.md` (M4.5 gate evaluator W6)
  - `docs/RESEARCH/xiaodong-m45-gate-framework-v1.md` (7 gate C++ 实现 W6)
  - `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20 4 时间戳契约)
  - `docs/RESEARCH/laotang-audit-schema-v1.1.md` (老唐 BLAKE3 WAL schema)
  - `docs/RESEARCH/data-contract-v1.md` (小邓 data contract)

> **小董按:** 本文件是 D-STAT-03 + D-STAT-04 双 ticket 的设计 spec. GM-PAPER-G §3 明确: 无 attestation 的盈利数字 = 可能 look-ahead 假盈利, 小余签字权 + 小董联签前不放行门禁. 这份文件把"如何证明 30 日窗口数据干净"做成可机器验证 + 可人工复查的双轨规范. 统计部分在 W6 框架基础上按新阈值 (n_trades≥100, bootstrap 5000次, p<0.10) 对齐 GM-PAPER-G v2.

---

## §1 范围与关系

### 1.1 本文覆盖的两件事

| Ticket | 名称 | 截止 | 本文对应节 |
|---|---|---|---|
| **D-STAT-03** | Stats validation framework (paper runtime 数据质量校验) | 8-31 | §2 + §3 |
| **D-STAT-04** | 数据完整性 attestation (GM-PAPER-G 30 日窗口放行文件) | 10-15 | §4 + §5 |

**依赖前置:** 小段 ETL 跨源 enforce (D-ETL, 7-31) 须先锁 schema; 小冯实时 feed+reconnect (8-15). D-STAT-03 的校验规则以 ETL schema 锁为先决.

### 1.2 与 W6 M4.5 gate evaluator 的关系

W6 交付的 `xiaodong-stats-validation-framework-v1.md` + `xiaodong-m45-gate-framework-v1.md` 处理的是 **14 日软验证 gate (M4.5 前置)** 的统计判定. 本 spec 处理的是 **30 日正式窗口 GM-PAPER-G** 的更严要求:

- 阈值升级: n_trades ≥ 100 (W6 为 ≥ 50); bootstrap 5000 次 (W6 为 1000 次); p < 0.10 单侧 (W6 为 p < 0.05 双侧); Sharpe CI 下界 > 0 (W6 为 > 0.5 — 注: 30 日窗口 OOS Sharpe ≥ 0.5 是点估计要求, CI 下界 > 0 是充分性下限)
- 数据质量闸门新增: W6 框架假设数据已干净; D-STAT-03 在数据进入统计计算之前加 4 类数据质量校验层
- Attestation 新增: W6 无 attestation 文件; D-STAT-04 生成可审计的签字文件作为 GM-PAPER-G 必要附件

---

## §2 Stats Validation Framework (D-STAT-03)

### 2.1 框架目标

paper runtime 运行期间, **每日 UTC 02:00** (错峰, 跨洋结算后) 对当日入库的所有 paper trade 数据跑 4 类数据质量校验. 不通过的记录**不得进入统计计算** (即不得参与 Sharpe / PnL / t-test 计算), 并进入 reject log.

**核心原则:** 宁可丢数据降低统计功效, 也不用脏数据拉高统计显著性. 脏数据进统计 = 虚假 p-value, 与 p-hacking 等效.

### 2.2 四类数据质量校验定义

#### 2.2.1 Class-1: 缺口检验 (Gap Check)

**定义:** 检测时序中的数据断档 — 连续交易日内存在无数据的空窗口.

**校验逻辑:**

```python
# paper_trades: DataFrame with columns [trade_ts, market_id, bucket, pnl, ...]
# trading_calendar: DataFrame with columns [date, expected_active]  (从 Goalserve pregame 日程生成)

def gap_check(paper_trades: pd.DataFrame, trading_calendar: pd.DataFrame, 
              max_gap_minutes: int = 240) -> GapCheckResult:
    """
    检测两类缺口:
    1. 日级缺口: 日历上有比赛但当日 n_trades = 0 (且非节假日豁免)
    2. 盘内缺口: 同一 market_id 最后 trade_ts 与比赛结束时间差 > max_gap_minutes
    """
    # 日级缺口
    trade_dates = set(paper_trades['trade_ts'].dt.date)
    expected_dates = set(trading_calendar[trading_calendar['expected_active']]['date'])
    gap_dates = expected_dates - trade_dates  # 有比赛但零交易的日子
    
    # 盘内缺口 (intra-market): 只对 pregame Moneyline bucket
    # ... (按 market_id group, 检查 max ts vs market close time)
    
    return GapCheckResult(
        gap_dates=sorted(gap_dates),
        intra_market_gaps=[...],
        gap_rate=len(gap_dates) / max(len(expected_dates), 1),
    )
```

**拒绝阈值:** `gap_rate > 0.10` (30 日窗口超过 3 个交易日断档) → 该窗口整体质量 FAIL, 阻断 GM-PAPER-G attestation.

**豁免:** 节假日 (NBA/NFL 官方休赛期) + GM 已批准的计划维护窗口 (须提前在 `maintenance_calendar.yaml` 登记, 小冯维护).

#### 2.2.2 Class-2: 陈旧检验 (Staleness Check)

**定义:** 检测数据的 `as_of_ts` 与 `event_ts` 之间的延迟是否异常, 识别数据源 feed 中断后的"陈旧数据重放"问题.

**校验逻辑 (依赖 R-20 4 时间戳):**

```python
def staleness_check(records: pd.DataFrame, 
                    stale_threshold_s: float = 300.0,    # 5 分钟: pregame 容忍
                    inplay_stale_threshold_s: float = 30.0) -> StalenessResult:
    """
    staleness = ingestion_ts - data_source_ts  (跨洋传输延迟)
    abnormal_lag = as_of_ts - ingestion_ts     (系统内处理延迟)
    """
    records['staleness_s'] = (records['ingestion_ts'] - records['data_source_ts']) / 1e9
    records['abnormal_lag_s'] = (records['as_of_ts'] - records['ingestion_ts']) / 1e9
    
    threshold = records.apply(
        lambda r: inplay_stale_threshold_s if r['is_inplay'] else stale_threshold_s, axis=1
    )
    stale_mask = records['staleness_s'] > threshold
    abnormal_lag_mask = records['abnormal_lag_s'] > 60.0  # 系统内 >60s 视为异常
    
    stale_records = records[stale_mask | abnormal_lag_mask]
    
    return StalenessResult(
        stale_count=len(stale_records),
        stale_rate=len(stale_records) / max(len(records), 1),
        p99_staleness_s=records['staleness_s'].quantile(0.99),
        stale_trade_ids=stale_records['trade_id'].tolist(),
    )
```

**拒绝规则:** 单条 trade 的关联数据 `staleness > threshold` → 该 trade 标记 `data_quality=STALE`, 排除出统计计算. 日级统计: `stale_rate_day > 0.30` (当日 >30% 数据陈旧) → 该日整日标记 STALE, 视同日级缺口.

**为什么 30%:** 体育市场的 alpha 信号生命周期 10s-10min, 陈旧数据意味着信号已失效; >30% 的陈旧日是 feed 故障日而非正常噪声, 其 PnL 不可归因于信号质量.

#### 2.2.3 Class-3: 异常值检验 (Outlier Check)

**定义:** 检测单笔 trade PnL 是否为统计意义上的异常值. 目的是防止单笔极端盈亏主导整个 Sharpe 计算 (cherry-pick 或数据错误).

**校验逻辑:**

```python
def outlier_check(daily_pnl: pd.Series, 
                  trade_pnl: pd.Series,
                  winsor_sigma: float = 5.0) -> OutlierResult:
    """
    双层异常值检测:
    Layer 1 — 日级 PnL (用于 Sharpe 计算):
        modified_z_score = 0.6745 * (x - median) / MAD
        |modified_z_score| > 3.5 → 日级异常值 (Iglewicz & Hoaglin 1993)
    
    Layer 2 — 单笔 trade PnL:
        单笔 |pnl| > 5 * rolling_daily_sigma → outlier trade
    """
    mad = np.median(np.abs(daily_pnl - daily_pnl.median()))
    modified_z = 0.6745 * (daily_pnl - daily_pnl.median()) / (mad + 1e-9)
    day_outliers = daily_pnl[np.abs(modified_z) > 3.5]
    
    rolling_sigma = daily_pnl.std(ddof=1)
    trade_outliers = trade_pnl[np.abs(trade_pnl) > winsor_sigma * rolling_sigma]
    
    return OutlierResult(
        day_outlier_dates=day_outliers.index.tolist(),
        trade_outlier_ids=...,
        winsorized_daily_pnl=np.clip(daily_pnl, 
                                     daily_pnl.median() - winsor_sigma * mad,
                                     daily_pnl.median() + winsor_sigma * mad),
    )
```

**处理规则 (对齐 W6 §5.3 + §5.4):**
- 日级异常值: **不剔除**, 改用 winsorized 值参与 Sharpe 计算; 原始值保留进 raw Sharpe; 日报两值双报, **取 winsorized (更保守) 值参与 GM-PAPER-G 判定.**
- 单笔 trade 异常值: 标记 `data_quality=OUTLIER_TRADE`, 但**不排除出日 PnL** (该笔是否为真实 trade 由小蒋 paper engine audit 链确认); 仅在归因分析中单独列示.

**统计理由:** Winsorize 而非剔除是因为体育事件本身具有肥尾分布 (overtime / 黑马比分); 完全剔除异常日会系统性高估 Sharpe, 方向性错误.

#### 2.2.4 Class-4: 分布漂移检验 (Distribution Drift Check)

**定义:** 检测 30 日窗口内 PnL 分布是否存在结构性漂移. 漂移意味着策略 edge 来源不稳定, "持续盈利"可信度低.

**校验逻辑:**

```python
def distribution_drift_check(daily_pnl: pd.Series,
                              window_split_days: int = 15) -> DriftResult:
    """
    把 30 日窗口等分为前 15 日 / 后 15 日, 用 Kolmogorov-Smirnov 两样本检验
    + Levene 方差齐性检验 判断分布是否漂移.
    
    同时跑 Mann-Kendall trend test 检测单调趋势 (持续盈利 = 平稳, 非趋势依赖).
    """
    first_half = daily_pnl.iloc[:window_split_days]
    second_half = daily_pnl.iloc[window_split_days:]
    
    ks_stat, ks_p = scipy.stats.ks_2samp(first_half, second_half)
    levene_stat, levene_p = scipy.stats.levene(first_half, second_half)
    mk_result = pymannkendall.original_test(daily_pnl)  # pip: pymannkendall
    
    # 漂移判定: KS p < 0.05 (均值漂移) OR Levene p < 0.05 (方差漂移)
    drift_detected = (ks_p < 0.05) or (levene_p < 0.05)
    trend_significant = mk_result.p < 0.10  # Mann-Kendall 单调趋势显著
    
    return DriftResult(
        ks_p=ks_p, levene_p=levene_p,
        mk_trend=mk_result.trend, mk_p=mk_result.p,
        drift_detected=drift_detected,
        trend_significant=trend_significant,
        interpretation=_interpret_drift(drift_detected, trend_significant),
    )
```

**判定规则:**
- `drift_detected=True` AND `trend_significant=True` (单调递增): **WARN**, 盈利依赖趋势而非稳定 edge, attestation 须加注释.
- `drift_detected=True` AND `trend_significant=False` (无单调性): **FAIL**, 前后期分布不同且无规律 → 该窗口数据质量存疑, 需要 root cause 调查后才能 re-attest.
- `drift_detected=False`: **PASS**.

**为什么检漂移:** GM-PAPER-G 要求"持续盈利"而非"一次盈利". 若前 15 日亏 + 后 15 日大涨凑出累计正 PnL, 统计上是漂移, 不满足"持续"定义.

### 2.3 日质量报告格式

每日 UTC 02:30 (校验完成后) 产出, 路径: `data/quality_reports/<date>/daily_quality_report.json`

```json
{
  "report_date": "2026-11-15",
  "report_generated_at_utc": "2026-11-15T02:31:47Z",
  "window_days": 30,
  "window_start": "2026-10-16",
  "window_end": "2026-11-14",
  "checks": {
    "C1_gap": {
      "pass": true,
      "gap_dates": [],
      "gap_rate": 0.00,
      "exempt_dates": ["2026-11-28"]
    },
    "C2_staleness": {
      "pass": true,
      "stale_rate_today": 0.032,
      "stale_rate_window_avg": 0.018,
      "p99_staleness_s": 4.2,
      "stale_trade_count": 3
    },
    "C3_outlier": {
      "pass": true,
      "day_outlier_dates": [],
      "outlier_trade_count": 1,
      "winsorized_sharpe_delta": -0.04,
      "raw_sharpe": 0.68,
      "winsorized_sharpe": 0.64
    },
    "C4_drift": {
      "pass": true,
      "ks_p": 0.41,
      "levene_p": 0.28,
      "mk_trend": "no trend",
      "mk_p": 0.33,
      "drift_detected": false
    }
  },
  "overall_quality": "PASS",
  "clean_trade_count_today": 47,
  "rejected_trade_count_today": 3,
  "rejected_trade_ids": ["T-2026111423-001", "T-2026111418-004", "T-2026111407-009"],
  "cumulative_clean_trades_window": 1203,
  "cumulative_rejected_trades_window": 18,
  "rejection_rate_window": 0.0148,
  "data_quality_score": 0.985,
  "blocking_attestation": false,
  "notes": "C2 3 stale trades from Goalserve feed reconnect at 14:23 UTC; within tolerance."
}
```

**字段说明:**
- `overall_quality`: ALL 4 checks PASS → "PASS"; 任一 FAIL → "FAIL"; WARN 不阻断但须在 attestation 中注记.
- `blocking_attestation`: true = 该日数据质量问题阻断 GM-PAPER-G attestation, 需要人工复查.
- `data_quality_score`: `1 - rejection_rate_window` (粗粒度质量得分, 用于 dashboard 趋势图).

### 2.4 脏数据 Reject 进入 paper 统计计算的闸门逻辑

```
                    ┌─────────────────────────────┐
                    │    paper_trades (raw)        │
                    └──────────┬──────────────────┘
                               │
                    ┌──────────▼──────────────────┐
                    │   D-STAT-03 Quality Gate     │
                    │   (daily, UTC 02:00-02:30)   │
                    └──────────┬──────────────────┘
                               │
             ┌─────────────────┼─────────────────┐
             │                 │                 │
         C1 Gap            C2 Stale          C3 Outlier     C4 Drift
        PASS/FAIL          PASS/FAIL         PASS/WARN      PASS/FAIL/WARN
             │                 │                 │
             └────────────┬────┘                 │
                          │                      │
                   ┌──────▼──────┐               │
                   │ clean_trades│◄──────────────┘
                   │ (qualified) │   winsorized PnL 替代原始 PnL
                   └──────┬──────┘
                          │
            ┌─────────────▼─────────────────┐
            │  Statistical Computation       │
            │  (Sharpe / t-test / bootstrap) │
            └─────────────┬─────────────────┘
                          │
            ┌─────────────▼─────────────────┐
            │  GM-PAPER-G Gate Evaluator    │
            │  (30-day window judgment)      │
            └───────────────────────────────┘

门禁硬规则:
- C1 FAIL (gap_rate > 10%) → 阻断整个窗口, 不进行任何统计计算
- C2 单条 STALE → 该 trade 从 clean_trades 排除; 日级 stale_rate > 30% → 该日排除
- C3 日级 OUTLIER → winsorized PnL 替代; 不排除该日
- C4 FAIL (drift + 无趋势) → 阻断 attestation, 须 root cause 后 re-window
- C4 WARN (drift + 单调趋势) → 允许计算, attestation 加注释, 须小梁+小余双签确认
```

---

## §3 日志与监控接口

### 3.1 Metric 命名 (对齐小郑 stcpp_l5 命名空间, observability)

```
stcpp_l5_dataqual_gap_rate{window="30d"}
stcpp_l5_dataqual_stale_rate{window="30d", source="goalserve|polymarket"}
stcpp_l5_dataqual_rejection_count_today
stcpp_l5_dataqual_overall_quality{status="PASS|FAIL|WARN"}
stcpp_l5_dataqual_clean_trade_count{window="30d"}
stcpp_l5_dataqual_drift_detected{check="ks|levene|mk"}
```

推送频率: 每日 UTC 02:30 → Prometheus Pushgateway. 小郑 dashboard D4 (数据质量面板) 接收.

### 3.2 Reject 入 audit log (与老唐 audit chain 对齐)

每条 rejected trade 须 emit `AET_DATA_QUALITY_REJECT` audit event (需老唐在 schema v1.2 加此 type):

```
audit_event {
  type: AET_DATA_QUALITY_REJECT
  trade_id: "T-2026111423-001"
  reject_reason: "STALE"         // GAP | STALE | OUTLIER_DAY | DRIFT_FAIL
  staleness_s: 342.1             // Class-2 具体值
  data_source_ts: <ns>
  ingestion_ts: <ns>
  as_of_ts: <ns>                 // R-20 4 ts 全带, 老唐 WAL header 格式
  quality_check_run_id: "QCR-20261115-001"   // 对应 daily_quality_report.json
}
```

**为什么进 audit:** 数据质量拒绝是影响统计结果的决定, 必须可追溯. 若 attestation 被质疑, 可以从 audit chain 复现哪些 trades 被排除以及原因.

---

## §4 数据完整性 Attestation Spec (D-STAT-04)

### 4.1 Attestation 的作用

GM-PAPER-G §3 规定: **30 日窗口须附数据完整性 attestation (无 look-ahead, R-20 4ts 单调) 作为通过必要附件**. 小余 (D 主管) 不签字 = 门禁不通过.

Attestation 的作用是: 向老雷 (GM) + 老韩 (RM) + 小梁 (Quant) 证明以下 3 件事:
1. **数据完整性:** 30 日窗口的数据满足 §2 的 4 类质量校验 (无重大缺口/陈旧/漂移), 统计计算基于干净数据集
2. **无 look-ahead:** 每笔 paper trade 使用的特征数据在 trade 执行时点之前已经存在 (event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts 链单调, R-20 满足)
3. **样本量充分性:** n_trades ≥ 100 且 bootstrap CI 有意义 (详见 §5)

### 4.2 Attestation 文件结构

文件路径: `data/attestations/gm_paper_g_attestation_<window_end_date>.json`

```json
{
  "attestation_id": "ATT-PAPER-G-20261214",
  "attestation_type": "GM_PAPER_G_30D_WINDOW",
  "created_at_utc": "2026-12-14T08:00:00Z",
  "window": {
    "start_date": "2026-11-15",
    "end_date": "2026-12-14",
    "calendar_days": 30,
    "trading_days_included": 26,
    "trading_days_excluded_exempt": 1,
    "trading_days_excluded_quality": 0
  },
  
  "section_A_data_integrity": {
    "summary": "PASS",
    "quality_report_ids": [
      "QCR-20261115-001", "...", "QCR-20261214-001"
    ],
    "C1_gap_check": {
      "result": "PASS",
      "max_gap_rate": 0.00,
      "exempt_days": ["2026-11-28"],
      "gap_days": []
    },
    "C2_staleness_check": {
      "result": "PASS",
      "window_avg_stale_rate": 0.019,
      "p99_staleness_s": 5.1,
      "total_stale_trades_excluded": 23
    },
    "C3_outlier_check": {
      "result": "PASS",
      "day_outlier_count": 0,
      "winsorized_sharpe_delta_vs_raw": -0.06,
      "reporting_sharpe": "WINSORIZED"
    },
    "C4_drift_check": {
      "result": "PASS",
      "ks_p_value": 0.38,
      "levene_p_value": 0.51,
      "mann_kendall_trend": "no trend",
      "mann_kendall_p": 0.29,
      "notes": null
    },
    "clean_trade_count": 1247,
    "total_trade_count": 1270,
    "rejection_rate": 0.0181,
    "data_quality_score": 0.9819
  },

  "section_B_no_lookahead": {
    "summary": "PASS",
    "methodology": "R-20 4-timestamp chain monotonicity verification on all clean trades",
    "verification_details": {
      "trades_verified": 1247,
      "trades_with_r20_violation": 0,
      "violation_trade_ids": [],
      "pit_assert_rule": "event_ts <= data_source_ts <= ingestion_ts <= as_of_ts",
      "as_of_ts_vs_decision_ts_max_delta_us": 412,
      "as_of_ts_vs_decision_ts_p99_delta_us": 89
    },
    "feature_snapshot_verification": {
      "verified": true,
      "feature_snapshot_ids_audited": 1247,
      "point_in_time_violations": 0,
      "method": "cross-reference feature_snapshot_id vs ingestion_ts in data-contract-v1 DWH"
    },
    "backtest_live_data_pipeline_identity": {
      "verified": true,
      "evidence": "D-04 shared pipeline hash matches; no paper-only code path",
      "pipeline_hash": "<sha256 of shared feature pipeline binary>"
    }
  },

  "section_C_sample_adequacy": {
    "summary": "PASS",
    "n_trades": 1247,
    "n_trades_threshold": 100,
    "n_trades_pass": true,
    "n_trading_days": 26,
    "bootstrap_sharpe": {
      "n_bootstrap": 5000,
      "seed": 42,
      "point_estimate": 0.71,
      "ci_lower_95": 0.18,
      "ci_upper_95": 1.24,
      "ci_lower_positive": true,
      "window": "30d_daily"
    },
    "statistical_power": {
      "true_sharpe_null": 0.0,
      "detected_sharpe": 0.5,
      "n_days_for_power_0.8": 64,
      "actual_n_days": 26,
      "power_at_actual_n": 0.54,
      "power_note": "26 trading days insufficient for 80% power at Sharpe=0.5; compensated by n_trades>=100 + CI_lower>0 dual-gate"
    }
  },

  "section_D_gm_paper_g_conditions": {
    "net_pnl_30d": 4821.50,
    "net_pnl_positive": true,
    "daily_win_rate": 0.538,
    "daily_win_rate_pass": true,
    "max_single_day_drawdown_pct": 0.021,
    "max_single_day_drawdown_pass": true,
    "oos_sharpe_30d": 0.71,
    "oos_sharpe_pass": true,
    "t_test_pvalue_one_sided": 0.063,
    "t_test_pass": true,
    "rm_failure_events": 0,
    "rm_pass": true,
    "paper_account_contamination": false,
    "r11_pass": true,
    "pregame_moneyline_only": true,
    "inplay_excluded": true
  },

  "attestation_result": "PASS",
  "blocking_notes": null,

  "signatures": {
    "stats_advisor": {
      "name": "小董",
      "role": "D 数据基础设施部 / stats-inference-advisor",
      "signed_at_utc": "2026-12-14T08:05:00Z",
      "scope": "Section A (data integrity) + Section B (no look-ahead) + Section C (sample adequacy) + Section D statistical conditions"
    },
    "data_infra_manager": {
      "name": "小余",
      "role": "D 主管 / ETL 主权",
      "signed_at_utc": "2026-12-14T09:30:00Z",
      "scope": "Section A data pipeline integrity (ETL schema lock, reconnect, 4-ts chain)"
    }
  },

  "pending_countersignatures_for_full_gate": [
    {"name": "小梁", "role": "C 主管 / Sharpe 主权", "scope": "Section C bootstrap Sharpe + Section D OOS Sharpe"},
    {"name": "老韩", "role": "B 主管 / RM 主权", "scope": "Section D RM zero-failure + R-11 paper isolation"}
  ],

  "generated_by": "xiaodong-attestation-generator v1",
  "quality_report_sha256_chain": "<sha256 of concatenated daily QCR json files>",
  "audit_chain_ref": "老唐 WAL audit_id range: [AUD-20261115-0001 ... AUD-20261214-9876]"
}
```

### 4.3 Section B — 无 look-ahead 证明方法

**核心命题:** 每笔 paper trade 在 `as_of_ts` 时点做出决策时, 所使用的特征数据的 `ingestion_ts` 严格早于 `as_of_ts`. 即不存在用"未来数据"预测"过去结果"的穿越.

**验证方法 (三层):**

**Layer 1 — R-20 4ts 单调性逐记录核查:**

```python
def verify_r20_chain(clean_trades: pd.DataFrame) -> R20VerifyResult:
    """
    对每笔 clean trade 及其关联特征快照验证 4ts 不等式.
    数据来源: audit WAL (老唐 BLAKE3 chain) cross-joined with paper_trades
    """
    violations = []
    for _, trade in clean_trades.iterrows():
        snap = get_feature_snapshot(trade['feature_snapshot_id'])  # 从 DWH 查
        
        chain_ok = (
            snap['event_ts'] <= snap['data_source_ts'] <= 
            snap['ingestion_ts'] <= trade['as_of_ts'] <= trade['decision_ts']
        )
        if not chain_ok:
            violations.append({
                'trade_id': trade['trade_id'],
                'violation_type': classify_r20_violation(snap, trade),
                'delta_ns': snap['ingestion_ts'] - trade['as_of_ts'],  # 负值 = look-ahead
            })
    
    return R20VerifyResult(
        total_verified=len(clean_trades),
        violations=violations,
        pass_rate=1.0 - len(violations) / max(len(clean_trades), 1),
    )
```

**Layer 2 — feature_snapshot_id point-in-time 核查:**

特征快照 ID 须能在 DWH 中检索到其 `ingestion_ts`. 若 `feature_snapshot_id` 对应的特征快照的 `ingestion_ts > trade.as_of_ts` → 穿越 → attestation FAIL.

这是对齐 data-contract-v1.md §8.2 (`feature 清单 point-in-time D↔C 接口契约`) 的验证.

**Layer 3 — pipeline 同源验证:**

回测 / paper / live 三层必须使用相同特征计算代码 (CLAUDE.md 红线: "回测与实盘用不同数据处理逻辑 → 策略不允许上线"). 验证方式: 比对 `data/attestations/pipeline_hash.txt` (记录 shared feature pipeline binary 的 sha256), 确认 paper engine 与 backtest engine 引用相同 binary.

### 4.4 谁签字 + 放行链

**签字权责分工:**

| 签字人 | 角色 | 签字范围 | 不签则 |
|---|---|---|---|
| **小董** | stats-inference-advisor | 统计计算正确性 + Section A/B/C/D 统计条件 | attestation 文件无效 |
| **小余** | D 主管 | ETL pipeline 完整性 + 数据供给链 | attestation 文件无效 |
| **小梁** | C 主管 (Sharpe 主权) | bootstrap Sharpe CI 认可 + OOS Sharpe ≥ 0.5 条件 | GM-PAPER-G 不通过 |
| **老韩** | B 主管 (RM 主权) | RM 零失效 + paper R-11 隔离验证 | GM-PAPER-G 不通过 |

**放行链 (sequential):**

```
Step 1: 小董 生成 attestation 文件 + Section A/B/C 计算 → 签字
Step 2: 小余 review ETL 数据链 → 签字
Step 3: 小梁 review Sharpe 条件 → 签字 (或提异议走协商)
Step 4: 老韩 review RM zero-failure + R-11 → 签字 (或提异议走协商)
Step 5: 老雷 (GM) 收到 4 签字完整的 attestation → 拍板 GM-PAPER-G PASS/FAIL
```

**异议处理:** 任一签字人有异议 → 按 CLAUDE.md §6 协商机制 (老胡主持, 24h ack, 48h 不下升级老雷). 不允许绕过签字 "先让步再说".

---

## §5 统计显著性支撑 (对齐 GM-PAPER-G v2)

### 5.1 GM-PAPER-G v2 统计条件列表

| 条件 | GM-PAPER-G v2 要求 | 本 spec 实现 |
|---|---|---|
| 样本量 | n_trades ≥ 100 AND bootstrap Sharpe CI 下界 > 0 | §4.2 Section C |
| 统计显著 | OOS Sharpe ≥ 0.5 (30 日窗口) AND t-test p < 0.10 | §5.2 + §5.3 |
| 稳定性 | 正收益日 ≥ 52% AND 无单日亏损 > 3% (权益) | §5.4 |
| 双判定 | 末窗 + 累计 (防 50 trades 功效不足假阳性) | §5.5 |

### 5.2 Bootstrap Sharpe CI (5000 次, 30 日版)

```python
def sharpe_bootstrap_ci_30d(daily_returns: pd.Series,
                             n_boot: int = 5000,
                             annual_factor: float = np.sqrt(252),
                             seed: int = 42) -> BootstrapResult:
    """
    30 日版 bootstrap: 5000 次 (vs W6 M4.5 版 1000 次).
    理由: GM-PAPER-G 是正式门禁, 更高 bootstrap 次数降低 Monte Carlo 误差.
    5000 次下 95% CI 宽度的 MC 误差 < 0.01 SR (vs 1000 次的 ~0.02).
    """
    rng = np.random.default_rng(seed=seed)
    n = len(daily_returns)
    boot_srs = np.empty(n_boot)
    
    for i in range(n_boot):
        sample = rng.choice(daily_returns.values, size=n, replace=True)
        mu, sig = sample.mean(), sample.std(ddof=1)
        boot_srs[i] = (mu / sig) * annual_factor if sig > 1e-9 else 0.0
    
    point_sr = (daily_returns.mean() / daily_returns.std(ddof=1)) * annual_factor
    ci_lower = np.percentile(boot_srs, 2.5)
    ci_upper = np.percentile(boot_srs, 97.5)
    
    return BootstrapResult(
        point_estimate=point_sr,
        ci_lower=ci_lower,
        ci_upper=ci_upper,
        ci_lower_positive=(ci_lower > 0),  # GM-PAPER-G 要求
        n_boot=n_boot,
        seed=seed,
    )
```

**CI 下界 > 0 的统计含义:** 5000 次 bootstrap 中, 95th percentile 下界为正, 意味着真实 Sharpe 为正的置信度 ≥ 97.5%. 这比"点估计 > 0"更严格, 防止少数几日的大涨把均值拉正但实际分布大量负值的情形.

### 5.3 t-test p < 0.10 (单尾) 合理性说明

**为什么 p < 0.10 而非 p < 0.05:**

体育 paper 交易 30 日窗口的统计功效分析:

```
样本: n = 26 trading days (30 日日历天, 扣节假日)
H0: E[daily_return] = 0
H1: E[daily_return] > 0 (单尾)
假设真实 Sharpe = 0.5 (年化), 对应日 Sharpe = 0.5/sqrt(252) ≈ 0.0315
对应日均收益 ≈ 0.0315 × σ_d

Power analysis (t-test, one-sided, α=0.05):
  SE = σ_d / sqrt(26) → t_stat = μ_d / SE ≈ 0.0315 × sqrt(26) ≈ 0.16
  Power at α=0.05, n=26 ≈ 0.12   (仅 12%! 极低)

Power analysis (t-test, one-sided, α=0.10):
  critical t (df=25, one-sided) = 1.316 (vs 1.708 for α=0.05)
  Power at α=0.10, n=26 ≈ 0.18   (仍低, 但比 0.12 好 50%)
```

**结论:** 在 26 日样本下, p < 0.05 的统计功效仅约 12% (检测真实 Sharpe=0.5 的能力). 放宽到 p < 0.10 提升到约 18%, 仍然偏低. 这正是 GM-PAPER-G 设计为"双判定" (末窗 + 累计) 的原因 — 单一 t-test 功效不足, 必须配合 n_trades ≥ 100 + bootstrap CI > 0 联合判定.

**防假阳性补偿措施:** p < 0.10 单尾的 Type I error 率为 10%, 高于学术标准. 补偿:
1. Bootstrap CI 下界 > 0 (独立检验, 联合 Type I error 降低)
2. 正收益日 ≥ 52% (non-parametric, 不受正态假设影响)
3. 末窗 + 累计双判定 (防止单窗口噪声)

### 5.4 末窗 + 累计双判定 (防 50 trades 功效不足假阳性)

**问题:** 若 n_trades 刚好 = 100 (最低门槛), 30 日平均每日仅约 3.8 笔, 日级 PnL 方差极大. 单一 30 日窗口的 t-test 可能因为方差大而 p > 0.10, 即使真实存在正 edge.

**双判定规则:**

```python
def dual_window_judgment(paper_data: pd.DataFrame, 
                          window_end_date: str) -> DualWindowResult:
    """
    末窗: 最后 30 日 (标准 GM-PAPER-G 窗口)
    累计: paper runtime 启动至今全部干净 trades (从 paper_start_date 起)
    
    通过规则:
    - 末窗 PASS AND 累计 PASS → FULL PASS
    - 末窗 PASS AND 累计 WARN → CONDITIONAL PASS (小梁须额外签字确认)
    - 末窗 FAIL → FAIL (无论累计)
    - 末窗 WARN AND 累计 PASS → 升级协商 (老胡主持)
    """
    last_30d = filter_window(paper_data, days=30, end=window_end_date)
    cumulative = filter_window(paper_data, start='paper_start', end=window_end_date)
    
    end_window_result = compute_stats(last_30d)
    cumulative_result = compute_stats(cumulative)
    
    return DualWindowResult(
        end_window=end_window_result,
        cumulative=cumulative_result,
        judgment=combine_judgment(end_window_result, cumulative_result),
    )

def compute_stats(data: pd.DataFrame) -> WindowStats:
    daily_pnl = aggregate_daily(data)
    n_trades = len(data)
    bootstrap = sharpe_bootstrap_ci_30d(daily_returns=daily_pnl / bankroll)
    ttest = scipy.stats.ttest_1samp(daily_pnl / bankroll, 0, alternative='greater')
    
    return WindowStats(
        n_trades=n_trades,
        n_days=len(daily_pnl),
        net_pnl=daily_pnl.sum(),
        oos_sharpe=bootstrap.point_estimate,
        ci_lower=bootstrap.ci_lower,
        ci_lower_positive=bootstrap.ci_lower_positive,
        t_stat=ttest.statistic,
        p_value=ttest.pvalue,
        win_rate=(daily_pnl > 0).mean(),
        pass_n_trades=(n_trades >= 100),
        pass_sharpe=(bootstrap.point_estimate >= 0.5),
        pass_ci=(bootstrap.ci_lower > 0),
        pass_pvalue=(ttest.pvalue < 0.10),
        pass_winrate=((daily_pnl > 0).mean() >= 0.52),
    )
```

**为什么防 50 trades 假阳性:** W6 M4.5 gate 的 G6 门槛是 n_trades ≥ 50, 对 14 日窗口合理. GM-PAPER-G 30 日窗口用 n_trades ≥ 100 (小梁背书). 但即便 100 笔, 若集中在 5 日内完成 (其余 25 日零交易), 日级 Sharpe 计算的有效样本仍只有 5 日, 功效极低. 双窗口判定 + 末窗必须 PASS 的规则确保盈利是在整个窗口内分布的.

### 5.5 deflated Sharpe 在 30 日 OOS 窗口的适用性说明

W6 framework §6.1 的 DSR (Deflated Sharpe Ratio) 主要针对**回测阶段**的过拟合检测, 核心是惩罚多次 trial 选优 (selection bias).

**GM-PAPER-G 30 日 OOS 窗口的 DSR 适用性:**

- **适用场景:** 若 C 单元 (小梁/小程) 在 paper runtime 期间对信号参数做过任何调整 (即使一次), DSR 必须计算, `n_trials` = 调整次数 + 1.
- **不适用场景 (理想情形):** 信号参数在 paper 启动前冻结 (W6 §3.3 p-hacking 防范规则 — 冻结 `signal_id@vN`), paper 期间零调整. 此时 n_trials = 1, DSR 退化为标准 SR, 无需单独报告 DSR.
- **结论:** DSR 在 GM-PAPER-G attestation 中是**条件性**指标: 信号版本 hash 在 attestation Section D 记录; 若 hash 在 30 日内未变更 → DSR = SR; 若变更 → 须计算 DSR 并要求 DSR > 0.90.

---

## §6 与老唐 audit chain R-20 verify 对齐点

### 6.1 对齐全景

| 老唐 audit chain 要素 | 本 spec 引用点 | 对齐方式 |
|---|---|---|
| WAL header 4 ts (event_ts/data_source_ts/ingestion_ts/as_of_ts) | Section B Layer 1 R-20 核查 | 从老唐 WAL 读 4 ts, cross-join paper_trades |
| `AET_STRATEGY_DECAYED` payload 含 `monitor_snapshot_sha256` | §2.4 reject audit + 小董签字 | 小董 `bayes_decay_monitor.py` 输出 hash 进 payload (老唐 v1.1 §2.y 已约定) |
| BLAKE3 hash chain 可信性 | Section B quality_report_sha256_chain | attestation 文件记录 daily QCR json 链式 sha256 |
| `data_source_ts_source` enum (UPSTREAM_PAYLOAD / INFERRED_*) | Section A C2 staleness check | 月度 sweep (小冯) 统计 INFERRED_* 比例; 进 attestation Section A |
| `AET_DATA_QUALITY_REJECT` (新 type, 需老唐 v1.2) | §3.2 reject 入 audit log | 需在 W6 (8-31) 前与老唐对齐, 加入 schema v1.2 |
| R-20 不等式 assert: `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ≤ now_utc_ns()` | Section B Layer 1 逐记录核查 | 代码实现 `verify_r20_chain()` |
| 月度反向对账 (`AET_RECON_DRIFT`) | 可选: attestation 生成前跑 recon | 30 日窗口结束后 recon 通过 → 纳入 attestation evidence |

### 6.2 新增接口需求 (需老唐 v1.2 配合)

本 spec 要求老唐 audit schema 在 v1.2 中新增:

```
新 AET type:
  AET_DATA_QUALITY_REJECT = 32    // 数据质量拒绝 (配合 D-STAT-03)
  AET_ATTESTATION_SIGNED  = 33    // attestation 签字事件 (配合 D-STAT-04)

message DataQualityRejectPayload {
  string trade_id         = 1;
  string reject_reason    = 2;  // GAP | STALE | OUTLIER_DAY | DRIFT_FAIL
  double staleness_s      = 3;  // Class-2 时填
  string quality_check_run_id = 4;
  // 4 ts 全带 (via WAL header, R-20)
}

message AttestationSignedPayload {
  string attestation_id   = 1;
  string signer           = 2;  // "小董" | "小余" | "小梁" | "老韩"
  string scope            = 3;  // 签字覆盖的 section
  string attestation_sha256 = 4; // attestation JSON 文件 hash
}
```

@老唐: 请在 W7 (8-31 前) 确认 v1.2 可接收上述两个新 type. 需老唐 + 老韩 + 老郭联合 review (ADR-003 C-2 通道).

### 6.3 R-20 违例处理升级

若 Section B `r20_violation_count > 0` (存在 look-ahead 穿越):

```
Severity 1 (1-3 条, delta < 1s): WARN + audit emit + 30 日窗口延期 7 日
Severity 2 (>3 条 OR delta > 1s): FAIL + 立即 incident + 老雷介入
Severity 3 (delta > 60s): P0 事故 (PIT correctness 破, 回测 backtest 可疑)
```

Severity 3 触发时, 同时冻结 C 单元 (小梁) 所有使用该特征窗口回测结果, 直到 root cause 确认.

---

## §7 交付物清单 + 时间线

### D-STAT-03 (截止 8-31)

| 物件 | 路径 | 负责 | DDL |
|---|---|---|---|
| 本 spec 文件 | `docs/RESEARCH/xiaodong-stats-validation-attestation-spec-v1.md` | 小董 | 2026-05-29 (本文件) |
| 4 类校验 Python 实现 | `tools/data_quality/quality_checker.py` | 小董 | 2026-07-31 |
| 日质量报告生成器 | `tools/data_quality/daily_report_generator.py` | 小董 | 2026-07-31 |
| Cron 配置 (UTC 02:00 daily) | `deploy/cron/data_quality_cron.yaml` | 小冯 (协助) | 2026-08-15 |
| Metric 推送脚本 | `tools/data_quality/push_metrics.py` | 小董 | 2026-08-31 |
| 老唐 AET_DATA_QUALITY_REJECT schema | `docs/RESEARCH/laotang-audit-schema-v1.2.md` (老唐写) | 老唐 (小董提需求) | 2026-07-31 |

**前置依赖:** 小段 ETL schema 锁 (7-31); 小冯实时 feed 稳定 (8-15).

### D-STAT-04 (截止 10-15)

| 物件 | 路径 | 负责 | DDL |
|---|---|---|---|
| Attestation 生成器 | `tools/attestation/generate_attestation.py` | 小董 | 2026-09-30 |
| Section B R-20 核查脚本 | `tools/attestation/verify_r20_chain.py` | 小董 | 2026-09-30 |
| Section C bootstrap 5000 次 (30 日版) | `tools/attestation/bootstrap_sharpe_30d.py` | 小董 | 2026-09-15 |
| Section D 双窗口判定 | `tools/attestation/dual_window_judgment.py` | 小董 | 2026-09-30 |
| 签字流程 SOP | `docs/RESEARCH/xiaodong-attestation-signing-sop-v1.md` | 小董 | 2026-10-01 |
| 第一份正式 attestation | `data/attestations/gm_paper_g_attestation_<date>.json` | 小董 | 2026-11 (30 日窗口跑完后) |

**前置依赖:** paper runtime 启动 (老吴 Frankfurt 9-30); D-STAT-03 校验框架稳定运行 ≥ 2 周.

---

## §8 开放问题

| # | 问题 | 决策人 | DDL |
|---|---|---|---|
| OQ-1 | 老唐 audit schema v1.2 是否可接 `AET_DATA_QUALITY_REJECT=32` + `AET_ATTESTATION_SIGNED=33` | 老唐 + 老郭 | 7-31 前 |
| OQ-2 | C4 分布漂移 WARN (单调上升趋势) 时, 小梁额外签字的时间 SLA 是多少 (影响 GM-PAPER-G 放行速度) | 小梁 | 协商 |
| OQ-3 | paper runtime 启动日期 (老吴 Frankfurt 9-30), 若 delay → D-STAT-03 校验数据不足 → 10-15 D-STAT-04 交付是否需要延期 | 老胡 PM | 9-30 checkpoint |
| OQ-4 | 30 日窗口 `n_trades ≥ 100` 的 pregame Moneyline 单独分桶统计 vs 混合统计哪个先通 (若 inplay 有少量 trades 进入) | 小梁 (Sharpe 主权) | 协商 |
| OQ-5 | Section B pipeline_hash 验证: shared feature pipeline binary 的 hash 由谁维护 (小余 ETL 还是老周 系统工程) | 小余 + 老周 | 协商 |

---

**v1 完成 — 2026-05-29, 小董.**

*本文件覆盖 D-STAT-03 + D-STAT-04 双 ticket 设计 spec. 实施部分 (代码) 待 ETL schema 锁 (7-31) 后启动, 不在本 spec 范围内. 实施派给实施方, 小董仅提 spec + review.*
