# 统计验证框架 v1

- Owner: 小董 (data-stats)
- Date: 2026-05-28
- 验收人: 小梁 (quant) + 小蒋 (backtest) + 老雷 (GM)
- 关联: xiaocheng-signal-catalog-v1.md, xiaojiang-backtest-framework-v0.1.md (in-flight), xiaojiang-paper-trading-engine-v0.1.md (in-flight), OKR M4.5
- Status: v1, 核心 deliverable 是第 5 节 "M4.5 gate 机器判定规范"
- 工具: Python `.venv` (statsmodels / scipy / numpy / pandas / pyarrow) + DuckDB + Jupyter

> 小董按: 这份框架的核心命题是 — "稳定盈利" 在统计上是一个**多维联合假设**, 不是看一个数字. M4.5 GM 红线表面上简单 (连续 2 周 Sharpe > 1.0 + PnL > 0), 但 14 天 × 每天 5-30 笔的样本量下, "看着 > 1.0" 和 "统计上显著 > 0" 是两件事. 我这份的任务: 把口径定死, 把误判率定死, 给小蒋和小郑可以直接跑 cron 的判定脚本规范.

---

## 1. Sharpe 计算标准

### 1.1 MVP 选择: 按日 (Daily Sharpe), 年化系数 sqrt(252)

**为什么不用按小时**: 体育市场 inplay 段只有 2-4h/赛事, 按小时算大量空格 (无交易), 会引入零方差日 + 严重低估 σ. 学术界 (Lo 2002) 也建议非连续交易资产用日级.

**为什么不用按周**: M4.5 只有 2 周窗口 = 14 个观察点, 按周直接退化到 n=2, 没法做检验.

**正式定义**:
```python
# daily_pnl: pandas.Series index=日期 (UTC), value=当日净 PnL (USDC), 含手续费/滑点
# bankroll: 当日起始本金 (动态), 用于 return normalize
daily_return = daily_pnl / bankroll_at_day_open    # 日收益率
mu_d  = daily_return.mean()                         # 日均收益
sig_d = daily_return.std(ddof=1)                    # 日 std (无偏)
rf_d  = 0.0                                         # 见 1.2
sharpe_annual = (mu_d - rf_d) / sig_d * np.sqrt(252)
```

### 1.2 无风险利率: 设 0 (paper 阶段) / US 1M T-bill (实盘后)

- **Paper 阶段 (M4 → M5)**: rf = 0. 理由: paper 不动用真金, 机会成本不可比.
- **实盘后**: 按月取 US 1M T-bill 滚动均值 (FRED `DTB4WK`), 日化 = annual / 252. 2026-05 约 5.2% annual ≈ 0.0207% daily.
- **跨期对比时口径必须一致**: 任何 Sharpe 同框比较, 必须同 rf. 报告里强制写明 `rf_basis=`.

### 1.3 滚动窗口

| 用途 | 窗口 | 采样 | 备注 |
|---|---|---|---|
| **M4.5 gate 判定** | 14 个交易日 (滚动) | 按日 | 见 §5 |
| 在线监控 (小郑 dashboard) | 7 / 14 / 28 / 90 天 4 档 | 按日 | 4 档同屏 |
| 信号 attribution | 28 天滚动 | 按日 | 每信号独立计算 |
| 季度复盘 | 整季 (90+ 日) | 按日 | 含 bootstrap CI |

### 1.4 已知坑 + 修正

| 坑 | 修正 |
|---|---|
| 单日大涨大跌拉爆 σ | 按 §5.4 winsorize + 报告原始和修正双值 |
| 0 PnL 日 (无交易) 也要计入分母 | 是的, 0 算 0, 不剔除 (体育季节性休息日是真实状态) |
| 大节假日断流 (NBA 6 月空窗) | 单独标注, 不混入 Sharpe 主线 (GM 红线已豁免节假日) |
| 收益分布非正态 (skewness + fat tail) | 报告 Sortino + max DD 作 cross-check |
| 自相关 (持仓跨日) | 按 Lo 2002 SR_AC 修正 (见 §6.1) |

---

## 2. 样本量需求 + Power Analysis

### 2.1 "稳定盈利 2 周" 统计上够吗 — 不够 (单独看), 必须组合多指标

**问题**: M4.5 GM 红线 14 天, 假设日均 10 笔, 总 N = 140 笔. KR-C-4 要求 ≥ 500 场.

