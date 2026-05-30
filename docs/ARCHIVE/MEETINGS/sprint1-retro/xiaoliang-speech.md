# 小梁 Sprint-1 Retro 发言

- **Speaker:** 小梁 (financial-expert, 量化研究部 owner)
- **Date:** 2026-05-28
- **Sprint:** Sprint-1 Retro
- **听取义务:** 已读 小程 / 小蒋 (回测+paper) / 小袁 / 小肖 / 小董 / 老彭 / 小邓 / 老韩 / GM 两份 ADR
- **双向收口:** 我对接 — 上对老雷 GM, 平对老韩 / 小程 / 小蒋 / 小肖 / 小袁 / 小董, 下对老彭 / 小邓

---

## 1. 开场: 这个 Sprint 我读到了什么

九份必读读完, 三件事让我点头, 两件事让我后背一凉.

**点头:**
1. 小袁实测 264 双边样本把我市场结构 v1 里 §3.4 那张"吃单粗估表"几乎全部覆盖, 且 **gameday 实测比我估的乐观 1-2 个 tick** — 我 §3.4 给 $2K 单 = 1-2 tick, 他实测 gameday 中位 0¢. 这个数字差异不是噪音, 是 sample window 选对了 (Roland Garros + NBA Finals + MLB regular).
2. 小肖 v1 把 Kelly 的红线 (用 expected fill 不用 quote / fail-closed / fill_rate floor 0.50) 直接定死, 我不用再去掰 — 我只需要确认参数初值.
3. 小董 7 个 hard gate 是把 GM M4.5 红线翻译成可机器判定, **G2 (Sharpe 点估 > 1.0 + bootstrap CI 下界 > 0.3) 双重判定** 我特别同意, 单一 Sharpe > 1.0 用 14 天样本太脆.

**后背一凉:**
1. 小袁 §3.7 实测: Polymarket 体育市场 **taker 3%, maker 0% + 0.75% rebate**. 我 §3.3 推断 Polymarket "vig 比 Pinnacle 锐 (< 2%)" 是对的, 但 §1.4 实测 **等效 vig 中位 0.1-2.5¢** + 套利机器人零 hit — 这意味着 **我们做 taker 的, expected edge 必须 > 3%, 不是 > 1¢**. 我 P0-01 的 3¢ 阈值在 fee 后看, edge 净额只有零点几¢ — 必须重新算账.
2. 老李"`.env` key 实活, 不需要 rotation alert" — 这个决策我必须问清楚, 见 §2.

---

## 2. 回答主持人六问

### 2.1 Pinnacle 路径 — 我之前推 P0-01 最稳, 老李那条"不需要 rotation alert"我怎么看

**先表态: 我同意老李对 rotation alert 的判断, 但理由不同.**

老李的理由: `.env` key 是 vendor-issued, 不像 wallet key 频繁 rotate, 监控成本高于收益.
我的理由: **Pinnacle 数据是 P0-01 唯一 anchor, key 失效 → 信号直接死, 不需要 rotation alert 是因为 key 失效会被 GM ADR-2026-05-28 (Goalserve odds gap) 的 fallback 链兜底** — 已经规划路径 A (Pinnacle API) / B (The Odds API $500/月) / C (老彭手工 CSV), 三路径冗余在前, alert 在后.

但我加一条**强约束**回给老李 / 老沈:
- **凭证健康度 metric 必须埋** (latency p99, 401/403 error rate, 数据 staleness), 这不是 rotation alert, 是 **vendor health monitoring**. 区别在: rotation 是预防, health 是事后. 我要后者, 不要前者.
- 我 P0-01 的 §3.1 `pinnacle.last_update_age <= 5_min` 滤波就是 vendor health 的策略层兜底.

**结论:** Pinnacle 路径决策权我支持老雷在 ADR-2026-05-28 (Goalserve odds gap) 给的"双线并进 + Pinnacle 路径 A/B/C 同步推进". P0-01 不卡在单一数据源.

### 2.2 小袁 taker 3% — P0-01 信号 edge 假设达标吗

**短答: 阈值要重新校准, 我之前的 3¢ 在 fee 后不够. 我现在改 5¢.**

小袁 §3.7 实测 + 我重新算账:

| 项 | 数字 | 来源 |
|---|---|---|
| Pinnacle no-vig fair price 偏离触发阈值 | 3¢ (v1) → **5¢ (改)** | 小程 §3.1 trigger, 我改 |
| taker fee | 3% × notional | 小袁 §3.7 实测 |
| 在 p=0.50 价位 fee 折算 cents | ~3¢ (3% × $1 notional = 3¢ per $) | 算 |
| 实际滑点中位 ($2K gameday) | 0¢ - 1 tick | 小袁 §3.3 |
| 实际滑点 ($10K gameday) | 0.5-1¢ | 同上 |
| **净 edge 估算 ($2K gameday)** | **5 - 3 - 0 = 2¢** | 推算 |
| **净 edge 估算 ($10K gameday)** | **5 - 3 - 1 = 1¢** | 推算 |

