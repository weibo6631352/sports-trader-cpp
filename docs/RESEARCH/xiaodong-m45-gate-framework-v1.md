# M4.5 7 Hard Gate 统计 Framework v1.1

- **Owner:** 小董 (stats-inference-advisor)
- **last_review:** 2026-06-01
- **v1 → v1.1 变更:** G2 阈值 §3.2 加 4 stage 梯度表 + ADR-016 引用 (W6 Wave 28)
- **Reviewers (会签):** 小蒋 (paper engine 数据契约) / 老韩 (RM fail 计数) / 老吴 (uptime 来源) / 小梁 (阈值, ADR-016 会签)
- **代码:** `include/stcpp/stats/gate_evaluator.hpp` + `src/stcpp/stats/gate_evaluator.cpp`
- **测试:** `tests/unit/test_gate_evaluator.cpp` (28 + 5 = 33 case, 新增 T_G2_StageThresholds)
- **ADR:** `docs/ADR/2026-06-01-adr-016-g2-ci-lower-threshold.md`

---

## 1. 背景与目标

M4.5 是公司从 paper 切到 live 的唯一关卡. 用户原话 (CLAUDE.md §11 上下文):
"paper trading 必须稳定盈利才上实盘". Sprint-1 retro 把 "稳定盈利" 具体化为 **7 个 hard
gate, 连续 2 周全过才解锁**. 任一 fail 重置 2 周窗口.

本框架做 3 件事:
1. 定义 7 gate 的 **数学公式 + 阈值 + pass/fail rule**.
2. 提供 **C++ evaluator** (paper / live 共享, R-7 + R-11 build-time 切数据源不分叉逻辑).
3. 保证 **统计严谨性** (Welch t, percentile bootstrap, df 修正, fail-closed on NaN).

---

## 2. 7 gate 总表

| Gate | 含义 | 检验方法 | 阈值 | 来源 |
|---|---|---|---|---|
| G1 | paper PnL 与 0 显著 | one-sample Welch t, 双侧 | p < 0.05 + mean > 0 | Sprint-1 retro |
| G2 | Sharpe 95% CI 下限 | percentile bootstrap, B=1000 | CI_lower > 0.5 | 小梁 + 小董 v1 |
| G3 | RM 失效次数 | scalar count | == 0 | 红线 R-1 |
| G4 | 系统在线率 | uptime / total | ≥ 0.995 | 北极星 SLA |
| G5 | 最大 DD | peak-to-trough on equity | ≤ 0.08 | Sprint-1 retro |
| G6 | 笔数 | per-trade pnl size | ≥ 50 | 防小样本 |
| G7 | paper > random baseline | two-sample Welch t, one-sided | p < 0.05 + paper > random | Sprint-1 retro |

---

## 3. 每 gate 公式 + 阈值理由

### 3.1 G1 — PnL t-test

H0: E[PnL] = 0    H1: E[PnL] ≠ 0 (双侧)

Welch 一样本 t-stat:
```
t = mean(pnl) / (s / sqrt(n)),   df = n-1
```
p-value 用 Student-t 分布 survival, 算法走 regularized incomplete beta:
```
p_two_sided = I_x(df/2, 1/2),   x = df / (df + t^2)
```
Pass 双条件: `p < 0.05` **且** `mean > 0` (方向 guard — 显著亏不算解锁).

**理由阈值 0.05:** 标准学术显著水平; 7 个独立 gate 全过的联合事件本身比 Bonferroni 校正
还严. 不做单 gate Bonferroni (会丢敏感性, 而 7 gate 关心的是 AND 而非 union).

**来源:** Welch (1947), Student (1908). NR3 §6.4.

### 3.2 G2 — Sharpe bootstrap CI

点估 Sharpe = mean / std × annualizer.

Percentile bootstrap: 从 per_trade_return resample n 次 (with replacement), 重算 Sharpe,
做 B=1000 次, 取排序后的 2.5 / 97.5 pct 为 95% CI.

Pass (M4.5 stage): `CI_lower > 0.5`.

**4 Stage 阈值梯度 (ADR-016, 2026-06-01 正式入档)**

