# 回测框架 v0.1

- Owner: 小蒋 (quant-backtest)
- Date: 2026-05-28
- 验收人: 小梁 (financial-expert) + 老周 (cpp-chief-architect) + 老韩 (risk-engineer, 走 RM)
- 关联:
  - `xiaocheng-signal-catalog-v1.md` (小程 12 信号, §3 P0-01 详细实验设计)
  - `xiaoxiao-kelly-slippage-model-v1.md` (小肖 Kelly + slippage v1, §6 回测验证方案)
  - `laohan-riskmanager-design-v0.2.md` (老韩 RM v0.2)
  - `laozhou-architecture-v0.2.md` (老周架构 v0.2 §D-04 回测/实盘共用 feature pipeline)
  - `xiaosong-test-replay-framework-v0.1.md` (小宋 replay 协议 + MessagePack framed)
  - 配套同 Wave 6: `xiaojiang-paper-trading-engine-v0.1.md` (本人, paper engine)
- Wave: 6
- 状态: v0.1 RFC, 待会签

---

## 0. TL;DR (老雷 + 老周 + 老韩 + 小梁 看这段)

- **公司红线 (D-04 + GM Wave 6) 收紧重申:** 回测 / paper / 实盘 三层共用同一份 feature pipeline 代码 + 同一份 RiskManager + 同一份 signer (paper signer 替身). 任何"研究专用复刻"被视为破坏红线.
- **回测框架技术栈:** Python 离线 + DuckDB ad-hoc (v1), v2 才考虑 C++ 框架化. 数据走 Parquet (@小段 / @小余 定 schema, 我消费).
- **walk-forward + 严格 OOS:** purged k-fold + embargo, IS/OOS 切分按"时间断点 + sport 分桶"两维, 避免赛季内 leakage.
- **过拟合检测:** Bonferroni 校正 + deflated Sharpe + nested CV (避免阈值调参偷看 OOS).
- **报告生成:** Sharpe / hit rate / drawdown / α decay / CLV / slippage-vs-predicted 七图一表, 模板与 paper trading 报告完全一致 (老雷一目了然两边对比).
- **P0-01 Pinnacle no-vig 第一份回测计划:** NBA 2024-10 ~ 2025-04, IS = 2024-10 ~ 2025-02, OOS = 2025-02 ~ 2025-04. 关键阻塞是 Pinnacle 历史数据 (待小程 + 老李 OQ-2 闭). 计划 T+5 周交付报告.
- **第一份回测什么时候出:** **2026-07-02 (T+5 周)**, 卡老李 Pinnacle 数据 6/12 + 小余历史 book schema 6/19 两条前置.

---

## 1. 设计目标 + 红线

### 1.1 目标

| # | 目标 | 度量 |
|---|---|---|
| B1 | 回测结果可复现 (确定性) | 同一 fixture + 同一 code SHA + 同一 config → 同一 PnL 序列 (浮点容差 < 1e-9) |
| B2 | 防过拟合 (避免研究阶段 IS 过拟合 OOS 翻车) | OOS Sharpe / IS Sharpe ≥ 0.6 (小程 §3.6 已锁); deflated Sharpe ≥ 1.0 (我加) |
| B3 | 与实盘共用 feature pipeline (D-04) | 回测引擎调用同一份 C++ feature 库 (Python binding), 不允许 Python 重写特征 |
| B4 | 信号 ↔ 回测 解耦 | 信号通过 contract (YAML) 描述, 回测引擎不感知信号内部 |
| B5 | 报告自动化 | `python -m backtest.report --run-id <id>` 一键出 HTML + 关键 metric JSON |
| B6 | 支持 walk-forward + purged k-fold | 严格 IS/OOS 切分, embargo 区间默认 = 信号 alpha decay 周期 |
| B7 | 与 paper trading 报告同模板 | M4.5 gate 自动对账时, backtest baseline / paper actual 同一张表 |

### 1.2 红线 (不允许妥协)

| # | 红线 | 来源 |
|---|---|---|
| BR-1 | **回测 / paper / 实盘三层共用 feature pipeline** | D-04 + GM Wave 6 |
| BR-2 | **回测必跑 RiskManager** (RM 是黑盒, 不许 mock 规则) | 老韩 RM v0.2 G1 |
| BR-3 | **OOS 数据严禁参与任何阈值调参** (含 hyperparameter search) | 行业共识, 我把它写死 |
| BR-4 | **任何 Python 产物 review 后转 C++ 落地** | CLAUDE.md §1 (老郭 + 老周确认) |
| BR-5 | **回测 slippage 模型必须用小肖 v1 同一份** (不许 backtest 自造) | 小肖 §6 |
| BR-6 | **数据 schema 走小段 / 小余 owner**, 我只消费 | 班底分工 |
| BR-7 | **任何"研究专用 hack" PR → 拒** | 我自己列, 老周备书 |

### 1.3 不在本文档范围

- 信号研究本身 → 小程 / 小梁 / 老彭
- 数据 ETL / 清洗 → 小田 (data-etl) / 小余 (历史数据)
- Parquet schema 定义 → 小段 (Goalserve) / 小余 (Polymarket 历史) / 老王 (data-warehouse)
- Kelly + slippage 实现 → 小肖
- 实盘 / paper 引擎执行 → 见姊妹文档 `xiaojiang-paper-trading-engine-v0.1.md`

---

## 2. 技术栈

### 2.1 v1 离线选型 (Python)

```
Python 3.11 (.venv 已就位, scripts/activate-quant.sh)
├── polars              (主数据帧, 列存 + lazy, 优于 pandas)
├── pyarrow             (Parquet I/O)
├── duckdb              (ad-hoc SQL, 单文件嵌入)
├── numpy / scipy       (数值 + 统计检验)
├── statsmodels         (OLS / GLS / time-series tests)
├── pyo3 / cffi binding (调 C++ feature pipeline, 见 §3)
├── matplotlib + jinja2 (报告生成, HTML/PDF)
└── jupyter             (探索, 不入正式 backtest run)
```