**关键数字 (我对小程的反馈):**
- P0-01 触发阈值从 **`|dev| >= 0.03` 改 `|dev| >= 0.05`** — 留 2¢ 给 fee + 滑点
- 单笔上限 (MVP) **$2K**, 避免 $10K 单位的 1¢ 滑点把 edge 吃成 1¢
- hit rate 60-65% 的假设在 5¢ 阈值下我维持不变 (Pinnacle 锐, 5¢ 偏离更稀有但更有效)
- **EDGE_NEGATED_BY_SLIPPAGE 拒单率监控** — 小肖 §4 已经加了拒因, 我盯着看; 若 > 60% 说明我阈值还低, 调到 6¢

**给小程**: P0-01 阈值正式定 **5¢**, 信号 contract YAML 改字段 `0.03 → 0.05`. 我会签.

### 2.3 小肖 fill_rate floor 0.50 + KELLY_FRACTION 0.25 配合是否过保守

**短答: MVP 阶段不过保守, 6 个月数据后再松.**

我做的是金融理论, 不做工程妥协. 1/4 Kelly 是 Thorp 1962 + MacLean-Thorp-Ziemba 2010 给的"概率估计有偏差时的 Pareto 前沿低风险端", expected log growth 降到 44%, drawdown 方差降到 1/16. **这不是保守, 这是数学正确性.**

fill_rate floor 0.50 配 KELLY 0.25 的乘积效应:
- 名义 Kelly size × 0.25 (fractional) × 0.50 (fill_rate floor) = **12.5% Kelly equivalent**
- 这看上去是 1/8 Kelly, 但实际等价 Kelly 是 0.25, 因为只有 fill 上的部分才计入仓位
- 即: "实际下注的金额, 在 fill 后等价 1/4 Kelly", 没有重复保守

但我同意小肖的隐含警告 (§7 M5+ 松绑表): 4 周 Sharpe >= 1.0 且 max drawdown <= 预期 50% 后, **KELLY_FRACTION 可松到 0.50 (1/2 Kelly)**. M5 前不动.

**给小肖**: fill_rate floor 0.50 + KELLY 0.25 配合 OK, 我会签 v1. 不需要在 MVP 阶段降 floor.

**给老韩**: KELLY_FRACTION 参数表 §7 我会签 0.25, 写入 v0.2.

### 2.4 老韩 PER_ORDER_CAP — 6/4 给数, 现在能给吗

**能给, 但要分两档.**

我看了:
- 小袁 §3.3 gameday $2K 单中位 0¢ 滑点, $10K 单中位 1¢
- 老韩 §7 参数表 (PER_ORDER_CAP_HARD / PER_ORDER_CAP_SOFT 都是 TBD @我)
- 小程 §3.1 P0-01 信号 `size = clamp(size_base * bankroll, 200, 1500)` 原写 $200-$1500 (MVP)
- 老钱给的 MVP virtual bankroll (paper) $20-50K (从 PQ-6 推断, 小蒋 paper v0.2)

**给老韩的 PER_ORDER_CAP (MVP 起步):**

| 参数 | 数字 | 单位 | 理由 |
|---|---|---|---|
| `PER_ORDER_CAP_HARD` | **$5,000** | USDC | constexpr, 不可改; 即使 bug 也不能超 |
| `PER_ORDER_CAP_SOFT` | **$2,000** | USDC | config, 只可调低; MVP 起步; 与小袁 gameday $2K 中位 0¢ 滑点对齐 |
| `MARKET_EXPOSURE_PCT` | **2%** | of bankroll | 沿用老韩 §7 建议 |
| `KELLY_FRACTION` | **0.25** | ratio | 1/4 Kelly, MVP |
| `KELLY_FRACTION_WARNING` | **0.125** | ratio | WARNING 状态降一半 |
| `DAILY_LOSS_PCT` | **3%** | of bankroll | 沿用 |
| `CONSEC_LOSS_N` | **5** | int | 沿用 |
| `CONSEC_LOSS_WINDOW` | **20** | int | 沿用 |
| `SAFETY_BUFFER` | **5%** | of bankroll | 沿用 |
| `FILL_RATE_FLOOR` (小肖 §4.2) | **0.50** | ratio | 沿用小肖 |
| `MAX_SLIPPAGE_TICKS` (小肖) | **3** | ticks | 沿用 |
| `RHO_MAX` (小肖) | **3.0** | ratio | 沿用 |