**结论**:
- 140 笔 **足以** 拒绝 "Sharpe = 0" (见 §2.2 power)
- 140 笔 **不足以** 估计 hit rate 到 ±2% (置信区间宽), 但足以验证 hit rate **不显著低于** 阈值
- **战术**: M4.5 用复合判定 (Sharpe 检验 + PnL 检验 + 风控通过 + 在线率), 不靠单一统计量

### 2.2 Power Analysis (检测 Sharpe = 1.0 vs 0)

**正态近似下 Sharpe 估计量的 std** (Lo 2002):
```
SE(SR_hat) ≈ sqrt((1 + 0.5 * SR^2) / n)
```

| 真实 Sharpe (年化) | 等价日 Sharpe | n_days (power=0.8, α=0.05, 单尾) | 用途 |
|---|---|---|---|
| 0.5 | 0.0315 | ~ 64 | 太弱, 不达标 |
| **1.0** (M4.5 阈值) | 0.063 | **~ 26** | M4.5 用得到 |
| 1.5 | 0.0945 | ~ 14 | M4.5 内就能检出 |
| 2.0 | 0.126 | ~ 9 | 容易 |

**结论**: 检测 "真实 Sharpe ≥ 1.0 vs null 0", 需要约 **26 个交易日**. M4.5 的 14 天**不够单独做这件事**, 必须借助:
1. paper 起跑后**累计**所有交易日 (M4 → M4.5 之间不止 14 天), 跑滚动 14 天**最后一窗** + 累计窗口双判定
2. M4.5 通过后实盘**再观察 4 周** (sequential test, 见 §3.4) 才动 size 上调

### 2.3 不同 sport 样本量差异

| Sport | 单赛季交易日 | M4.5 14天平均触发数 (估) | 短板 |
|---|---|---|---|
| NBA | ~ 170 (Oct-Apr) + playoff | 80-150 笔 (P0-01) + 30-70 笔 (P0-02) | 6-9 月空窗 |
| NFL | ~ 22 (周日为主) | 10-25 笔 | 周频不够日级 Sharpe |
| MLB | ~ 180 (Apr-Sep) | 30-60 笔 | edge 薄 |
| Soccer 5 大联赛 | ~ 280 (Aug-May) | 100-200 笔 | 国别市场分散 |

**M4.5 跑哪个 sport**:
- 2026-10-29 落在 NBA 季前 / 季初 + NFL 周中段 + MLB 季后赛. 主样本必为 **NBA + NFL** (与小程 catalog 一致).
- 若 NBA 还没正式季初 (10/22 通常 NBA 开赛), 必须延后 M4.5 或调用季前赛数据 (季前赛 sharp 程度低, 慎用).
- @老胡 排期需要 cross-check 时间线.

### 2.4 最小可判定样本量 (Min Sample Size, MSS)

| 判定项 | MSS | 来源 |
|---|---|---|
| Sharpe ≥ 1.0 (power 0.8) | 26 交易日 | §2.2 |
| Hit rate 显著 > 50% (binomial, p < 0.05) | 270 笔 | binom(0.53, n) test |
| PnL > 0 (t-test, 假设日 σ=0.005) | 16 交易日 | (0.001/0.005)² × 6.31 |
| Max DD ≤ 8% 验证 | 单事件即可触发不通过 | 直观 |
| 风控失效 = 0 | 整窗即可触发 | 直观 |

**复合 gate**: 4 项独立判定, 全过才算通过. 详见 §5.

---

## 3. A/B Test 框架

### 3.1 多策略并跑设计 (P0-01 vs P0-02 vs Baseline)

**Paper 阶段流量切分**:

```
Bucket A (50% 资金): SIG-P0-01 单跑
Bucket B (50% 资金): SIG-P0-02 单跑
Bucket C (shadow, 0 资金): random-entry baseline (同 sport, 同时段, 随机方向)
```

**为什么不三分 33/33/33**: paper 阶段总资金虚拟 $50K, 单笔最小 $100, 三分后单 bucket 等效 $16K 测试机会窗太薄, 容量压测无意义. 等 M5 实盘上线再切三向.

**Shadow bucket 必须有**: 不消耗资金, 仅在 paper engine 内打标, 用作**非参数比较的 control**. 这是 GM 红线 "稳定盈利" 的最强 sanity check — 你比 random 还烂的话 Sharpe > 1.0 是噪声.

### 3.2 统计显著性判定

**两两比较 (A vs C 和 B vs C)**:
- 主检验: **Welch's t-test** 比较日收益均值 (不假设等方差)
- 副检验: **Mann-Whitney U** (非参数, 防 fat tail)
- 主+副都要 p < 0.05 才算"显著优于 baseline"