**为什么 polars 不 pandas:**
- 列存 + lazy → ad-hoc 50M 行 Polymarket book snapshot 不爆 RAM (单机 64GB)
- I/O 直接 Parquet, 跟小余 / 小段 schema 零摩擦
- 单核性能比 pandas 快 5-20x (Wes McKinney 2024 公开 bench)

**为什么 DuckDB 而非 ClickHouse / Postgres:**
- 单文件嵌入, 无外部服务, 跨开发机零搭建成本
- 直接读 Parquet, 不必先 load
- ad-hoc 阶段够用; 量级到 TB 再考虑 ClickHouse

**为什么不用 vectorbt / backtrader 等现成框架:**
- vectorbt: 适合股票 K 线, 不适合 CLOB 离散撮合 + 多市场 portfolio
- backtrader: 老 (2015), 慢, 单线程
- 我们要的是"调 RM + 信号 contract + slippage 模型"的薄壳, 现成框架反而要切除一半再用

### 2.2 v2 (C++ 框架化, M+12 后再考虑)

- 触发条件: Python 单次回测 > 30min, 或并行参数扫描需要 multi-machine
- 选型: 自研 backtest runner (复用 paper trading 引擎的 C++ 内核, 见姊妹文档 §3)
- v2 之前不动

### 2.3 与 C++ feature pipeline 共享 (BR-1 红线落地)

**问题:** 回测要用同一份特征. C++ feature pipeline (老周 §2.2 / L2 DATA) 是实盘热路径. 怎么让 Python backtest 调用?

**方案 (与老周 / 小田会签):**

```
+----------------------------+         +--------------------------+
| Python backtest (v1)       |         | C++ live trader (实盘)   |
|                            |         |                          |
| polars df ─┐               |         | feature_pipeline.so ─┐   |
|            │               |         |                       │   |
|            ▼               |         |                       │   |
| feature_pipeline_py ───────┼────同一份│───────────────────────┘   |
|   (pyo3 binding)           |   .so   |   (linked statically)    |
|            │               |         |                          |
|            ▼               |         |                          |
| feature columns ── parquet |         |                          |
|            │               |         |                          |
|            ▼               |         |                          |
| signal contract eval       |         |                          |
|            │               |         |                          |
|            ▼               |         |                          |
| RiskManager (同份 .so)     ─┼────────┼─→ RiskManager (同份 .so) |
|            │               |         |                          |
|            ▼               |         |                          |
| paper_signer (mock chain)  |         | real_signer (链上)        |
+----------------------------+         +--------------------------+
```

- **feature_pipeline.so**: C++ 静态库, 同一份 binary, Python 通过 pyo3 binding 调用
- **RiskManager.so**: 同上, RM 在 backtest / paper / live 三层用同一个 .so
- **signer 是唯一差异点**: backtest 用 `backtest_signer` (不产生任何外部 effect, 只算 fill), paper 用 `paper_signer` (产虚拟交易号 + 模拟 fill), live 用 `real_signer` (链上)

**实施时间表 (与老周 / 小田):**
- M+2 (Sprint-2): feature_pipeline C++ 库雏形 + pyo3 binding stub
- M+4: backtest 跑通 feature 共用
- M+5: paper trading 接入 (姊妹文档)
- M+6: live 接入

**v1 (现在 ~ M+2) 过渡方案:** 我用 Python 临时复刻 P0-01 所需的 3 个最简特征 (Pinnacle no-vig fair / Polymarket mid / book depth), 但**必须在 M+2 切到 C++ 共用**, 不允许长期 Python-only. PR-level CI 强制扫:
- 新增特征实现 PR 同时改 C++ + Python binding, 否则拒.

### 2.4 与小宋 Replay 框架的关系

| 维度 | 小宋 replay (v0.1) | 小蒋 backtest (本文) |
|---|---|---|
| 主用途 | 行为复现 + 回归测试 + 事故复现 | PnL 模拟 + 参数扫描 + 信号验证 |
| 输入 | EventRecorder 录的事件流 (MessagePack framed) | Parquet 历史数据 (小余 / 小段) |
| 时间轴 | 严格事件驱动 (按 monotonic_ns) | 事件驱动 (复用小宋) 或离散批量 (ad-hoc) |
| 范围 | 全系统 (含 RM / signer / exec) | 同上, 但 signer 替身为 backtest_signer |
| 跑频 | nightly (replay smoke + full) | on-demand + 信号变更后跑 |
| Owner | 小宋 (test-replay-engineer) | 我 |

**共享:**
- ReplayDriver (小宋 §2.3) 是 backtest engine 的事件分发底座, 我**复用不重写**.
- Parquet → MessagePack frame 的 adapter 由我写 (一次性工具).
- 回测的 "Golden" 是已知 PnL baseline, 复用 GoldenLogAssertion 验回归.

---

## 3. Walk-Forward 设计

### 3.1 总体思路

走 **rolling walk-forward + purged + embargo** 三件套. 不要 simple holdout, 不要 leave-one-out (体育数据天然有 cluster).

```
时间轴:
  IS-1  | Embargo | OOS-1 | ... 滚动 ...
        IS-2  | Embargo | OOS-2 |
              IS-3  | Embargo | OOS-3 |
```

- **IS (in-sample) 窗口**: 用于阈值调参 + 模型训练 (P0-02 比分模型)
- **Embargo**: 紧邻 IS / OOS 边界的一段 (≈ 7 天), 完全弃用. 防止信号 alpha decay 周期超出当天导致 leakage
- **OOS (out-of-sample) 窗口**: 严格只读, 任何调参禁止偷看, 由我代码层 enforce (见 §3.4)

### 3.2 IS / OOS 切分 (按 sport / 信号区分)