**调整规则 (我签字):**
- PER_ORDER_CAP_HARD 在生产 binary 是 constexpr, 修改 = 重 build + 双人签
- PER_ORDER_CAP_SOFT 在 config, 但**只可调低不可调高** — RM v0.2 §7 "只可调低" enforce
- MVP 跑 4 周后, Sharpe > 1.0 + DD < 50% 预期, 可申请 SOFT 调到 $3,000 (HARD 仍 $5K)
- M5 后实盘起步用 SOFT × 0.5 = $1,000 (小蒋 paper v0.2 PR-2 风险缓解)

**给老韩**: 这是 6/4 deadline 的 PER_ORDER_CAP 数, 我现在交付. 老韩 §7 参数表 v0.2 我会签 PR-1.

### 2.5 小蒋 paper engine 共享生产路径 (R-11 单 binary multi-mode) — 我 Agreed?

**Agreed, 但加一条金融红线.**

小蒋 v0.2 paper + backtest 全 C++ + 三 mode 同 binary, R-11 落地, 我从金融视角看:
- PR-1 (paper = live 同 binary 同 mode flag): **绝对支持**. 金融上, paper vs live 的偏差只允许来自 fill backend, 不允许来自 RM 配置 / 信号 / Kelly 计算 / slippage 模型差异. 小蒋 §2.2 列出三处刻意不同 (D-1 signer / D-2 RM config / D-3 ledger), 我看过, 不影响金融正确性.
- PR-2 (paper 必跑 RM 同一份): 必须. 这是金融红线: 任何 "backtest-only RM bypass" 就是欺骗自己.
- BR-5 (回测 slippage 必须用小肖 v1 同一份): 必须. 否则 paper Sharpe 与 live 偏差不可解释.

**加的金融红线 (我提, 给小蒋 v0.3):**
- **R-12 (新): backtest_config / paper_config / prod_config 的 KELLY_FRACTION / EDGE_CI / FILL_RATE_FLOOR / MAX_SLIPPAGE_TICKS 必须 hash 一致.** 小蒋 v0.2 §5.1 已经提了 "红线字段 hash 比对", 我**会签**.
- **R-13 (新): paper PnL vs backtest baseline 比对的 PSD (Paper-Prod Sharpe Deviation) 在 paper 阶段也要算** (小董 §7 给了公式). 小蒋 v0.2 §6.4 已经规划"每日 cron 对比", 但我要 paper 期间也用 PSD 监控自己 (不只 paper vs prod, 也 paper vs backtest_baseline).

**给小蒋**: paper engine v0.2 Agreed, 我会签 PR-1/PR-2/PR-5/PR-7/PR-9. **R-12/R-13 加进 v0.3.**

### 2.6 小董 M4.5 7 hard gate — 我认 G1-G7?

**全认. 一条一条说.**

| Gate | 内容 | 我的立场 | 备注 |
|---|---|---|---|
| G1 (PnL) | sum > 0 且 t-test p < 0.10 单尾 | **认** | 双重判定防单笔暴利堆出来 |
| G2 (Sharpe) | SR_14d > 1.0 且 bootstrap CI 下界 > 0.3 | **强认** | 我特别强调 CI 下界 — 单 Sharpe > 1.0 在 14 天样本下是噪音, CI 下界 > 0.3 才说明不是单日大涨堆的 |
| G3 (Risk) | 风控失效 = 0 | **认** | 这条不议价 |
| G4 (Uptime) | >= 99.5% | **认** | 沿用 GM 红线 |
| G5 (DD) | max_drawdown <= 8% | **认** | 我在 P0-01 §3.6 已经定 8% 阈值, 与小董一致 |
| G6 (Trade count) | >= 50 笔 | **认** | 防 cherry-pick 单笔暴利; 14 天 50 笔 = 日均 3.5 笔, 对 NBA + NFL 联跑现实 |
| G7 (Shadow) | paper > shadow_random, paired p < 0.10 | **强认** | 这是我个人最看重的一条: shadow random-entry baseline 是"行情自然涨大家都赢"的唯一对照 |

**对小董的咨询回复 (OQ-D7 DSR 口径):**
- DSR 用 LdP 2014 论文的 sample skew (n-1 denominator) 和 excess kurtosis (Fisher convention, kurtosis - 3). 这是行业共识, 不要 raw kurtosis.
- 验证用例: Thorp "The Mathematics of Gambling" 1984 例 (Kelly p=0.5 q=0.6 b=1 → f=0.2) 跑通后才算 DSR 实现正确.

**对小董 OQ-D11 (是否允许 1 yellow):**
- **不允许**. 7 个 gate 全部 hard. 1 个 yellow 通过会破坏 multiplicative 严格性 — 一旦开口子, 下次就开两个.
- 但允许"G6 trade_count 不够 → 窗口拉长" (小董 §5.3 已规划), 这不是 yellow, 是窗口延伸.