**A vs B (信号互比)**:
- 仅在两个 bucket 都已显著 > C 之后再做
- 用 **paired t-test on aligned days** (同日同条件下 PnL 差)

### 3.3 p-hacking 防范 (硬性约束)

| 风险 | 防范 |
|---|---|
| **多重检验** | Bonferroni 修正: 比较 K 个信号 → α' = 0.05 / K. 当前 K = 2 (P0-01, P0-02) → α' = 0.025 |
| **窥视数据 (peeking)** | 禁止在跑数中途调阈值. 决策点固定: T+0, T+7d, T+14d. 中间日只看 dashboard 不动手 |
| **HARKing (事后改假设)** | 信号上线前必须冻结 `signal_id@vN` + 验收阈值 hash (写入 git tag), 跑完后阈值变更必须 RFC + bump v(N+1) |
| **回测过拟合** | DSR + PBO (见 §6) |
| **survivorship bias** | 失败信号也写入 catalog (with reason), 不删历史 |
| **报告偏差** | 每周报告必须含**所有**跑过的 bucket, 包括 baseline 和已暂停的 |

### 3.4 Sequential Testing (M4.5 之后实盘渐进上量用)

paper → 实盘后, 不能一次切 100%, 必须 sequential:

**Pocock boundary** (双尾 α=0.05, 4 个监测点):
```
Stage 1 (M5 + 7d):  |Z| > 2.413 才能停 (上量/下量)
Stage 2 (M5 + 14d): |Z| > 2.413
Stage 3 (M5 + 28d): |Z| > 2.413
Stage 4 (M5 + 56d): |Z| > 2.413
```

- 每阶段未触发停止 → 资金 × 1.5 上量, 直到 hit 老韩 cap
- 任一阶段触发**负向**停止 (Z < -2.413) → 立即回滚 paper

[注] 这套不是 M4.5 内部用, 是 M4.5 通过之后. M4.5 内部用 §5 的**固定窗口 + 复合**判定.

---

## 4. Bayesian α 衰减检测

### 4.1 目标

线上信号的 edge 在分布上是否随时间下移. 用 Bayesian 而非 frequentist 因为:
- 小窗口 (4-8 周) 内频率主义检验功效不够
- 我们有强先验 (信号上线时的回测 IS Sharpe), 该用上
- 衰减是渐进的, Bayes posterior 给的是分布而不是 reject/accept, 利于风控分级响应

### 4.2 模型

**信号 i 的真实日 Sharpe** 假设 ~ Normal(μ_i, τ²_i):
```
先验 (从回测 IS):  μ_i ~ Normal(SR_is_i, σ_prior²)        σ_prior = 0.3 (年化), 反映 IS vs OOS 不确定性
观测 (每日实盘):    sr_obs_d | μ_i ~ Normal(μ_i, σ_d²)     σ_d = SE(daily SR) ≈ sqrt((1+0.5*μ²)/n_trades_d)
```

**Posterior** (共轭, closed form):
```
τ²_post = 1 / (1/σ_prior² + n_days / σ_d_avg²)
μ_post  = τ²_post × (SR_is / σ_prior² + sum(sr_obs_d) / σ_d_avg²)
```

每日更新一次, 给小郑 dashboard 推 `μ_post` + 95% credible interval.

### 4.3 衰减阈值告警 (与小郑 observability 接口)

**告警分级** (对齐小郑 observability v0.1 §4 路由):

| 等级 | 触发条件 | 行为 | 接口 |
|---|---|---|---|
| **GREEN** | P(μ_i > 0.8) > 0.7 | 正常 | dashboard 绿灯 |
| **YELLOW** | P(μ_i > 0.5) > 0.7 && P(μ_i > 0.8) < 0.5 | 评估, 提 retraining ticket | Alertmanager severity=warning, 邮件 |
| **RED** | P(μ_i > 0.2) < 0.5 | 暂停信号, 强制 review | Alertmanager severity=critical, 电话 + Slack |
| **BLACK** | P(μ_i < 0) > 0.3 | 立即下线, RM 拉闸 | RiskManager kill switch (老韩接口) |

**Metric 命名** (对齐小郑 §1.4 `stcpp_l5_*` 命名):
```
stcpp_l5_signal_alpha_posterior_mean{signal_id="SIG-P0-01"}
stcpp_l5_signal_alpha_posterior_ci_low{signal_id="SIG-P0-01"}
stcpp_l5_signal_alpha_posterior_ci_high{signal_id="SIG-P0-01"}
stcpp_l5_signal_alpha_decay_level{signal_id="SIG-P0-01"}    # 0=GREEN 1=YELLOW 2=RED 3=BLACK
```