**P0-01 (Pinnacle no-vig, pregame 6h):**
- IS: 2024-10-22 ~ 2025-02-15 (NBA 常规赛主体, ~600 场)
- Embargo: 2025-02-15 ~ 2025-02-22
- OOS: 2025-02-22 ~ 2025-04-13 (常规赛末段, ~280 场)
- 季后赛 (2025-04 ~ 06) 作为 **second OOS / regime-shift test** (小程 §3.3)
- **划分理由:** alpha decay 周期 ≈ pregame 6h, 远小于 embargo 7d, 安全; 季后赛动态 (sharp 比例升 + 流动性深) 是天然 regime shift, 用来测 robustness

**P0-02 (inplay score-price-mismatch):**
- 训练 score_model 用 IS = 2023-24 + 2024-25 NBA 常规赛 (~2400 场, 小程 §4.2)
- IS for 信号阈值: 2024-10 ~ 2025-01
- OOS: 2025-01 ~ 2025-04
- 注意: score_model 训练数据与信号 IS 不能完全重合, 否则 overfit (回归测试中 nested CV 见 §4.3)

### 3.3 Walk-forward 滚动 (M+6 后参数自适应版)

MVP (Sprint-1 至 M+6) 用一次性 IS/OOS 切分, 不滚动. M+6 后启用滚动:
- IS 窗 = 3 个月, OOS 窗 = 1 个月, step = 1 个月
- 每月月初: 用最新 3 个月 IS 重新调参, 跑下一个月 OOS, 然后投入实盘
- 滚动 PnL = 各 OOS 段拼接, **不是**重新跑 IS 的 PnL (后者会偷看)

### 3.4 防止偷看 OOS 的代码层 enforce

**机制:**
1. backtest framework 持有一个 `DataAccessGuard` 对象, 持有当前 phase (IS / OOS).
2. 信号代码读数据必经 guard: `df = guard.get_data(start, end)`.
3. 在 IS 阶段, guard.start <= request.end <= IS_END; 越界 raise `OOSLeakageError`.
4. OOS 阶段执行时, guard 锁定模型参数 (从 IS 末态读), 任何 setattr → `FrozenParamError`.
5. nested CV (§4.3) 时, guard 嵌套 (outer / inner 各一层).

```python
# backtest/guard.py 接口示意
class DataAccessGuard:
    def __init__(self, phase: Phase, start: ts, end: ts):
        self.phase = phase
        self.start, self.end = start, end
        self._frozen_params = {}

    def get_data(self, start, end) -> pl.DataFrame:
        if not (self.start <= start and end <= self.end):
            raise OOSLeakageError(f"Phase {self.phase}: requested [{start}, {end}], allowed [{self.start}, {self.end}]")
        return self._load(start, end)

    def freeze_params(self, params: dict):
        # IS 末调用, OOS 期间任何 mutate → raise
        ...
```

**CI 检查:**
- `tools/ci/oos_leakage_check.py` 用 AST 扫描所有 backtest 子模块, 任何直接读 Parquet (绕过 guard) → 拒 merge
- @老吴 帮我加到 CI pipeline

---

## 4. 过拟合检测

### 4.1 多重比较校正 (Bonferroni)

**问题:** 我们会试很多阈值 / 很多 sport / 很多窗口. 单次 p<0.05 在 N=20 实验下几乎必出 false positive.

**做法:**
- 任何阈值扫描 (e.g. P0-01 的 3¢ vs 4¢ vs 5¢) 必须报告:
  - 单次 p-value
  - Bonferroni 校正后 p-value (= p × N_experiments)
  - 校正后仍 < 0.01 才算 IS 通过

### 4.2 Deflated Sharpe Ratio (López de Prado 2014)

Sharpe ratio 在试了 N 个策略后取最大, 期望值偏高. Deflated Sharpe = E[max_Sharpe] 校正:

$$DSR = Z(\hat{SR}) - \frac{\sqrt{1 - \gamma}}{\sqrt{T - 1}} \cdot \left( \sigma_{SR} \cdot Z^{-1}(1 - 1/N) \right) \cdot \sqrt{T}$$

(具体公式 López de Prado "Deflated Sharpe" 2014, Bailey & Lopez de Prado JPM 2014.)

**实现:**
- 每个回测 run 记录: 试了多少阈值组合 (N), trade 数 (T), Sharpe 时序的 skewness γ, std σ_SR
- 输出 DSR; **MVP 上线门槛: DSR > 1.0** (与小程 §3.6 Sharpe IS ≥ 1.0 平行验收, 二者全过才放行)

### 4.3 Nested Cross-Validation

防"调阈值偷看 OOS":
- Outer loop: IS / OOS 切分 (§3.2)
- Inner loop: 在 outer-IS 内再切 train / val, 阈值搜在 inner-train 上, 阈值选定后用 inner-val 评分
- 最终 OOS 评分用 outer-OOS, 阈值在 outer-IS 末 freeze, 不再动

```python
for outer in walk_forward_splits():
    is_data, oos_data = outer.is, outer.oos
    best_params = None
    best_inner_score = -inf
    for inner in cv_splits(is_data, n_folds=5):
        for params in param_grid:
            score = evaluate(params, inner.train, inner.val)
            if score > best_inner_score:
                best_inner_score, best_params = score, params
    # freeze params, run OOS
    oos_score = evaluate(best_params, oos_data, oos_data)  # 同一份, 不再调
    report(outer, best_params, oos_score, best_inner_score)
```

### 4.4 Purged + Embargo (Marcos López de Prado)

体育数据天然有 cluster (同一场比赛多个 snapshot 高度相关). 简单 k-fold 会让验证集与训练集来自同一场, leakage.

**Purged k-fold:**
- 按 `game_id` 分组, 同一场只能进 1 个 fold
- 邻近时间 (< embargo) 的 fold 也弃, 防止赛季效应

**Embargo:**
- IS 末 → OOS 始 之间留 7 天 (默认), 见 §3.1

---

## 5. 数据 Schema 需求 (给小余 / 小段 / 老王)

### 5.1 总览