**对小董 OQ-D13 (Bayesian BLACK → RM kill switch):**
- **支持**. P(μ_i < 0) > 0.3 持续 2 周 = alpha 已经死了, RM kill switch 是正确的. 老韩接口需配合.

**给小董**: M4.5 7 gate 我全签, v1 我会签验收. 加我作为 OQ-D7 / D11 / D13 的决策者.

---

## 3. 给老彭 + 小邓 (我的下游)

### 3.1 老彭

- 你的 Pinnacle no-vig multiplicative 方法 v1 我直接用 (小程 §3.2 已采纳)
- 你的 6 假设我已合并进小程 catalog (P0-01 / P1-04 / P1-05 / P2-09 / P2-10)
- **OQ-9 (multiplicative vs Shin vs power 哪个对 NBA 更准)**: MVP 用 multiplicative, M5 后等小蒋回测 RMSE 数据再决定
- **OQ-D7 DSR 口径**: 我已经回小董, 沿用 LdP 2014 标准

### 3.2 小邓

- ML 路线图 v1 我完全同意立场 — MVP 不上 ML, M5 后是 GBM (no-vig + score_model)
- **数据 schema 需求**: 小邓提的"现在就要做的事"清单, 数据团队 (小余/小段) 必须接, 否则 M5 后回头补字段推迟 6-12 周
- **ONNX 推理路径**: 小蒋 v0.2 已经规划 C++ ONNX Runtime + Treelite, 我支持

---

## 4. 给老雷 GM 的报告

### 4.1 已闭项 (Sprint-1 内)

1. KELLY_FRACTION = 0.25 (1/4 Kelly), 给老韩 v0.2 PR-1
2. PER_ORDER_CAP_HARD = $5,000, SOFT = $2,000 (MVP 起步)
3. P0-01 阈值从 3¢ 改 5¢ (小袁 taker 3% 实测后修正)
4. M4.5 G1-G7 全 hard, 我会签小董 v1
5. paper engine v0.2 R-11 单 binary multi-mode 同意 (小蒋会签)

### 4.2 待 Sprint-2 决议

1. **Pinnacle 路径** (A/B/C) — 老李 / 老彭 6/12 deadline, 决议后我会签
2. **KELLY_FRACTION 松到 0.50** — M5 后 4 周 Sharpe >= 1.0 + DD < 50% 预期才松
3. **多策略 Kelly (correlated bets)** — v0.3 / M6 后做, 现在留接口

### 4.3 我个人最大的担心

**Polymarket taker 3% fee 是 alpha 杀手.** 我之前的 1¢ edge 假设是没扣 fee 的 nominal edge, 扣完 fee + 滑点 + slippage, MVP MVP P0-01 真实 edge 净额 1-2¢ 是 floor, 不是 ceiling. 这意味着:
- M4.5 14 天 50 笔 PnL > 0 在 fee 拖累下, 单笔 edge 净 1¢ × 50 笔 × $2K 单 = $1,000 paper PnL 量级
- 这对 $20-50K virtual bankroll = **2-5% return in 14 days**, 年化 50-130%
- 听上去激进, 实际是因为 Sharpe > 1.0 在低 vol 环境下 (Polymarket 1¢ tick 量化) 数学上就该这么高

**这是 MVP 数字, 不是 long-term return 承诺.** 长期 (12 个月) capacity 受限 (小袁 §3.3 NBA 大场 $20-40K/日累计), M5 后我会给老钱 + 老雷 portfolio 容量模型 v1.

---

## 5. 收尾

Sprint-1 我 owner 的物件:
- xiaoliang-market-structure-v1.md (已交, 小袁实测后修订 v1.1 留 Sprint-2)
- 验收 小程 catalog v1, 小袁 micro v1, 小肖 Kelly v1, 小董 stats v1, 小蒋 backtest+paper v0.2 (我会签)
- 会签 老韩 RM v0.2 PER_ORDER_CAP / KELLY_FRACTION 参数

Sprint-2 我 owner:
- xiaoliang-market-structure-v1.1 (加 小袁实测修订)
- portfolio 容量模型 v1 (给老钱 + 老雷)
- 多策略 Kelly correlated bets 接口预研 (留 v0.3)

**双向收口**: 我对老韩 / 小程 / 小蒋 / 小肖 / 小董 / 老彭 / 小邓 都给出了具体回应 + 数字 + 会签承诺. 没有"看着办". 上对老雷 GM 报告已写在 §4.

我说完了.

— 小梁 (financial-expert, 量化研究部 owner), 2026-05-28