**计算频率**: 每日 UTC 00:30 (跨洋错峰), Python cron, 输出到 Prom Pushgateway. 小郑 dashboard 拉.

### 4.4 与小郑 dashboard 对接 (D3 信号监控)

需要小郑 dashboard D3 (信号监控) 增加面板:
- 每个 `signal_id` 一行: μ_post + CI + 等级灯
- 历史 28d posterior 轨迹 (sparkline)
- 触发告警时间轴

**接口契约** (向小郑提):
```yaml
metrics_namespace: stcpp_l5_signal
push_endpoint: pushgateway.obs.internal:9091
push_frequency: daily 00:30 UTC
labels:
  - signal_id (SIG-P0-01 / SIG-P0-02 / ...)
  - signal_version (@v1.2 from xiaocheng catalog)
  - sport (NBA / NFL / ...)
```

---

## 5. M4.5 Gate 严格判定 (GM 红线, 核心 deliverable)

### 5.1 GM 原始红线 (摘抄于 OKR M4.5)

> Paper trading 必须连续 2 周 (不含 6 月节假日 NBA/NFL 端联调期):
> - 累计纸面 PnL > 0
> - Sharpe (按日) > 1.0
> - 风控失效事件 = 0
> - 系统在线率 > 99.5%

### 5.2 数据科学严谨化 (机器判定规范)

GM 红线的统计学加固版本, 翻译成可机器判定:

```
Gate G1 (PnL gate):     sum(daily_pnl[-14:]) > 0
                     && t.test(daily_return[-14:], mu=0, alternative="greater").p_value < 0.10
                     [双重: 简单和也要正, 且单尾 t 检验不能拒绝 "均值 > 0"]

Gate G2 (Sharpe gate):  SR_14d = annualize(mean(daily_return[-14:]) / std(daily_return[-14:])) > 1.0
                     && SR_14d_lower_CI95_bootstrap > 0.3
                     [双重: 点估计 > 1.0, 且 bootstrap CI 下界 > 0.3 (意味着不是单日大涨堆出来的)]

Gate G3 (Risk gate):    count(risk_failure_events[-14:]) == 0
                     [风控失效零容忍]

Gate G4 (Uptime gate):  uptime_pct[-14:] >= 99.5%
                     [对齐小郑 SLO]

Gate G5 (DD gate):      max_drawdown[-14:] <= 8% of bankroll
                     [新加, GM 没写但风险必查]

Gate G6 (Trade count):  count(trades[-14:]) >= 50
                     [防 cherry-pick 单笔暴利, 必须有足够交易支撑统计]

Gate G7 (Shadow gate):  paper_pnl > shadow_random_pnl (paired across days, p<0.10)
                     [比 random 强, 防"行情自然涨大家都赢"假象]

PASS = G1 AND G2 AND G3 AND G4 AND G5 AND G6 AND G7
```

### 5.3 边界 case 处理

| 边界 | 处理 |
|---|---|
| 单日大涨 (PnL > 5× σ_d) | winsorize @ 5σ 再算 Sharpe (双值都报: 原始 + winsorized, 取小的判定) |
| 单日大跌 (DD > 4%) | G5 直接卡 (单日 4% 触发 yellow, 8% black) |
| 节假日 (NBA 6 月空窗) | 跳过, GM 已豁免, 但 G6 trade_count 必须满足 — 即窗口可能拉长到自然 50 笔 |
| 半天交易 (圣诞 / 感恩节) | 计入但减半采样权重 (gate 内不重要, attribution 里要标) |
| 单 sport 一边倒 (只有 NBA, NFL 0 触发) | 不算 fail, 但报告必须标 "single sport sample", M5 上量前要扩 |
| 跨时区日切 | 全部 UTC 00:00 切分, 不用 ET (M4.5 一致性 > 直觉) |

### 5.4 Bootstrap CI 实现 (G2 用)

```python
# 14 个 daily_return 样本
import numpy as np
from scipy import stats

def sharpe_bootstrap_ci(daily_returns, n_boot=10000, alpha=0.05, annual_factor=np.sqrt(252)):
    """
    Bootstrap CI for annualized Sharpe. Returns (point_estimate, lower, upper).
    """
    rng = np.random.default_rng(seed=42)  # 固定 seed, 复现性
    n = len(daily_returns)
    boot_srs = np.empty(n_boot)
    for i in range(n_boot):
        sample = rng.choice(daily_returns, size=n, replace=True)
        mu, sig = sample.mean(), sample.std(ddof=1)
        boot_srs[i] = (mu / sig) * annual_factor if sig > 1e-9 else 0.0
    point = (np.mean(daily_returns) / np.std(daily_returns, ddof=1)) * annual_factor
    lo, hi = np.percentile(boot_srs, [100*alpha/2, 100*(1-alpha/2)])
    return point, lo, hi
```