| Stage | `GateStage` 枚举 | CI 下界阈值 | 适用场景 | 时间点 |
|---|---|---|---|---|
| M2 | `M2` | **0.3** | alpha 检测低门槛 (信号存在性) | W6-W8 |
| M4.5 | `M4_5` | **0.5** | paper 2 周解锁门槛 (SSOT) | paper 14d 窗口 |
| M5 Q1 | `M5_Q1` | **0.8** | live 第 1 季度稳态 KPI | M5+ live Q1 |
| 北极星 | `NorthStar` | **1.5** | T+36 月北极星 KPI | CLAUDE.md §2 |

代码: `kG2SharpeCILowerByStage` (array<double,4> in gate_evaluator.hpp). `kG2_SharpeCILow` = 0.5 (引用 M4_5 slot).

**历史注记:** Sprint-1 retro (2026-05-28) 小梁口头提过 0.3, 但未正式会签代码. 小董 W4 代码落 0.5. 小梁 2026-06-01 W5 Smell-4 明确"以小董代码为准 (0.5)". ADR-016 正式入档确认.

**B=1000 合理性:** 标准误 SE_pct(B) ≈ sqrt(p(1-p)/B) → 对 2.5% 分位 ≈ 0.0049, 在 Sharpe
scale 上约 0.01–0.03 (单位看波动率). 升 B=10000 → SE 减半但每次 evaluator 跑批耗时 10×
(paper 每周末跑, 我们等不起 5 分钟); 降 B=100 → SE 翻 3× 不可接受. 选 **B=1000** 是 cost
vs precision 的扫尾甜区 (Efron & Tibshirani 1993, §13.4).

**阈值 0.5 (非 1.5 北极星) 理由:** paper 解锁是 "信号 > 噪声" 的**低门槛**, 不是要求即刻
达成最终目标. 1.5 是 36 个月后单策略稳态 KPI, 此时只有 2 周 paper, 任何 Sharpe > 0.5 的
CI 下界都意味着 "极大概率有 alpha", 足以放进 live 继续 ramp. 这条线如果设 1.5, 14 天 paper
几乎不可能过 (CI 宽度 ~3·SE_Sharpe ≈ 1.5 当 n=50).

**BCa 升级:** percentile method 对 skewed Sharpe 分布有偏 (paper PnL 长尾). BCa (bias-
corrected accelerated, Efron 1987) 在 50 笔下偏差更小, 但实现 ~150 行 + jackknife. **本
v1 用 percentile, Sprint-3 升 BCa**.

### 3.3 G3 — RM 失效次数 = 0

简单 count == 0. 任何 bypass (老韩 risk_gateway 输出的 BYPASS / STALE_NOT_HALTED /
HARD_CAP_VIOLATED 等 7 种) 都算 1 次. Sprint-1 retro 决议: **一次失效重置 2 周窗口**.
本 evaluator 只汇报, 重置由调度脚本 (W5 接小蒋) 实施.

**Negative input** → InvalidInput error (上游数据脏, fail-closed).

### 3.4 G4 — 在线率 ≥ 99.5%

uptime_seconds / total_window_seconds. 数据 @老吴 SRE (uptime_events.parquet 状态变更事件,
RUNNING+WARNING 算在线; HALTED+SAFE_MODE+process down 算掉线).

**阈值 99.5% 理由:** 北极星目标 99.9% (long-term, multi-region failover). paper 阶段 14
天 = 1209600s, 99.5% 允许 6048s ≈ 1.68h 掉线总量. 这给单点重启 + WSS reconnect + signer
restart 留余量, 不至于一次小事故就重置 2 周窗口 (G3 才是零容忍).

### 3.5 G5 — 最大 DD ≤ 8%

peak-to-trough on equity_curve. 每点维护 running peak, DD = (peak - eq) / peak. 取
max over time.

**阈值 8% 理由:** 北极星 max DD ≤ 15% (long-term). paper 14 天 8% 是 Sprint-1 retro 给
出的 "未到风控触发线但已显著告警" 的中间档. 思路: paper 任何超 8% 都是策略 / 风控有未识
别风险, **不允许带病上 live**.

**Boundary inclusive:** DD ≤ 8.0% pass, > 8.0% fail (单测覆盖 8.0 / 8.01 两个 case).

**peak ≤ 0 处理:** equity 一直不为正 → 无法定义 % DD → InvalidInput. paper 跑批不该出现.

### 3.6 G6 — 笔数 ≥ 50

per_trade_pnl_usdc.size() ≥ 50.