| 数据集 | Owner | 时间窗 | Parquet 路径 | 估算大小 | 优先级 |
|---|---|---|---|---|---|
| Polymarket book snapshots (5s) | 小余 | 6 个月 | `data/poly/book_snapshots/{date}/*.parquet` | ~80 GB | **P0** |
| Polymarket trades (tick) | 小余 | 6 个月 | `data/poly/trades/{date}/*.parquet` | ~15 GB | P0 |
| Pinnacle odds snapshots (30s) | 老李 + 老彭 | 6 个月 (P0-01 关键阻塞) | `data/pinn/odds/{date}/*.parquet` | ~3 GB | **P0 关键路径** |
| Goalserve livescore (event) | 小段 | 6 个月 NBA + NFL inplay | `data/goal/livescore/{sport}/{date}/*.parquet` | ~20 GB | P0 (P0-02 用) |
| Goalserve pregame + lineup | 小段 | 6 个月 | `data/goal/pregame/{date}/*.parquet` | ~2 GB | P0 |
| ESPN PBP (备用) | 小段 / 老彭手工 | 2 季 NBA | `data/espn/pbp/{date}/*.parquet` | ~5 GB | P1 (score_model 训练) |
| 538 / FTE Elo ratings | 我手工 | 静态 | `data/static/elo/elo_{season}.parquet` | <100 MB | P0 (P0-02 用) |
| 我方下单意图 (影子模式) | 小宋 + 我 | M4 起 2 周 | `data/shadow/intents/{date}/*.parquet` | ~50 MB | P0 (M4.5 gate 用) |
| 我方实际 fill 回报 | 老韩 audit + 老叶 chain | M4 起 2 周 | `data/shadow/fills/{date}/*.parquet` | ~10 MB | P0 |

### 5.2 详细 Schema (与小余 / 小段会签)

#### 5.2.1 Polymarket book snapshots

```
ts_ns                  i64          单调纳秒
recorded_at_ns         i64          wall clock (NTP) — 用于跨源对齐
market_id              str          0x... (32 字符 hex)
condition_id           str          Polymarket condition id
token_id               str          YES / NO 二元 token (其中之一)
side                   enum         BUY / SELL (bid / ask)
asks_price             list<f64>    sorted asc, 长度 N (≤ 50)
asks_size              list<f64>    USDC notional, 与 asks_price 等长
bids_price             list<f64>    sorted desc
bids_size              list<f64>
mid_price              f64          (best_bid + best_ask) / 2
spread_bps             i32
depth_24h_usdc         f64          24h 累计成交 notional
last_update_age_ms     i32          距上一次 book 更新
book_version           u64          单调递增 (检测断流)
source_lag_ms          i32          从源到本地落盘的总延迟 (老李实测)
```

**关键约束:**
- 必须 5s 频率 (最少). P0-01 需要捕获 dev >= 3¢ 触发, 30s 频率会漏触发
- `book_version` 是 gap detection 关键
- 历史与实时 schema 一致 (BR-1)

#### 5.2.2 Pinnacle odds snapshots (P0-01 关键)

```
ts_ns                  i64
recorded_at_ns         i64
sport                  enum         {NBA, NFL, MLB, NHL, soccer_*, tennis_*}
league                 str
event_id               str          Pinnacle event id
home_team              str
away_team              str
kickoff_ts             i64          比赛开赛 ts (用于 pregame 窗口判定)
yes_american_odds      f64          home 美式赔率 (或 1X2 的 1)
no_american_odds       f64          away 美式赔率 (或 1X2 的 2)
draw_american_odds     f64          (可空, soccer 三向用)
yes_decimal            f64          换算后十进制
no_decimal             f64
yes_implied_raw        f64          1 / yes_decimal
no_implied_raw         f64
overround              f64          yes_implied_raw + no_implied_raw
yes_novig_fair         f64          multiplicative 去 vig (小程 §3.2 公式)
no_novig_fair          f64
last_update_age_s      i32          距 Pinnacle 上次调价
source_lag_ms          i32
```

**关键约束:**
- 30s 频率 (Pinnacle 不动得太快, 比 PM 慢一档)
- 必须含 `last_update_age_s` 字段, 老 quote 不能用 (小程 §3.1 滤波 `<= 5min`)
- multiplicative no-vig 已在 schema 内预算 (避免回测时实时算)

#### 5.2.3 Goalserve livescore (P0-02 + P1-03)

```
ts_ns                  i64
event_id               str
sport                  enum
period                 i8           (1-4 + OT)
time_remaining_s       i16          0-2880 (NBA) / 0-3600 (NFL)
score_home             i16
score_away             i16
score_diff             i16          home - away (signed)
possession             enum         {home, away, none}
last_event_type        enum         {goal, foul, red, injury, timeout, period_end, none}
last_event_ts          i64
home_team_id           str
away_team_id           str
source_lag_ms          i32          Goalserve push 到本地落盘延迟 (S1-003 关键测量)
```

#### 5.2.4 影子 intent / fill (M4 起)

```
# shadow intents
intent_id              str          ULID
idempotency_key        str
strategy_tag           str          signal version, e.g. "P0-01@v1.2"
market_id              str
side                   enum
size_usdc              f64          intended
price                  f64          quote
edge_bps               i32          quote-based
edge_ci_low_bps        i32
signal_ts_ns           i64
data_freshness_ms      i32
book_depth_l1_usdc     f64          (小肖 §4.2 新增字段)
book_snapshot_ts_ns    i64
tick_size              f64

# shadow fills
intent_id              str          关联
audit_id               str          (RM 给)
decision               enum         APPROVED / REJECTED / DEFERRED
reject_code            enum         (老韩 §3.10 13 enum)
approved_size_usdc     f64
expected_fill_price    f64          (小肖 §4.2)
expected_fill_rate     f64
slippage_bps           i32
actual_fill_price      f64          (paper / live mode 才有)
actual_fill_size       f64
fill_ts_ns             i64
pnl_realized_usdc      f64          (closing 时填)
```