### 5.5 机器判定脚本规范 (给小蒋 paper engine 和小郑 obs 接)

**脚本路径** (建议): `tools/m45_gate_evaluator.py`

**输入**:
- `daily_pnl.parquet` (date, pnl, bankroll_open, n_trades, uptime_pct, risk_events, max_dd)
- `shadow_pnl.parquet` (date, pnl) — 来自 shadow bucket
- `--window=14` 默认
- `--report_path=...` 输出 JSON + HTML

**输出 (JSON schema, 给 dashboard)**:
```json
{
  "evaluation_date": "2026-10-29",
  "window_days": 14,
  "window_start": "2026-10-15",
  "window_end": "2026-10-28",
  "gates": {
    "G1_pnl":      {"pass": true,  "value": 1234.56, "p_value": 0.034},
    "G2_sharpe":   {"pass": true,  "value": 1.42, "ci_lower": 0.41, "ci_upper": 2.18},
    "G3_risk":     {"pass": true,  "value": 0},
    "G4_uptime":   {"pass": true,  "value": 0.9962},
    "G5_dd":       {"pass": true,  "value": 0.045},
    "G6_trades":   {"pass": true,  "value": 87},
    "G7_shadow":   {"pass": true,  "p_value": 0.041}
  },
  "overall_pass": true,
  "blocked_by": [],
  "winsorized_sharpe": 1.28,
  "next_evaluation": "2026-10-30"
}
```

**调用方**:
- 小蒋 paper engine: 每日 00:30 UTC 跑一次, 推 metric (`stcpp_l5_m45_gate_*`) + 邮件日报
- 小郑 dashboard: D1 大盘加一个 M4.5 gate panel (7 灯)
- 老雷 (GM): 每周一收 PDF 汇总, 包含决策建议 (continue / pause / restart)

**配色**: ALL GREEN → 进入"准实盘"状态 (实盘代码冷启动 + dry-run 7d), 任一 RED → 全员 standup 复盘.

### 5.6 失败重试规则 (对齐 GM 红线 "三次失败 → 撤回 scope")

```
Attempt 1 fail → root cause 复盘 (24h) → 调整 → 重跑 14d
Attempt 2 fail → 撤回到 M4, 重跑 paper engine + 信号调参 → 重跑 14d
Attempt 3 fail → 升级老钱 + 老雷 + 小梁三方战略会, 决议是否撤 MVP scope
```

**统计纪律**: 每次"调整"都必须 bump `signal@vN`, 否则就是 p-hacking. 严格 enforce.

---

## 6. 过拟合检测 (DSR + PBO)

### 6.1 Deflated Sharpe Ratio (Bailey & López de Prado 2014)

回测 Sharpe 不可信原因: 1) 多次试验选最优 (selection bias), 2) 收益非正态 (skew / kurtosis 影响 SR std).

**DSR 公式**:
```
SR* = sqrt((1 - γ) × Var(SR_estimates)) × max(...)         (effective SR threshold from trials)
DSR = Φ( (SR_hat - SR*) × sqrt(n - 1) / sqrt(1 - skew × SR_hat + (kurt-1)/4 × SR_hat²) )
```

其中:
- `SR_hat`: 回测 Sharpe
- `n`: 回测样本量 (天)
- `skew, kurt`: 收益分布 skewness + excess kurtosis
- `SR*`: 反映"试了 N 次最优挑"的 selection benchmark
- `Φ`: 标准正态 CDF

**判定**: DSR > 0.95 才算"统计上显著的 Sharpe".

**实施 (给小蒋)**:
```python
from scipy.stats import norm, skew, kurtosis

def deflated_sharpe(returns, n_trials, sr_hat=None):
    if sr_hat is None:
        sr_hat = returns.mean() / returns.std(ddof=1)  # daily, not annualized
    n = len(returns)
    s = skew(returns)
    k = kurtosis(returns, fisher=True)  # excess kurt
    # selection threshold: 试 n_trials 次的最大 SR 期望 (Bailey & LdP)
    emc = 0.5772156649  # Euler-Mascheroni
    sr_star = np.sqrt(np.log(n_trials)) - emc / np.sqrt(2 * np.log(n_trials))  # 标准化
    sr_star_scaled = sr_star * np.sqrt(2 * np.log(n_trials))  # rough; 实现按 LdP 论文调
    denom = np.sqrt(1 - s * sr_hat + (k - 1) / 4 * sr_hat**2)
    z = (sr_hat - sr_star) * np.sqrt(n - 1) / denom
    return norm.cdf(z)
```