**阈值 50 理由:** Kelly·0.25 单笔 ~$1.25K (老彭 sizing), 50 笔 ~ $62.5K 流水. 统计上
n=50 下 t-test 自由度 49, t 临界值 ~2.01 (vs 大样本 1.96), 接近渐近正态; bootstrap CI
宽度 ~3·SE_Sharpe ≈ 0.3–0.5 (取决于 σ), 与 G2 阈值 0.5 兼容. 笔数 < 50 时 G2 CI 太宽,
失败概率主导是 "样本少" 而非 "信号差", 解锁失真.

### 3.7 G7 — paper > random baseline

H0: μ_paper = μ_random    H1: μ_paper > μ_random (one-sided)

Welch 双样本 t-stat (不假设等方差):
```
t = (mean_paper - mean_random) / sqrt(s_p²/n_p + s_r²/n_r)
df = (s_p²/n_p + s_r²/n_r)² / [ (s_p²/n_p)² / (n_p-1) + (s_r²/n_r)² / (n_r-1) ]
```
Satterthwaite df. p_one_sided = p_two_sided / 2 (当 t > 0).

Pass: `p < 0.05` **且** `mean_paper > mean_random`.

**G7 random_baseline 怎么生成 (关键问题):**

v1 协议 (待会签):
- **同窗同事件**: paper 同 2 周内每个 signal trigger 时点取一次 baseline 样本
- **同 size**: 直接复用 paper 给该笔下的 size (Kelly·0.25 计算结果)
- **50/50 BUY_YES/BUY_NO 随机**: PRNG (seed=baseline_run_id, 可复现) 抛硬币定方向
- **同 fee schedule**: Polymarket 手续费 + slippage 走 SlippageModel.Linear (小肖) 真算
- **同窗口结算**: 持仓 holding 与 paper 同一规则 (止盈 / 止损 / 到期)

**为什么不是 "市场基线 (买大盘指数)":** Polymarket 体育市场不像 SPY 有自然指数, 任何
"all-yes 50/50" 都是人造. 我们要回答的问题是: **paper signal 是否比"不带信号纯随机"显著**.
50/50 baseline 是这个问题的 null model.

**实施 owner @小蒋** (W5 接 paper_audit.wal 时同时 emit 一份 random_baseline.wal, 共享
SlippageModel + RM 路径, 区别只在 intent 来源).

---

## 4. p-value 数值实现

Student-t survival 用 regularized incomplete beta (Numerical Recipes 3e §6.4):
```
I_x(a, b) = front · CF(a, b, x) / a       (x < (a+1)/(a+b+2))
I_x(a, b) = 1 - front · CF(b, a, 1-x) / b (else)
front = exp(a·ln(x) + b·ln(1-x) - lnB(a,b))
```
CF = Lentz continued fraction, 200 iter, eps 3e-12.

p_two_sided(t, df) = I_x(df/2, 1/2),   x = df / (df + t²)

**精度:** vs scipy.stats.t.sf 校准误差 < 1e-10 在 df ∈ [1, 1000], |t| ∈ [0, 20]
(单测 G1/G7 通过验证).

**避坑:** df 必须 > 0, t 必须 finite. 退化 (std=0) 直接走 "全相等" 分支, 不走 CF.

---

## 5. R-20 4 ts 契约

GateMetrics 自带:
- `window_start_ts_ns` (event: paper 第一笔 fill)
- `window_end_ts_ns` (data_source: paper 最后一笔 fill)
- `ingestion_completed_ts_ns` (ingestion: audit.wal 读完时刻)
- `as_of_ts_ns` (as_of: evaluator 调起时刻)

`CheckPit()` 链式校验 start ≤ end ≤ ingestion ≤ as_of, **任一 break → 7 gate 全 fail
+ first_error=PitViolation**. 上游应已经过 `pit::AssertChain` (with now() check), 本
evaluator 不重复查 "as_of ≤ now()" 避免 evaluator 跑批与 paper 收集间隔太久误判.

**为什么不直接 link pit.hpp:** evaluator 在统计层, pit.hpp 在 WAL 层. evaluator 的语义
是 "**输入数据的时序合规**", 而非 "**WAL 落盘的物理时序**". 概念分离避免循环依赖.

---

## 6. fail-closed 数据卫生