**这两张表是 M4.5 paper trading gate 的真值源** (姊妹文档).

### 5.3 与小余的 ETL 接口 (@小余 + @小田)

我不做数据清洗 (我的"拒绝任务"清单). 我消费:
- Parquet 读取走 `pyarrow.dataset` (支持 partition pruning)
- Schema 版本写入 Parquet metadata `schema_version`, 我 load 时校验
- 任何 schema 变更必须 bump 版本 + 通知 @我, 不许静默改

**P0 数据 deadline:**
- @小余: Polymarket book snapshots 6 个月 → **2026-06-19** (Sprint-1 末)
- @小余 / 老李: Pinnacle 历史数据 (路径 A/B/C 三选一) → **2026-06-12** (P0-01 死线前 2 周)
- @小段: Goalserve livescore 6 个月 → 2026-06-19
- @我: 538 Elo → 我自己, 2026-06-05

---

## 6. 回测引擎技术架构

### 6.1 模块图

```
+-----------------------------------------------------------+
|                  backtest CLI / driver                    |
|  $ python -m backtest run --config p0_01_nba.yaml         |
+-----------------------------------------------------------+
              |
              ▼
+-----------------------------------------------------------+
|              backtest.engine.Driver                       |
|  ┌────────────────────────────────────────────────────┐  |
|  │  load Parquet (pyarrow) → DataAccessGuard          │  |
|  │           ↓                                          │  |
|  │  feature_pipeline (C++ binding, BR-1)              │  |
|  │           ↓                                          │  |
|  │  signal contract eval → OrderIntent stream         │  |
|  │           ↓                                          │  |
|  │  RiskManager.evaluate (C++ binding, BR-2)          │  |
|  │           ↓                                          │  |
|  │  backtest_signer (虚拟 ack)                         │  |
|  │           ↓                                          │  |
|  │  slippage_model.estimate (小肖 v1, BR-5)            │  |
|  │           ↓                                          │  |
|  │  ledger.apply_fill → PnL accrual                   │  |
|  │           ↓                                          │  |
|  │  metrics.collect (Sharpe / DD / hit rate / etc)    │  |
|  └────────────────────────────────────────────────────┘  |
|              ↓                                              |
|     report.html + run.parquet (results)                   |
+-----------------------------------------------------------+
```

### 6.2 信号 Contract (与小程会签)

```yaml
# configs/signals/p0_01_pinnacle_novig.yaml
signal_id: SIG-P0-01
version: v1.0
name: pinnacle-novig-revert

features:
  required:
    - polymarket.mid                  # F-01
    - polymarket.book.depth_24h       # F-02
    - pinnacle.novig_fair_price       # F-05
    - pinnacle.last_update_age        # F-06
    - news.lineup_flag                # F-14
    - game.kickoff_in_minutes         # F-15

trigger:
  python_expr: |
    abs(p_pm - p_pinn_novig) >= 0.03
    and (0.5 <= kickoff_in_hours <= 6)
    and depth_24h >= 50_000
    and last_update_age_s <= 300
    and not news.lineup_flag

entry:
  size_rule: kelly_fractional(p_pinn_novig, 1 / p_pm - 1, alpha=0.25)
  size_clamp: [200, 1500]
  direction: sign(p_pinn_novig - p_pm)
  order_type: LIMIT
  limit_price: mid + dev * 0.3
  fallback: TAKER@best_ask after 60s

exit:
  take_profit:  abs(dev) < 0.01
  stop_loss:    abs(dev) > 0.06
  time_stop:    holding_hours > 6
  news_stop:    news.event_within_30s

metrics:
  target_hit_rate_is:    0.55
  target_hit_rate_oos:   0.53
  target_edge_bps_net:   150
  target_sharpe_is:      1.0
  target_sharpe_oos:     0.8
  target_max_dd_pct:     8
```

### 6.3 与 RM 的接口 (BR-2)

backtest_signer 不是 mock RM, 而是真调 RM:

```python
# pseudocode
from sports_trader_cpp.risk import RiskGateway   # pyo3 binding

rm = RiskGateway(config_path="configs/rm/backtest_config.toml")
rm.set_clock(virtual_clock)   # 注入虚拟时钟 (与小宋 VirtualClock 同源)

for ts, intent in intent_stream:
    virtual_clock.advance_to(ts)
    decision = rm.evaluate(intent)
    if decision.decision == "APPROVED":
        # 走 backtest_signer + slippage_model
        ...
    elif decision.decision == "DEFERRED":
        # 入 retry queue, 下一 tick 再试
        ...
    else:
        # 拒, 累计拒单分布
        rejected_counter[decision.reject_code] += 1
```

**关键点:**
- RM 配置 `backtest_config.toml` 与实盘 `prod_config.toml` 仅差 几个 capacity 类参数 (PER_ORDER_CAP_SOFT 可能更高用于扫参), 但**所有红线参数 (HARD cap / KELLY_FRACTION / STALE 阈值) 全部一致**
- 任何"backtest-only RM bypass" → 红线
- 拒单分布是关键 metric (§7.4), 信号阈值过松会出现高 EDGE_NEGATED_BY_SLIPPAGE 占比, 反馈给小程调阈值

### 6.4 backtest_signer

```python
class BacktestSigner:
    """
    实盘 signer 的占位. 不产生任何外部 effect.
    仅按 slippage 模型估算 fill price + fill rate, 用于 PnL 模拟.
    """
    def sign_and_submit(self, audit_id, intent, decision) -> Fill:
        slip = self.slippage_model.estimate(SlippageInput(
            order_size_usdc=decision.approved_size_usdc,
            quote_price=intent.price,
            book_depth_l1_usdc=intent.book_depth_l1_usdc,
            time_since_quote_ms=self.clock.now_ms() - intent.book_snapshot_ts_ns // 1_000_000,
            tick_size=intent.tick_size,
        ))
        # 用 Monte Carlo 抽 fill_rate (按 Bernoulli) + fill_price (按 expected)
        rng = self.rng
        actually_filled = rng.random() < slip.expected_fill_rate
        if not actually_filled:
            return Fill(audit_id=audit_id, status="UNFILLED", ...)
        return Fill(
            audit_id=audit_id,
            status="FILLED",
            fill_price=slip.expected_fill_price,
            fill_size=decision.approved_size_usdc,
            fill_ts_ns=self.clock.now_ns() + estimate_e2e_latency_ns(),
        )
```

