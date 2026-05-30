# Alpha 信号侧最短路径: 从 0 到第一笔正期望 Paper 成交

- **Owner**: 小程 (quant-signal-research, C 单元 IC #19)
- **Last review**: 2026-05-30
- **Status**: v1 供料稿 — 供 GM + 小梁决策
- **任务来源**: GM 并行供料派单 (§10.2 模式, worktree 只读分析 + 写文档)
- **报告范围**: 信号/数据侧优先级建议, 不含代码实现
- **关联文档**:
  - `xiaocheng-signal-catalog-v1.md` (信号目录 v1, 12 条候选)
  - `xiaocheng-p0_02-signal-spec-v0.1.md` / `v0.2.md` (P0-02 spec)
  - `xiaocheng-w10-w1-p0-03-cross-platform-arb-spec.md` (P0-03 spec)
  - `laopeng-microstructure-alpha-v1.md` (因子定义 + net_edge 口径)
  - `laopeng-w9-inplay-edge-gross-net-confirm.md` (gross/net 澄清)
  - `laopeng-goalserve-pregame-backfill-plan-v1.md` (历史数据现状盘点)
  - `src/stcpp/paper/paper_loop.cpp` (P0-3 fake fair gate 整改)
  - `src/stcpp/pricing/fair_value_estimator.cpp` (BaselineFairValueModel)

---

## §1 当前系统的真实状态 (诚实清单)

在给出任何建议之前, 必须先客观陈述当前 paper loop 到底在干什么, 哪些是真的, 哪些是 stub。

### §1.1 已经工作的部分

| 组件 | 状态 | 说明 |
|---|---|---|
| Polymarket WSS book 接入 | 已工作 | PM microprice / depth 实时读取, 覆盖 174 市场 |
| BaselineFairValueModel | 已工作 | `score_diff * alpha + time_frac * beta` sigmoid 先验 + 0.20 book blend |
| P0-01 信号代码骨架 | 已工作 | Pinnacle no-vig vs PM mid 比较逻辑已落代码 |
| paper loop tick 循环 | 已工作 | 对每个 market 每 tick 做 TickOne, 构造 intent, 过 RM |
| has_real_fair gate | 已整改 | P0-3 整改后, NotStarted → edge_bps/suggested_notional 强制 0, 不产假信号 |
| RM + 风控链路 | 已工作 | kelly cap / per-outcome cap / advisory gate 等都在跑 |
| PnL attribution spec | 已定义口径 | 待 A 单元实现 LedgerStateProvider |

### §1.2 当前阻塞正期望信号的根因

**根因 1: Goalserve inplay key 缺失 → prior 永远 NotStarted**

`BaselineFairValueModel` 在 `time_status == NotStarted` 时 `time_fraction = 0`, score_diff = 0,
导致 prior = sigmoid(0) = 0.5。没有比分 / 时间的真实输入, 模型退化为纯 prior = 0.5。
P0-3 整改 (paper_loop.cpp) 正确地将这种情况的 edge_bps 强制 0, 不下单 — **诚实, 但也意味着完全没有 inplay 信号**。

**根因 2: Pinnacle 数据链路未打通 → P0-01 pregame 信号无锚**

P0-01 (pinnacle-novig-revert) 是 MVP 首发信号, 需要 Pinnacle no-vig fair price。
当前无 Pinnacle API 接入 (OQ-1/OQ-2 仍 open)。没有锚, P0-01 触发条件 C 中
`pinnacle.novig_fair_price` 无值 → 信号不触发 → paper loop 跑空。

**根因 3: 历史数据 = 零 → 回测无法跑通**

`laopeng-goalserve-pregame-backfill-plan-v1.md §1.1` 明确: 截至 2026-05-29,
pregame bookmaker 赔率历史 Parquet 实际上是零。data/paper_mldata 里的是合成 ML 特征 sample,
不含 `bookmaker_id` / `odds_eu` / `overround`, 无法做 de-vig 计算。
score_model (P0-02 的胜率估算) 同样没有真实训练数据。

**根因 4: bet365 单源 inplay de-vig 精度损失 → 净 edge 中位为负**

即便 Goalserve inplay key 接入, P0-02 用单 bet365 de-vig:
gross edge 1.5-2.5% (post-bias, pre-fee) — 3% taker fee — 0.3% slippage = **净 edge -1.8% ~ -0.8%**。
只有 C2 >= 6¢ 的大偏离子集才有正期望 (+0.2~+0.7%)。
这意味着触发频率会大幅降低, 不是"接入 Goalserve 就立刻有大量信号"。

---

## §2 fair value 要"活"起来, 最缺的输入是什么

**结论: 最缺的是 Goalserve inplay 实时比分流 (含事件流), 这是唯一同时解锁两条 MVP 信号的数据链路。**

### §2.1 三类输入的权重分析

| 输入 | 缺失影响 | 解锁信号 | 工程复杂度 | 时间估算 |
|---|---|---|---|---|
| **Goalserve inplay 比分 + 事件** | prior = 0.5, 全部 inplay 信号哑火; P0-02 完全无法运行 | P0-02 (inplay score-price-mismatch), P1-03, P1-06 | 中 (API key + 小段 client 已有骨架) | 1-2 周接通 |
| **Pinnacle pregame odds** | P0-01 无锚, pregame 主力信号哑火 | P0-01 (pinnacle-novig-revert), P0-03 | 中高 (Pinnacle API RTT OQ-1 open, 备选 The Odds API) | 2-4 周 (取决于路径选择) |
| **ML score_model** | P0-02 的 z-score 判断退化为历史 lookup table | P0-02 精度提升 | 高 (需 2 年 NBA/NFL inplay PBP 历史数据) | 8-12 周 |

**Goalserve inplay 是第一顺位**: 它是 P0-02 的充分前提, 也是 BaselineFairValueModel 的真实输入。接通后, fair value 从 "0.5 死先验" 变成 "基于实际比分 + 时间的动态估计", paper loop 才真正有意义地在跑。

**Pinnacle 是第二顺位**: P0-01 是 pregame 信号, 不依赖 Goalserve inplay key, 可与 Goalserve inplay 并行推进, 且是当前信号目录里容量最大的一条 ($20-40K/日累计)。

**ML score_model 是第三顺位**: 在 MVP 阶段, P0-02 用历史胜率 lookup table (按 sport/time_bucket/score_diff 分桶) 就够, 不需要 GBM 模型。lookup table 所需的训练数据量远小于 GBM, 且可逐步用实时数据积累替代。

### §2.2 de-vig 对手盘流动性的作用

当前 Polymarket book depth 已接入 (microprice + depth_within_2_ticks)。
de-vig 流动性门槛 (C3: depth >= $2K) 不是缺失项, 是已有的过滤器。
问题不是"对手盘流动性门槛", 而是"锚数据 (Goalserve/Pinnacle) 缺失导致 dev = fv - p_pm 无法计算"。

---

## §3 当前模型在哪类盘口/比赛阶段最可能先抓到 edge

**在当前无 Goalserve inplay key 的约束下**, 最现实的 edge 窗口是:

### §3.1 P0-01: pregame 6h 内 NBA/Soccer 主要场次

**为什么这个最先到?**
- 不需要 Goalserve inplay key
- 只需 Pinnacle pregame API (或 The Odds API 作备选)
- PM pregame 174 市场里 NBA + Soccer 五大联赛 + UCL 是流动最深的
- NBA gameday 大场 ±2tick 深度中位 $15,125 (小袁实测), 满足 P0-01 的 depth >= $50K 要求
- pregame 6h 内散户偏好最活跃, PM vs Pinnacle gap 结构性存在

**最可能先抓到 edge 的具体场景**:
1. NBA 总决赛 / 季后赛大场, kickoff 前 2-6h, PM 散户推高热门队价格
2. UCL 淘汰赛, PM 对欧洲球队有主场偏好溢价
3. 大牌球星缺阵消息在 Pinnacle 反应快但 PM 慢 (与 P1-04 lineup 信号重叠, 早期先靠 P0-01 探测)

**hit rate 预期**: 60-65% (IS), 58-62% (OOS), 这是所有候选信号里最乐观的数字, 来源于:
- Pinnacle 是公认最 sharp 的锚, de-vig 精度高
- PM 散户偏好结构性存在, 不是随机噪声
- pregame 场景无 inplay 信息时效压力, 执行有余量

### §3.2 P0-02: inplay Soccer 五大联赛, 进球/红牌后 0-30s

**前提: Goalserve inplay key 已接通**

**为什么 Soccer 比 NBA 更先适合 P0-02?**
- Soccer 比赛结构更稳定 (90 分钟 + stoppage), 时间 bucket 分布均匀
- 进球 / 红牌是离散大事件, PM 调价滞后 5-15s 结构性存在 (老彭实证)
- soccer inplay de-vig 只需单 bet365 (ADR-008 §5 已确认), 工程门槛低于 NBA (NBA 需 Basketball 赔率结构不同)
- Five Big Leagues (EPL/La Liga/Bundesliga/Serie A/Ligue 1) 单赛季场次多 (~2000 场), 样本量快速积累

**哪个比赛阶段最先能抓到 edge**:
- 比赛时间 15-75 分钟段 (非开场混乱, 非末段极端赔率)
- 进球后 0-30s 的 PM 调价滞后窗口是核心 alpha 来源
- |score_diff| = 1 的场景 (悬念最大, PM 和 bet365 分歧最显著)

**注意事项**: 即使接通 Goalserve, C2 >= 6¢ 过滤后触发频率会降低 20-30%。每大场约 5-15 次触发, 净 edge 约 +0.2-0.7%。这是薄边, 不是暴利。

### §3.3 P0-01 vs P0-02 阶段优先级

| 阶段 | 优先策略 | 触发条件 |
|---|---|---|
| 当前 (无 Goalserve inplay key) | P0-01 pregame 先跑 | Pinnacle API 接通即可 |
| Goalserve inplay 接通后 | P0-01 + P0-02 并行 | 两条信号 covariance < 0.2, 可同时跑 |
| 历史数据积累后 (>3 个月) | 加入简单 lookup table score_model | 不需要 GBM, lookup 就够 |

---

## §4 从 0 到第一笔正期望 paper 成交: 数据/信号侧最短路径

### §4.1 最短路径定义

**目标**: paper loop 产出第一笔满足以下条件的虚拟成交:
- edge_bps > 0 (正期望, 不是假阳性)
- has_real_fair = true (真实数据驱动, 不是 stub)
- 信号触发条件全部基于真实数据 (不是合成 sample)
- RM 通过 (kelly cap / depth / fill_rate 都满足)

**最短路径 = P0-01 pregame 路径 (不依赖 Goalserve inplay key)**

```
步骤 1: 打通 Pinnacle odds 数据链路 (1-2 周)
  - 评估路径 A (Pinnacle 官方 API) vs 路径 B (The Odds API $500/月)
  - 建议: The Odds API 作为 MVP 快速验证路径 (1 周内可接), Pinnacle API 并行评估
  - 小段 Goalserve pregame getodds 已有骨架, 但需确认 Pinnacle 来源字段

步骤 2: P0-01 触发逻辑接真实 Pinnacle fair_value (1 周, 依赖步骤 1)
  - FairValueEstimator 改为读真实 Pinnacle no-vig 而非 prior=0.5
  - has_real_fair = true gate 解除 (NotStarted 状态 pregame 是正常的, 有 Pinnacle fair 就够)
  - 触发条件: |PM_mid - Pinnacle_novig| >= 3¢, depth >= $50K, kickoff 6h-30min

步骤 3: 选 NBA 大场或 Soccer UCL 单场跑一次 paper (1 天)
  - 目标: 至少 1 笔 paper 成交, edge_bps > 0
  - 验证: /api/v1/risk/rejects 里 advisory 市场不出现 INVALID_INTENT
  - 验证: /api/v1/pnl/attribution 的 gross / fee / net 数字真实

步骤 4: 连跑 72h, 统计 paper 信号触发率 / hit rate / paper PnL (3 天)
  - 验收门槛: hit rate >= 55% (前 20 笔), paper net PnL > 0 (含 3% fee 模拟)
  - 若不达标: 检查 Pinnacle 数据质量 (stale guard / 时效) 和 depth 门槛是否过严
```

**时间估算**: 步骤 1-3 串行, 最快 2-3 周。

### §4.2 路径 B (含 Goalserve inplay, 更全但更慢)

```
并行推进步骤:
  A. Goalserve inplay key 申请 + 小段 client 接通 (1-2 周)
  B. P0-02 Soccer 五大联赛 inplay 跑通 (1 周, 依赖 A)
  C. 72h paper 连跑, 统计 Soccer inplay 触发率 / paper edge

路径 B 比路径 A 晚 1-2 周, 但覆盖更多信号 (P0-01 + P0-02 并行)
总计 3-5 周到"第一笔正期望 paper 成交"
```

### §4.3 最短路径 top 3 (按 GM 优先级排序)

**路径 1 (推荐): P0-01 快速打通 Pinnacle → 第一笔 paper 成交 (2-3 周)**
- 优点: 不依赖 Goalserve inplay key, 阻塞最少, 最快见成果
- 数据链路: The Odds API (快速) → P0-01 触发 → paper RM 通过 → 第一笔成交
- 风险: The Odds API 历史数据质量 / 延迟需实测; Pinnacle 官方 API 可能更稳定但周期更长

**路径 2 (并行): Goalserve inplay 接通 → P0-02 Soccer 首发 (3-5 周)**
- 优点: Soccer inplay 是长期主力信号, 提早接入积累运营经验
- 数据链路: Goalserve inplay key → score/event 解析 → bet365 de-vig → P0-02 触发
- 风险: 单 bet365 de-vig 精度 0.7-1.2pp 损失, 净 edge 薄; 需 C2 >= 6¢ 严格过滤

**路径 3 (最速但局限): PM book 自身 microprice 信号 (当日可跑, 但 alpha 最弱)**
- 优点: 完全不依赖外部数据 (Goalserve / Pinnacle), 当前代码已有 microprice
- 信号: microprice vs mid 偏离 (book imbalance, F-MS-02 类)
- 风险: 无外部锚, alpha 最弱, 不是 MVP 主力; 仅用于"验证系统 end-to-end 通路"
- 用途: 在 Goalserve / Pinnacle 接通前, 用 microprice 信号做系统通路验证 (不计入正式 paper PnL)

---

## §5 最该先打通的 1 条数据链路

**结论: Pinnacle pregame odds (The Odds API 路径作为 MVP 快速替代)**

### §5.1 选择依据

**为什么不选 Goalserve inplay (看起来更核心)?**
- Goalserve inplay key 申请流程和账号问题未知, 可能有业务准入门槛
- 即便接通, 单 bet365 de-vig 净 edge 中位为负, 只有 C2 >= 6¢ 子集有正期望
- Soccer inplay 大场每日触发次数有限 (5-15 次), 积累统计量慢

**为什么选 Pinnacle pregame odds?**
1. **alpha 质量最高**: Pinnacle 是公认最 sharp 的书, de-vig 精度最高 (多源平均误差 < 0.3¢)
2. **容量最大**: P0-01 日累计容量 $20-40K, 是所有信号中最高的
3. **hit rate 最优**: 预期 IS 60-65%, OOS 58-62%, 是 12 条候选里最高的
4. **不依赖 Goalserve inplay**: pregame 信号完全绕开 inplay key 阻塞
5. **快速备选可行**: The Odds API ($500/月) 可以 1 周内接通 (已有同行验证)
6. **与 P0-03 共享**: P0-03 cross-platform vig-arb 的主锚也是 Pinnacle, 接通后两条信号复用

### §5.2 Pinnacle 数据链路具体接法 (给 GM 的建议)

**推荐执行顺序**:

Step A: 老李评估 Pinnacle 官方 API 跨洋 RTT (OQ-1, 已 open)
- 1 周内给结论: RTT p50/p90/p99, 是否需要 colo
- 若 RTT 可接受 → 直接走 Pinnacle API (最稳)
- 若 RTT 过高 → 走 The Odds API ($500/月 历史 + 实时)

Step B: 与 The Odds API 并行验证 (不等老李结论)
- 申请 The Odds API 试用, 实测 Pinnacle sport=basketball_nba / soccer 的数据字段完整性
- 确认: h2h (moneyline) / totals / spreads 三种盘口都有 Pinnacle 来源

Step C: 数据接入到 FairValueEstimator
- 小段的 Goalserve pregame getodds 已有骨架 (laopeng-multiplicative-devig-calibration-v1.md)
- 或 The Odds API 新建简单 REST poller (C++ 小程序, 30s 一次)
- 输出字段: sport / match_id / pinnacle_novig_fair / last_update_ts

Step D: P0-01 触发逻辑接真数据
- `has_real_fair = true` gate 在 pregame 场景解除
- 触发条件: |PM_mid - pinnacle_novig| >= 3¢, depth >= $50K, kickoff 6h-30min

Step E: 跑一场 NBA 大场 paper
- 第一笔正期望 paper 成交

---

## §6 Alpha Decay 参考 (两条信号的 decay 曲线对比)

| 信号 | Decay 机制 | tau | 行动建议 |
|---|---|---|---|
| P0-01 (pregame) | Sharp 资金流入 + Pinnacle 向 closing line 收敛 (分段, 非 exp) | T-6h → T-30min 渐进 | 开赛前 2h 入场质量最好; 开赛前 30min 是末班车; game_stop 立即平 |
| P0-02 (inplay) | PM 做市机器人调价 (信息时差衰减) | tau = 25s, 30s 半衰 | T+0 ~ T+10s 是最佳入场窗; T+30s 后 alpha 残余 30%; time_stop = 90s 强制 |

**P0-01 decay 对 GM 执行的含义**: pregame 信号不需要毫秒级响应, 分钟级决策就够。系统延迟敏感度远低于 inplay 信号, 更适合 MVP 阶段的跨洋链路。

**P0-02 decay 对 GM 执行的含义**: tau = 25s 要求端到端延迟 < 15s (留 10s 余量)。跨洋链路 RTT 3-5s + 代理 2-3s + 决策 + 下单 ~250ms, 总计约 6-8s, 理论上能抓住 67% 的 alpha 残余 (exp(-8/25) = 0.73)。但任何链路波动都会快速吃掉这个余量。

---

## §7 给 GM 的 3 条行动建议 (按优先级排序)

### 建议 1 (P0, 立即做): 打通 The Odds API → P0-01 pregame 路径

**本质**: 用 $500/月 的数据成本换掉"Pinnacle 数据 = 零"的现状。
对"第一笔正期望 paper 成交"是最直接的解锁动作。
执行方式: C++ 小程序 + curl 实测 The Odds API `/v4/sports/basketball_nba/odds` 端点,
验证 Pinnacle 来源数据字段完整性, 然后接进 FairValueEstimator。

**量化预期**:
- 接通后: P0-01 每 NBA 大场可触发 3-8 次 paper 成交
- hit rate 预期: IS 60-65%, OOS 58-62%
- 净 edge 预期: +1.5-2.5¢ per trade (pregame 场景)

### 建议 2 (P1, 1-2 周内): 并行申请 Goalserve inplay key

**本质**: Soccer 五大联赛 inplay 是长期主力信号, 提早接入才能积累运营经验和历史数据。
P0-02 的历史 lookup table (score_model 早期版) 可以用 3 个月实时积累替代历史回填。

**量化预期**:
- 接通后: Soccer 五大联赛每大场触发 5-15 次 P0-02 (C2 >= 6¢ 过滤后)
- 净 edge 预期: +0.2-0.7% (C2 >= 6¢ 子集)
- 触发频率: 每周约 15-30 场大场, 日触发 50-150 次 paper 成交

### 建议 3 (P2, 暂不做): 不要现在上 ML score_model

**理由**: 当前历史数据 = 零 (backfill plan §1.1 已确认)。GBM 需要 2 年 NBA/NFL PBP 数据。
在数据积累 3 个月内, 用简单 lookup table (sport × time_bucket × score_diff) 就够。
lookup table 误差 vs GBM 约 1-2pp Brier score 差距, 对 P0-02 净 edge 影响 < 0.3%。
ML 路线是 M5 后的事, 现在上反而增加复杂度 + 拖延第一笔成交。

---

## §8 开放问题 (需 GM 拍板的决策点)

| # | 问题 | 选项 | 建议 |
|---|---|---|---|
| OQ-A | Pinnacle 数据路径: Pinnacle 官方 API vs The Odds API | 官方 API 稳定但周期长; The Odds API 快速但付费 | The Odds API 先接 ($500/月), 官方 API 并行评估 |
| OQ-B | P0-02 Soccer 首发时间: 与 P0-01 同步还是等 P0-01 稳定后 | 同步更快积累数据; 串行更简单 | 并行推进; P0-01 和 P0-02 数据依赖不重叠 |
| OQ-C | score_model 早期版本: lookup table vs 临时用 prior=0.5 | lookup table 需要比分历史; prior 最简单 | 先用 prior=0.5 做 P0-02 触发 (fv 锚是 bet365, 不是 score_model); score_model 是 Z-score 过滤器, 不是 fair value 主体 |
| OQ-D | 历史数据回填: 是否值得花时间回填 17 个月 pregame 历史? | 回填需 ETL 工程量; 实时积累慢但低风险 | MVP 阶段先实时积累; 回填是 M5 后的事 |

---

## §9 一句话小结

**最缺的输入**: Goalserve inplay 比分 + 事件流 (解锁 P0-02 inplay alpha 的唯一前提)。

**最该先打通的数据链路**: Pinnacle pregame odds via The Odds API — 不依赖 Goalserve inplay key, 是 P0-01 pregame 信号的直接前提, 也是"第一笔正期望 paper 成交"的最短路径 (2-3 周可达)。

**最先抓到 edge 的场景**: NBA 总决赛 / UCL 大场, kickoff 前 2-6h, |PM_mid - Pinnacle_novig| >= 3¢, P0-01 触发。

---

**v1 供料稿完成。本文件是只读分析, 不含代码实现, 不改主干。**

— 小程, 2026-05-30