所有时序输入用 `AllFinite()` 预检. 任何 NaN / Inf → InvalidInput, pass=false. 解锁 live
是公司红线 P0, **宁拒绝不误放**. 单测覆盖:
- G1 NaN in pnl → InvalidInput
- G2 Inf in return → InvalidInput
- G5 NaN in equity → InvalidInput
- G3 negative count → InvalidInput
- G4 uptime > total → InvalidInput (数据源 bug)

---

## 7. 触发 GM 决议的事项

| 议题 | 当前值 | 候选 | 建议人 |
|---|---|---|---|
| G2 Sharpe CI 下限阈值 | 0.5 (M4.5 stage, **ADR-016 已决**) | 4 stage 梯度: 0.3/0.5/0.8/1.5 | 小董 / 小梁 会签 ✓ (ADR-016) |
| G2 bootstrap B | 1000 | 1000 / 10000 | 小董, 不建议改 |
| G5 max DD 阈值 | 8% | 6 / 8 / 10% | 老韩 + 小梁 |
| G6 笔数下限 | 50 | 30 / 50 / 100 | 小董 + 老彭 |
| G7 random baseline 生成协议 | 50/50 同窗 | 待会签 | 小蒋 + 小董 + 小梁 |
| G1/G7 p 阈值 | 0.05 | 0.05 / 0.01 | 小董, 建议保持 0.05 |
| 是否做 Bonferroni 校正 | 不做 | 不做 / 做 | 小董 不建议 (AND 关系本已严) |

**建议 GM 决议触发点:** G2 阈值 0.5 + G7 random baseline 协议. 其余维持 Sprint-1 retro
原值即可.

---

## 8. 与现有 6-gate POC 的关系

`tools/m4_5_gate/run_gate_check.py` (小蒋) 是 v0.1 Python POC, 6 个 gate:
G-A duration / G-B PnL / G-C Sharpe 点估 / G-D Risk / G-E Uptime / G-F OOS decay.

**v1 升级点:**
1. 6 → 7 gate, 新增 G7 random baseline (统计上把 "策略" 跟 "无策略" 隔离开)
2. G1 从 "PnL > 0" → "t-test 显著且 > 0" (杜绝小样本伪盈利)
3. G2 从 "Sharpe > 1.0 点估" → "bootstrap CI lower > 0.5" (考虑不确定性)
4. 整套搬到 C++ 进生产二进制 (R-11 paper / live 共享 evaluator)
5. R-20 PIT 前置硬绑

**Python POC 保留** 作 baseline + 跑 paper_audit.wal 的 quick sanity, **决策依赖 C++
evaluator**.

---

## 9. 不耻下问 (跨域求助 backlog)

| 问题 | 求助 | 状态 |
|---|---|---|
| paper_audit.wal schema (per-trade pnl 字段名 / 时间戳源) | @小蒋 | W5 对接 |
| RM bypass count 7 种 → 是否合并 1 个 scalar | @老韩 | 待 v1.1 会签 |
| uptime_events.parquet schema 字段名 | @老吴 SRE / 小郑 | W5 对接 |
| G2 阈值 0.5 / G5 阈值 8% 经济合理性 | @小梁 financial-expert | 派单后会签 |
| BCa bootstrap 实现 (Sprint-3 升级) | 自接 | Sprint-3 backlog |

---

## 10. 后续路线

- **v1 (本次, W4 Wave 20):** C++ evaluator + 28 测试 + research doc
- **v1.1 (W5):** 接 paper_audit.wal 真数据 (@小蒋), 接 uptime_events (@老吴)
- **v1.2 (W6):** 跑第一次完整 14 天 paper, 输出 GateOutcome JSON 入 audit log
- **v2 (Sprint-3):** BCa bootstrap, Bayesian posterior on Sharpe (informative prior:
  小程 P0-01 backtest Sharpe), block bootstrap (考虑 paper 时序自相关)

---

**Refs:**
- Welch, B.L. (1947). *The generalization of "Student's" problem when several different
  population variances are involved*. Biometrika 34.
- Efron, B. & Tibshirani, R. (1993). *An Introduction to the Bootstrap*. CRC. §13.4.
- Numerical Recipes 3rd ed., §6.4 (incomplete beta) + §14.5 (t-test).
- 小蒋 v0.1 `tools/m4_5_gate/run_gate_check.py` (6-gate Python POC).
- 老王 WAL framework v0.2 §7 (pit::AssertChain).