### 6.5 PnL Accrual

```python
class Ledger:
    def apply_fill(self, fill: Fill, settle_event: SettleEvent):
        """
        在 settle 时计算 realized PnL.
        Polymarket 二元: 赢则 (1 - fill_price) * size, 输则 -fill_price * size.
        """
        ...

    def mark_to_market(self, ts) -> dict[str, float]:
        """
        持仓未平时, 用当时 mid 估值. 仅用于 drawdown / equity curve.
        """
        ...
```

---

## 7. 报告生成

### 7.1 报告模板 (与 paper trading 报告同模板, BR-7)

每次 backtest run 产 1 个 HTML + 1 个 metrics.json:

```
report/
├── 01_summary.png            # PnL curve + 累计 trade count
├── 02_sharpe.png             # rolling 30d Sharpe
├── 03_drawdown.png           # 最大回撤 + underwater plot
├── 04_hit_rate.png           # 按 sport / 时段 / size 分桶
├── 05_alpha_decay.png        # holding period vs realized edge (检测 alpha 持久度)
├── 06_slippage_diag.png      # predicted vs actual slippage (小肖 §6.2 KPI)
├── 07_clv.png                # closing line value (与 Pinnacle closing 对比)
├── 08_reject_breakdown.png   # RM 拒单分布饼图 (13 enum)
├── metrics.json              # 所有 metric 数值版, 给自动化判定用
├── trades.parquet            # 全部 trade 明细, 供 ad-hoc duckdb
└── config_snapshot.yaml      # 跑此 run 时的所有配置 (含 code SHA)
```

### 7.2 七图详解

#### 7.2.1 Sharpe 计算

- **回测 Sharpe (年化)**: `(mean_daily_pnl_pct / std_daily_pnl_pct) * sqrt(252)`
- **rolling 30d**: 滚动窗口
- **CI (bootstrap)**: 1000 次 bootstrap resample, 给 95% CI

**MVP 上线门槛 (与小程 §3.6 + 老雷 M4.5 一致):**
- IS Sharpe ≥ 1.0
- OOS Sharpe ≥ 0.8
- OOS / IS ratio ≥ 0.6 (过拟合检测)
- Deflated Sharpe ≥ 1.0 (§4.2)

#### 7.2.2 Hit Rate

- per-trade binary outcome (close PnL > 0)
- 分桶: sport / 时段 / size / 是否大场
- 期望与 small-program §3.6 信号定义一致

#### 7.2.3 Max Drawdown

- equity curve = bankroll + mark-to-market unrealized
- max DD = max over time (peak - trough) / peak
- **MVP 门槛: ≤ 8% (P0-01) / ≤ 10% (P0-02)**

#### 7.2.4 Alpha Decay

我们要看 "holding 多长 alpha 还有效":
- x 轴: 持仓时长 (分钟 / 小时)
- y 轴: 累计 realized edge (bps net of slippage)
- 期望: 单调上升 (信号方向对) → 到平台 → 平 (alpha 用完)
- 警报: 如果 holding > 2h 后 edge 反向回吐, signal 阈值太松 (反馈小程)

#### 7.2.5 Slippage Diagnostic (与小肖 §6.2 对齐)

- 散点图: x = predicted slippage_bps, y = actual slippage_bps
- 期望: y <= x (我们悲观, 实际更便宜 → OK), 偏正 (actual > predicted) → 模型乐观, **触发停盘** (小肖 §6.2 KPI)
- 用 KS 检验 + RMSE / MAE

#### 7.2.6 CLV (Closing Line Value)

行业标准 alpha 指标 (老彭 §7.5):
- CLV = (我方入场价 - Pinnacle closing line) / Pinnacle closing line
- 正 CLV 持续 → 我们 beat sharp book → 真 alpha
- MVP 门槛: avg CLV > 1.5% (实盘验证), 回测期不强制 (因 closing line 可能与入场重叠)

#### 7.2.7 Reject Breakdown

按 RM 13 enum 分布饼图:
- EDGE_CI_NEGATIVE > 30% → 信号阈值太松 (反馈小程)
- LOW_FILL_RATE > 20% → 流动性筛选不严 (反馈 P0-01 depth 阈值)
- STALE_DATA > 5% → 数据基础设施问题 (反馈小段)

### 7.3 报告自动化 CLI

```bash
# 跑 backtest
python -m backtest run \
  --config configs/signals/p0_01_pinnacle_novig.yaml \
  --data-root data/ \
  --is-start 2024-10-22 --is-end 2025-02-15 \
  --oos-start 2025-02-22 --oos-end 2025-04-13 \
  --output runs/p0_01_run_20260702/

# 出报告
python -m backtest report --run-dir runs/p0_01_run_20260702/

# 对比两个 run (e.g. backtest vs paper)
python -m backtest compare \
  --baseline runs/p0_01_run_20260702/ \
  --candidate runs/p0_01_paper_w1/ \
  --output reports/comparison.html
```

`compare` 命令是 M4.5 gate 自动判定的基础 (姊妹文档 §6).

---

## 8. P0-01 Pinnacle no-vig 第一份回测计划

### 8.1 项目总览