[注] 上面是 sketch, 正式版本 @小蒋按 LdP 2014 论文实现, 我提供 unit test 用例 + 对比 reference values.

### 6.2 PBO (Probability of Backtest Overfitting, López de Prado 2014)

通过 **Combinatorially Symmetric Cross-Validation (CSCV)** 估计: 在回测里挑出的"最佳策略" 在 OOS 排名跌到中位数之下的概率.

**算法**:
```
1. 把回测时间窗等分成 S=16 段
2. 对 (S choose S/2) = 12870 个 IS/OOS 切法:
   a. 在 IS 上挑出 Sharpe 最大的策略 i*
   b. 在 OOS 上计算 i* 的 Sharpe 排名 r*
   c. 计算 logit(r* / (1 - r*)) 作为 "is the best still good?" 信号
3. PBO = P(logit < 0) = OOS 排名跌到中位数之下的频率
```

**判定**:
- PBO < 0.25: 健康
- PBO 0.25 ~ 0.5: 警告, 加强 OOS
- PBO > 0.5: 过拟合, 弃用

**Top 1 防过拟合方法 (GM 汇报用)**: **PBO + walk-forward 联用**. PBO 是诊断 (告诉你过没过拟合), walk-forward 是预防 (强制 OOS 滚动). 单 DSR 不够, 因为它假设 trial count 已知, 但很多 trial 是隐性的 (改个参数没记账).

### 6.3 与小蒋 walk-forward 接力

| 物件 | 我交付 | 小蒋交付 |
|---|---|---|
| DSR 实现 | Python ref impl + 5 test cases | 集成到 backtest 报告 |
| PBO 实现 | Python ref impl + CSCV 切分逻辑 | 集成 + 输出 PBO score |
| Walk-forward 切分约定 | IS/OOS 比例 (70/30) + roll step (4 周) | 引擎支持 |
| Trial count 登记 | 每次回测必须 declare n_trials | 工程实现 declare 接口 |
| 拒绝阈值 | DSR > 0.95 AND PBO < 0.25 | gate 函数 |

### 6.4 防止隐性 n_trials 漂移

最阴险的过拟合: 跑了 100 次回测, 报告里只写 1 次. DSR 失效.

**纪律**:
- backtest 引擎每次跑必须写 audit log (`backtest_run_id`, `signal_version`, `params_hash`, `ts`)
- DSR 计算时 `n_trials = COUNT(DISTINCT params_hash FROM audit WHERE signal_id = X AND ts < report_ts)`
- 老韩 audit 同样规则, 不可手工改

---

## 7. 实盘 vs Paper 对照检验

### 7.1 PSD (Paper-Prod Sharpe Deviation)

**定义**:
```
PSD = SR_paper(同窗口) - SR_prod(同窗口)
```

**预期偏差来源** (按贡献排序):
1. **滑点 + 微观结构** (小晓 Kelly+slippage 模型 v1 已建): 估 0.3-0.6 SR 单位 (年化), paper > prod
2. **跨洋决策延迟** (老姜延迟预算 v1): inplay 信号 (P0-02) 估 0.2-0.4 SR 单位, pregame (P0-01) 估 < 0.1
3. **未成交率** (paper 假设 100% 成交, prod 有部分被吃): 估 0.1-0.2 SR
4. **adverse selection** (sharp 也在和我们抢): 估 0.1-0.3 SR
5. **fee schedule 当前 0% 但 PM 可能变化**: 未来风险

**期望总 PSD**: 0.7 - 1.5 SR 单位.

### 7.2 PSD 验收阈值

| PSD 范围 | 解读 | 行为 |
|---|---|---|
| PSD < 0.5 | 实盘比预期还好 | 检查 paper engine 是不是太保守 |
| 0.5 ≤ PSD ≤ 1.5 | 符合预期 | 正常 |
| 1.5 < PSD ≤ 2.5 | 偏高, 滑点模型可能低估 | 复盘小晓模型, 调 paper slippage |
| PSD > 2.5 | 严重偏差, 可能信号本身就过拟合 paper 或 prod 有 bug | 立即停, 复盘 |

### 7.3 同窗口对照检验