| 项 | 值 |
|---|---|
| 信号 | SIG-P0-01 pinnacle-novig-revert (小程 §3) |
| 数据 | NBA 2024-10-22 ~ 2025-04-13, 全季 + 季后赛 |
| IS 窗 | 2024-10-22 ~ 2025-02-15 (~600 场) |
| OOS 窗 | 2025-02-22 ~ 2025-04-13 (~280 场) |
| 二段 OOS (季后赛) | 2025-04-13 ~ 2025-06-15 (regime shift test) |
| 目标 trade 数 | IS ≥ 500, OOS ≥ 150, 二段 OOS ≥ 80 |
| 计算耗时估算 | 单次 run ~ 15min (polars 单机) |
| 参数扫描组合 | 27 (3 阈值 × 3 depth × 3 Kelly fraction), Bonferroni N=27 |

### 8.2 关键阻塞 (gating)

| # | 阻塞 | Owner | 死线 | 不闭则 |
|---|---|---|---|---|
| G1 | Pinnacle 历史数据 6 月 (路径 A/B/C 选定) | @老李 + @老彭 + @小程 OQ-2 | 2026-06-12 | P0-01 回测推迟 |
| G2 | Polymarket book snapshots 6 月 Parquet | @小余 | 2026-06-19 | 同上 |
| G3 | Goalserve pregame + lineup (news_gate 用) | @小段 | 2026-06-19 | 信号 news_stop 失效 |
| G4 | feature_pipeline C++ binding 雏形 | @老周 + @小田 | 2026-06-26 (M+2) | v1 用 Python 临时复刻 (有缺口) |
| G5 | RiskGateway pyo3 binding | @老韩 + 老周 | 2026-06-26 | 同上 |
| G6 | slippage model v1 实现 | @小肖 | 2026-06-19 | 不能算 fill |
| G7 | 信号 contract YAML (P0-01) 锁版 | @小程 + 我 | 2026-06-12 | 阈值不定 |

### 8.3 时间表 (T = 2026-05-28 today)

| 周 | 里程碑 |
|---|---|
| W1 (5/28 - 6/4) | 框架 skeleton + ETL stub + 信号 contract 草签 |
| W2 (6/5 - 6/11) | DataAccessGuard + Parquet loader + 538 Elo 入库 |
| W3 (6/12 - 6/18) | Pinnacle 数据接入 (G1 闭后); 单元测试 no-vig 公式 (小程 §3.2) |
| W4 (6/19 - 6/25) | feature 临时 Python 复刻 + Polymarket book Parquet 接入; 跑 IS dry-run |
| W5 (6/26 - 7/2) | RM binding + slippage model 接入; 跑 IS 正式 → 出 IS 报告 |
| W6 (7/3 - 7/9) | OOS 跑通; 跑 27 参数扫 + Bonferroni + DSR |
| W7 (7/10 - 7/16) | 二段 OOS (季后赛 regime shift) + 报告 v1 提交 review |

**P0-01 第一份完整回测报告: 2026-07-02 (W5 末)**, 含 IS + 第一轮 OOS, 报告 v1.

后续 (W6-W7) 含参数扫 + regime shift, 报告 v2 = 7/16.

### 8.4 失败模式 (preregistered)

事先注册可能的 fail 路径, 避免事后合理化:

| Fail mode | 触发条件 | 反馈对象 |
|---|---|---|
| IS hit rate < 55% | binary test 不显著 | 信号阈值 / 偏差去 vig 公式 → 小程 §3.2 |
| OOS hit rate < 53% | OOS 翻车 | 过拟合 / Pinnacle 数据偏差 → 小程 + 老彭 |
| Sharpe IS / OOS ratio < 0.6 | 过拟合 | 阈值搜空间过大 → 缩 grid |
| EDGE_NEGATED_BY_SLIPPAGE > 30% | 流动性太差 | 提 depth 阈值 50K → 80K |
| slippage 模型 RMSE > 50 bps | 模型偏 | 标定 κ_depth, β (小肖 §6 数据) |
| trade 数 < 200 (IS) | 阈值过严 | 降至 2.5¢ 试 (但要 Bonferroni 计入) |
| 季后赛 OOS Sharpe 比常规赛 OOS 降 > 50% | regime shift | 上线时 sport 限制 / 暂停季后赛交易 |

### 8.5 报告交付物

| 物件 | 形态 | 验收人 |
|---|---|---|
| `runs/p0_01_v1/report.html` | 七图一表 | 小梁 + 小程 |
| `runs/p0_01_v1/metrics.json` | 自动判定 JSON | M4.5 gate 脚本 (姊妹文档) |
| `runs/p0_01_v1/trades.parquet` | ad-hoc 用 | 我 + 小董 |
| 回测复盘备忘 | 1 页 md | 我写, 小梁会签 |

---

## 9. 与他人接力

### 9.1 我向上游要的

| 物件 | Owner | 死线 |
|---|---|---|
| 信号 contract YAML (P0-01, P0-02) | 小程 | 6/12 / 6/26 |
| Pinnacle no-vig 公式 + 单元测试 | 小程 | 6/12 (已有公式, 测试待写) |
| score_model (P0-02) | 小程 | 7/9 (T+6w 起跑 P0-02) |
| Polymarket book Parquet | 小余 | 6/19 |
| Pinnacle 历史 | 老李 / 老彭 | 6/12 |
| Goalserve livescore + pregame Parquet | 小段 | 6/19 |
| feature_pipeline C++ + pyo3 binding | 老周 / 小田 | 6/26 |
| RiskGateway pyo3 binding | 老韩 / 老周 | 6/26 |
| Slippage model v1 (C++) | 小肖 | 6/19 |
| ReplayDriver pyo3 (复用小宋) | 小宋 | 6/30 (用于事件流复现 v2) |

### 9.2 我向下游交付的

| 物件 | 用途 | 接收 |
|---|---|---|
| backtest framework v0.1 (本文) | 设计契约 | 全队 review |
| backtest CLI + 框架代码 (Python) | 跑回测 | 自己 + 小程 / 小董 / 老木 |
| P0-01 第一份回测报告 v1 | 阈值确认 + 信号验收 | 小梁 + 小程 + 老雷 |
| P0-02 回测报告 (T+7w) | 同上 | 同上 |
| 滚动 walk-forward 报告 | 月度 ops 报告 | 老雷 + 老韩 |
| backtest baseline (供 M4.5 gate 对比) | paper PnL 比对基准 | M4.5 gate (姊妹文档) |

### 9.3 与小宋 / 小程 / 小肖 / 老韩 周同步

- 周一站会 (与小程 + 小肖): 信号 / slippage 模型变更
- 周二同步 (与老韩): RM 接口稳定性
- 周四同步 (与小宋): replay / fixture 共享
- 任何 contract / schema 变更必须 RFC + 双签 (避免回测口径漂移)

---

## 10. 风险点 + 开放问题

### 10.1 已知风险

| # | 风险 | 缓解 |
|---|---|---|
| BR-1 (高) | Pinnacle 历史数据卡死 → P0-01 推迟 | 三路径并行 (老李 API / The Odds API 付费 / 老彭 CSV); 6/12 至少一条 work |
| BR-2 (高) | feature_pipeline C++ 没出 → backtest 用 Python 临时复刻, 与实盘不一致 | 复刻仅限 3 个最简特征, 严格 deadline M+2 切回; CI 强制 PR-level 同步改 |
| BR-3 (中) | OOS leakage (代码绕开 guard) | DataAccessGuard + CI AST 扫 |
| BR-4 (中) | 信号阈值过拟合到 NBA 常规赛 | OOS + 二段 OOS (季后赛) + Bonferroni + DSR 四重 |
| BR-5 (中) | slippage model 在历史数据上无 ground truth fill (我们没下过单) | 用 Polymarket 历史 trade tape 反推 (大单 fill 价 vs quote 价), 替代真实下单影子 |
| BR-6 (中) | Polymarket 流动性历史变化 (24 vs 25 vs 26 不同) | 报告含 quarter-by-quarter Sharpe, 检测 capacity drift |
| BR-7 (低) | Python 单机性能不够扫 27 参数 | 用 joblib 并行 + duckdb 加速, 或退到 9 参数减半扫 |
| BR-8 (低) | 报告模板与 paper 不一致 → 切换时返工 | 模板代码统一在 `backtest.report` 模块, paper 引擎 import 同份 |

### 10.2 开放问题

| # | 问题 | 找谁 | 截止 |
|---|---|---|---|
| BQ-1 | feature_pipeline pyo3 binding 性能预算 (单次 feature compute < 50ms?) | @老周 / @小田 | 6/12 |
| BQ-2 | RM backtest_config.toml 是否需要单独审批 (软参数可调高) | @老韩 | 6/12 |
| BQ-3 | slippage model 历史 fit (反推 κ_depth, β) 用什么 holdout | @小肖 | 6/19 |
| BQ-4 | 二段 OOS (季后赛) 是否进 M4.5 gate, 还是仅作 robustness 备查 | @老雷 + @小梁 | 6/26 |
| BQ-5 | walk-forward 滚动 (M+6 后) 启动节奏 | @老雷 | M+6 |
| BQ-6 | 多策略 (P0-01 + P0-02) 联合回测 portfolio 模型 (Markowitz vs 等权) | @小梁 | T+8w |
| BQ-7 | Python 临时特征复刻 review 流程 (复刻 vs C++ 同步性 CI 检测细节) | @老周 | 6/12 |

### 10.3 v0.1 不做, 留 v0.2+

- C++ backtest runner (M+12 后)
- 多账户回测
- 实时 walk-forward (streaming)
- Bayesian hyperparameter search (现在 grid 够用)
- counterfactual analysis (做 / 不做某 trade 的对比)
- option-style payoff 回测 (体育 prop 类信号未启用)

---

## 附录 A — 与小程信号 contract 的字段对齐

(已在 §6.2 给 YAML 示例; 完整 schema 见小程 §3.1 / §4.1, 我接同样字段, 加 `version` / `features.required` 元数据)

## 附录 B — 与小肖 slippage model 接口

(已在 §6.4 backtest_signer 用 SlippageInput / SlippageEstimate, 完全是小肖 §5.2 接口)

## 附录 C — 报告 metrics.json schema

```json
{
  "run_id": "p0_01_run_20260702",
  "config_sha": "abc123...",
  "code_sha": "def456...",
  "is_period": ["2024-10-22", "2025-02-15"],
  "oos_period": ["2025-02-22", "2025-04-13"],
  "trades": {
    "is_count": 587,
    "oos_count": 198,
    "is_hit_rate": 0.61,
    "oos_hit_rate": 0.56
  },
  "sharpe": {
    "is": 1.34,
    "oos": 0.92,
    "deflated_sharpe": 1.18,
    "oos_is_ratio": 0.69
  },
  "drawdown": {
    "is_max_pct": 0.052,
    "oos_max_pct": 0.071
  },
  "alpha_decay": {
    "half_life_hours": 2.1
  },
  "slippage_diag": {
    "predicted_mean_bps": 12.0,
    "actual_mean_bps": 9.5,
    "rmse_bps": 18.0,
    "bias_bps": -2.5
  },
  "clv_pct": 0.018,
  "reject_breakdown_pct": {
    "EDGE_CI_NEGATIVE": 0.08,
    "LOW_FILL_RATE": 0.04,
    "EDGE_NEGATED_BY_SLIPPAGE": 0.12,
    "...": "..."
  },
  "verdict": {
    "is_pass": true,
    "oos_pass": true,
    "dsr_pass": true,
    "overall": "PASS_FOR_PAPER_TRADING"
  }
}
```

`verdict.overall` 取值 `{PASS_FOR_PAPER_TRADING, PASS_FOR_LIVE, FAIL_NEEDS_REVIEW, FAIL_HARD}`, 是 M4.5 gate 输入.

---

**END v0.1.** 等 6/12 Pinnacle 数据 + 6/19 schema 闭, bump v0.2 + 跑 P0-01 IS dry-run.

— 小蒋 (quant-backtest), 2026-05-28