- M5 上线后, 每周计算 PSD, 滚动 28 天
- bootstrap CI for PSD: paired difference between paper and prod daily returns
- 若 PSD CI 下界 > 2.5 持续 2 周 → 触发 BLACK alert

### 7.4 paper engine 必须输出的字段 (给小蒋提需求)

```
trade.expected_fill_price   (paper assumes this)
trade.expected_slippage_bps (from xiaoxiao model)
trade.expected_latency_ms   (from laojiang budget)
trade.signal_id_version
trade.bucket (A / B / C-shadow / random)
trade.is_paper (true)
```

prod engine 必须输出同名字段 + `actual_fill_price` `actual_slippage_bps` `actual_latency_ms` `actual_fill_pct`.

PSD 分析的核心: 对齐字段 + diff.

---

## 8. 统计工具选型

### 8.1 Python `.venv` (已装, 与老吴 toolstack v1 一致)

| 库 | 用途 | 版本固定 |
|---|---|---|
| `numpy` | 数值底座 | 1.26+ |
| `pandas` | 数据清洗 | 2.1+ |
| `scipy.stats` | t-test / binomial / norm | 1.11+ |
| `statsmodels` | OLS / time series / power analysis | 0.14+ |
| `pyarrow` | parquet IO | 14+ |
| `matplotlib` + `seaborn` | 报告图 | latest |
| `jupyter` | 交互分析 | latest |

**新增 (我会提 PR 加 requirements)**:
- `arch` (GARCH for σ 估计, 防 heteroscedasticity)
- `pingouin` (effect size + bayes factor 一站式)
- `joblib` (bootstrap 并行)

### 8.2 DuckDB (OLAP)

用途:
- 跨表 join (trades × shadow × signals × news) 一次查
- 直接读 parquet 不用先 load
- 嵌入 Python (无需起服务)

**典型 query 模板** (M4.5 gate 用):
```sql
COPY (
  WITH window AS (
    SELECT date, signal_id, bucket, pnl, bankroll_open,
           pnl / bankroll_open AS ret,
           n_trades, uptime_pct, risk_events, max_dd
    FROM paper_trades
    WHERE date BETWEEN DATE '2026-10-15' AND DATE '2026-10-28'
  )
  SELECT
    COUNT(*) AS n_days,
    SUM(pnl) AS total_pnl,
    AVG(ret) AS mu_ret,
    STDDEV_SAMP(ret) AS sig_ret,
    SUM(n_trades) AS total_trades,
    AVG(uptime_pct) AS uptime,
    SUM(risk_events) AS total_risk_events,
    MAX(max_dd) AS worst_dd
  FROM window
) TO 'gate_input.parquet';
```

### 8.3 Jupyter (离线分析)

用途:
- 信号 attribution 月报
- DSR + PBO 计算 (跑得久, notebook 适合)
- 回测结果可视化 (与小蒋共用)

**路径约定** (与小米 docs 协调):
- `notebooks/m45_gate/<eval_date>.ipynb` — M4.5 gate 每次评估留档
- `notebooks/signals/<signal_id>/<analysis_type>.ipynb` — 信号专项

### 8.4 不引入的工具 (与老吴 toolstack v1 一致)

- R (统计 strong, 但生态分散, 团队 Python 一把梭)
- MATLAB (商业, 不入仓)
- Stata (商业)

---

## 9. 开放问题

### 9.1 待 [实测/计算]

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| OQ-D1 | M4.5 14 天窗口实际能拿到多少 daily samples (考虑节假日 + sport 季节性) | 我 + 老胡 | 6/19 |
| OQ-D2 | shadow random-entry baseline 的具体实现 (谁来负责 shadow engine?) | 小蒋 (paper engine) | 6/26 |
| OQ-D3 | PSD 模型的 0.7-1.5 SR 估计是否经得起 M5 真实数据校验 | 我 + 小晓 | M5 + 4w |
| OQ-D4 | Bayesian posterior 的 `σ_prior=0.3` 是否合理 (vs IS/OOS gap 实测) | 我 + 小蒋 | M2 后 |
| OQ-D5 | `n_trials` 的硬性登记机制由谁实施 (audit 接口) | 老韩 + 小蒋 | M2 |

### 9.2 待 [咨询]

| # | 问题 | 咨询对象 |
|---|---|---|
| OQ-D6 | rf = 0 在监管/合规口径下是否需要披露备注 (实盘报表) | 老黄 (compliance) |
| OQ-D7 | DSR + PBO 实施时 LdP 论文有几个口径差异 (skew 是 sample vs population, kurt 是 excess vs raw), 该用哪个 | 小梁 + 老彭 |
| OQ-D8 | M4.5 失败后 "调整后重跑" 是否需要重置 bankroll (paper 阶段 bankroll 浮动) | 老雷 + 老韩 |
| OQ-D9 | shadow bucket 的"随机入场"用 uniform 还是按真实 signal 时间分布 sampling | 小程 + 小蒋 |
| OQ-D10 | 当 P0-02 信号样本不足 (只在 inplay) M4.5 是否单独跑 P0-01 gate | 老雷 (战略) |

### 9.3 待 [战略] 决策

| # | 问题 | 决策人 |
|---|---|---|
| OQ-D11 | M4.5 七个 gate (§5.2 G1-G7) 是否全部硬性, 还是允许 1 个 yellow 通过 | 老雷 + 老韩 |
| OQ-D12 | M4.5 PASS 后实盘上量速度: Pocock sequential (§3.4) vs 老韩 cap 哪个优先 | 老韩 + 老雷 |
| OQ-D13 | Bayesian decay 监控的 BLACK 触发是否直接接 RM kill switch (我倾向 yes) | 老韩 |
| OQ-D14 | A/B test 50/50 资金切分是否经得起 paper $50K 总盘的统计功效 | 老钱 + 我 |

### 9.4 已知未知

- shadow random-entry baseline 在体育 inplay 这种非平稳分布上是否真的"中性" (可能 random 自带方向偏)
- Bayesian σ_prior 的设定本质是主观, 我用 0.3, 是否需要敏感性分析
- DSR 在 14 天窗口的 finite-sample 表现尚未在体育数据上 validate (论文用股票)
- M4.5 节假日豁免的边界还需要 GM 在 OQ-D11 决议时给细节

---

## 附录 A: 与其他 Sprint-1 文档的依赖关系

```
xiaodong-stats-validation-framework-v1 (本报告)
  ├── 输入依赖 →
  │     ├── xiaocheng-signal-catalog-v1: 12 信号 + A/B 测试方案雏形 (本报告 §3 升级)
  │     ├── xiaojiang-backtest-framework-v0.1: DSR/PBO 实施 (本报告 §6 提需求)
  │     ├── xiaojiang-paper-trading-engine-v0.1: shadow bucket + gate evaluator hosting
  │     ├── xiaoxiao-kelly-slippage-model-v1: PSD 预期偏差源 (本报告 §7)
  │     ├── xiaozheng-observability-v0.1: dashboard D3 + alert 路由 (本报告 §4)
  │     ├── laojiang-latency-budget-v1: PSD 延迟分量
  │     └── OKR M4.5: GM 红线机器判定 (本报告 §5 核心)
  │
  ├── 直接产出 →
  │     ├── 小蒋: gate_evaluator.py 接口 + DSR/PBO 实施需求
  │     ├── 小郑: stcpp_l5_* metric 命名 + 4 级 alert 路由
  │     ├── 老韩: BLACK alert → kill switch 接口
  │     ├── 小梁: A/B test 框架 + Bayesian decay 验收口径
  │     └── 老雷: M4.5 gate 7 灯 PDF 周报
  │
  └── 等候反馈 →
        ├── 老雷 (GM): OQ-D11/D12 战略决议
        ├── 老韩 (风控): OQ-D5 audit 接口 + OQ-D13 kill switch
        └── 小梁 (quant): OQ-D7 DSR 口径
```

## 附录 B: 给小蒋 + 小郑的 contract 摘要

**给小蒋 paper engine 必须输出**:
- `daily_pnl.parquet` schema: `date, signal_id, bucket, pnl, bankroll_open, n_trades, uptime_pct, risk_events, max_dd`
- shadow bucket 跑同样 contract, 仅 `bucket=shadow_random`
- 每个 trade 记 `expected_*` vs `actual_*` (for PSD)
- audit log `backtest_run_id` + `signal_version` + `params_hash` (for n_trials in DSR)

**给小郑 observability 必须支持**:
- D3 信号监控面板新增 4 个 metric (`stcpp_l5_signal_alpha_*`)
- D1 大盘新增 M4.5 gate panel (7 灯)
- Alertmanager 增加 4 级路由 (GREEN/YELLOW/RED/BLACK) for `stcpp_l5_signal_alpha_decay_level`
- Pushgateway 接收每日 00:30 UTC 推送

---

**v1 收尾**. 本报告完成 Wave 6 统计验证框架. v2 计划 M2 后发布, 灌入第一次 P0-01 backtest DSR/PBO 实测 + Bayesian σ_prior 敏感性分析.

— 小董, 2026-05-28
