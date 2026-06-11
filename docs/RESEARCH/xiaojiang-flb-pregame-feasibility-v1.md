# 赛前 FLB / CLV 策略可行性研究 v1

- owner: 小蒋 (quant-backtest, C 单元 #20)
- last_review: 2026-06-10
- 状态: 研究产出 (ad-hoc, 非正式 framework)
- 关联:
  - `laopeng-goalserve-pregame-backfill-plan-v1.md` (数据缺口 SSOT)
  - `laopeng-bookmaker-history-backfill-v1.md` (历史数据规范)
  - `laopeng-w9-inplay-edge-gross-net-confirm.md` (成本口径)
  - `experiments/xiaojiang-price-bucket/price_bucket_analysis.py` (价区分析)
  - 记忆: `flb-favorite-edge-2026-06-09`, `inplay-directional-dead-2026-06-09`
- 任务来源: GM 2026-06-10, 赛前 FLB/CLV 证据驱动研究

---

## 0. 核心结论 (先行)

**Verdict: 赛前 FLB net of fee 在理论上 +EV，但无历史数据可实证。推荐「先采数据再建策略」路径，不推荐在数据空白下直接上线。**

| 维度 | 结论 | 证据强度 |
|---|---|---|
| 成本结构是否有利 | 是。赛前 buy-and-hold 只付 1 次 entry fee (~0.68% at p=0.65)，无 churn | 理论确定 |
| >=3% gross edge 是否 net +EV | 是。net EV = gross_edge - 0.03*p*(1-p)，3% edge at p=0.65 => net +2.32% | 理论确定，无实证 |
| >=5% edge 桶是否 net +EV | 是。net EV ≈ +4.32% at p=0.65 | 理论确定，无实证 |
| <3% edge 桶是否 +EV | 是（任何 gross_edge ≥ 0.68% 均 net +EV），但 breakeven 很低，实际 devig 误差可能吃掉 | 理论确定，实际不确定 |
| 有无历史回测验证 | **无。** 历史 Parquet 实际为零，71% 胜率数字出处存疑 | 数据缺口 P0 |
| 到 +200u 的容量 | 保守估算 5-20 天，但建立在「每天 5-20 个 >=5% edge 机会」的未验证前提上 | 估算，无实证 |
| CLV 可达性 | 结构上可达（赛前市场向 fair value 收敛），但无 PM 价格历史不可计算 | 推断，无数据 |

---

## 1. 数据可用性 — 现状盘点

### 1.1 现有数据

| 数据集 | 内容 | 是否可用于 FLB 回测 |
|---|---|---|
| `data/paper_mldata/**/*.parquet` | 合成 stub。feat_04 范围 0.30-0.70 (SEED=42 均匀分布)，settlement_outcome 是 5 档枚举非真实 binary outcome | **不可用** |
| `data/ml_capture/quotes.jsonl.*` | 空文件 (0 行)。2026-05-30 采集窗口的 quote snapshot 无结算数据 | **不可用** |
| `ml_research/data/training_sample.jsonl` | Paper daemon 实时快照 (2026-06-02)，无 settlement_outcome | **不可用** |
| Goalserve pregame getodds | 已接实时拉取，但历史快照: **路径 A (API 历史回拉) 20-35% 可行性，未实测；路径 B (The Odds API $500/月) OQ-14 截至 2026-05-29 申请中，批复状态未确认** | 实时可用，历史未知 |

**关键结论 (来自 laopeng-goalserve-pregame-backfill-plan-v1.md §1.1):**

> 截至 2026-05-29，pregame bookmaker 赔率历史 Parquet 实际上是零。

### 1.2 「42万回测」数字出处

记忆提及「42万回测存在 + 验过≥5%低估桶77%胜率」。**经全库检索，未找到对应文档。** 老彭 W9 文档 (laopeng-w9-inplay-edge-gross-net-confirm.md §1.1) 的 77% 未在该文档出现；老彭 W8 WQ-P02-3 ack 的数字是「C2≥6¢子集 gross ~4% net +0.2%」，是**理论估算**不是历史回测。

记忆中「flb-favorite-edge-2026-06-09：min_open_fair=0.5 胜率49%→71%净正」——这是 2026-06-09 策略会议结论，疑似基于现有 paper 运行或推算，不是历史走forward回测数字。

**数据可用性 verdict: 无任何历史 PM 开盘价/结算价数据。任何「回测数字」均为理论估算。**

---

## 2. 赛前 FLB 净 EV 分析

### 2.1 成本结构

赛前 buy-and-hold 策略的费用结构与 in-play 有本质差异：

**In-play (已 DEAD):**
- 入场 fee: `0.03 × p × (1-p)` ≈ 0.68% (p=0.65)
- 平仓 fee (taker): 同上 0.68%
- 合计 round-trip: **1.36%**
- churn 效应: 若 gross edge = 2%，round-trip fee/edge = 68%，实测 churn 吃 75% realized

**赛前 FLB buy-and-hold:**
- 入场 fee: `0.03 × p × (1-p)` ≈ 0.68% (p=0.65)
- 结算: PM 自动结算，**免 fee** (无平仓 taker 成本)
- 合计: **0.68%**
- fee/gross edge (5% edge): 只有 13.5%

**关键对比: 赛前策略的成本效率比 in-play 高 2倍 (0.68% vs 1.36%)，且无 churn 磨损。**

### 2.2 各价位 × 各 edge 桶的 net EV

公式: `net_EV = gross_edge - 0.03 × p × (1-p)`

| PM 价格 p | entry fee | edge=2% | edge=3% | edge=5% | edge=7% | edge=10% |
|---|---|---|---|---|---|---|
| 0.50 | 0.0075 (0.75%) | +1.25% | +2.25% | +4.25% | +6.25% | +9.25% |
| 0.60 | 0.0072 (0.72%) | +1.28% | +2.28% | +4.28% | +6.28% | +9.28% |
| 0.65 | 0.0068 (0.68%) | +1.32% | +2.32% | +4.32% | +6.32% | +9.32% |
| 0.70 | 0.0063 (0.63%) | +1.37% | +2.37% | +4.37% | +6.37% | +9.37% |
| 0.75 | 0.0056 (0.56%) | +1.44% | +2.44% | +4.44% | +6.44% | +9.44% |

**所有格均为 +EV。** Break-even gross edge 只有 0.56%-0.75%（取决于价位）。

### 2.3 边界条件和实际限制

net EV 公式看起来很好——但这里有一个**关键前提**：`gross_edge = fair_value - pm_price > 0` 必须是真实且可靠的。

**问题：gross_edge 的质量取决于 sharp 源的精度。**

老彭 W8 W2 ack 确认：
- bet365 单家 devig 精度损失: 0.7-1.2pp
- 8家 bookmaker 均值 devig 精度: ±0.5-1.2%

若实际 devig 误差 = 1%，那么「3% gross edge」实际只有 2%，fee 后 net = +1.32%，仍然 +EV 但薄。若 devig 误差 = 2%，3% gross edge 实际只有 1%，fee 后 net = +0.32%，接近无利。

**因此赛前 FLB 的真正 breakeven 是：gross_edge > devig_误差 + 0.68% = 约 1.5-2.5%。**

这意味着 >=3% edge 桶是安全的（即使 devig 损失 1pp），<3% 桶在 devig 误差下可能为负。

### 2.4 「churn fee 吃 75% realized」的成因重新审视

记忆显示当前 paper 中 churn 手续费吃掉 75% realized。这主要来自 in-play 频繁平仓的 round-trip fee。

赛前 buy-and-hold 策略从结构上消除了这个问题：持仓到结算，无需平仓，单次 entry fee 约占 gross_edge 的 13.5%（5% edge 场景），而非 75%。

---

## 3. CLV 可达性

### 3.1 CLV 定义与重要性

CLV (Closing Line Value) = 你的入场价与赛前收盘线的差值。正 CLV 是 sharp 选手的黄金标准（Pinnacle model 的共识）：若你的入场价系统性好于 PM 赛前收盘中价，说明你捕捉到了真实信息优势。

### 3.2 赛前 PM FLB 的 CLV 结构

赛前 FLB 能否系统性获得正 CLV，取决于：

1. **PM 赛前市场向 fair value 收敛的速度**：若市场在开盘时低估 favorite，随着时间推移，信息逐渐流入，价格向 fair value 收敛，早期入场者 CLV > 0。这是 FLB 逻辑的核心假设。

2. **Polymarket 体育盘的特殊性**：PM 体育盘主要由散户参与，做市方是算法做市商。散户存在 longshot bias（高估低赔率盘），这在传统博彩中已被大量文献证实（参见 Thaler & Ziemba 1988, Snowberg & Wolfers 2010）。PM 是否有同样偏差——**未知，无 PM 历史价格数据验证**。

3. **CLV 的可测性**：需要 PM 开盘价 → 收盘价的历史快照配对。目前**完全没有这个数据**。

### 3.3 CLV 与 Goalserve 2.3s 延迟的关系

**重要区分**：赛前 CLV 与 in-play 逆选是两个不同的问题。

- in-play 逆选 DEAD 的根因：Goalserve 赔率滞后 bet365 P50=2.3s，追移动靶，物理不可修。
- 赛前 CLV 完全不依赖 Goalserve inplay 实时赔率——用 Goalserve pregame getodds（赛前 -6h 批量），没有实时延迟问题。赛前策略的 alpha 窗口是「小时/天」级别，不是「秒」级别。

**所以 in-play DEAD 的结论不适用于赛前 FLB。两者根因完全不同。**

---

## 4. 容量 / 流动性估算

### 4.1 当前系统对赛前 sharp 源的覆盖

记忆「8/170 市场有 sharp 源」针对的是 in-play 系统（需要 Goalserve inplay 实时赔率）。赛前策略使用 Goalserve pregame getodds（已接），覆盖更广：

- Soccer: 英超/西甲/德甲/意甲/法甲 + 各国联赛
- Basketball: NBA
- Tennis: ATP/WTA
- 老彭估算：2 年 ~68,500 events，但历史 Parquet 为零

**赛前可操作盘口数量未知**，需要实时采集后才能估算。

### 4.2 数量级容量估算（条件性）

假设条件：每天有 N 个 >=5% edge 机会（favorite p=0.65），每笔 $50，net EV = 4.32%：

| 每日机会数 | 每日 EV | 到 +$200 需天数 | 到 +$200u 年化 |
|---|---|---|---|
| 5 机会 | $10.8 | 19 天 | 年内可达 |
| 10 机会 | $21.6 | 10 天 | 年内可达 |
| 20 机会 | $43.2 | 5 天 | 月内可达 |

**前提：每天有 N 个 >=5% edge 的 favorite 机会。** 这个 N 完全未知——需要实际采集 Goalserve pregame getodds + PM 现价数据，观察真实 edge 分布。

### 4.3 容量上界约束

PM 体育盘流动性约束：
- 近 even 盘 (p≈0.5) NBA/MLB gameday 深度 $15,125 (小袁实测)
- 赛前 -6h 以上流动性通常低于 gameday
- 每笔 $50-200 在 MLB/NBA 主流盘是可行的，不会明显滑点
- 小市场（冷门赛事）深度可能 <$1000，边际可下注额很小

---

## 5. 关键数据缺口

| 缺口编号 | 内容 | 严重度 | 现状 |
|---|---|---|---|
| X1 | 无 PM 赛前开盘价/收盘价历史数据 | P0 | laopeng §1.1: 历史 Parquet 为零 |
| X2 | 无赛前结算配对 (settlement_outcome 是合成) | P0 | paper_mldata 是 stub，不可用 |
| X3 | Goalserve pregame getodds 历史回拉可行性未验证 | P0 | 路径 A：20-35% 可行性，小段未实测 |
| X4 | The Odds API $500/月 OQ-14 批复状态未知 | P0 | 路径 B：老彭 2026-05-29 提案，截至今日状态未知 |
| X5 | 71%/77% 胜率数字无源头文档 | P1 | 疑似策略会议推算，非历史回测 |
| X6 | PM 体育盘 favorite-longshot bias 是否存在未验证 | P1 | 传统博彩文献支持，但 PM 预测市场结构不同 |
| X7 | 赛前 PM 价格 opening→closing 收敛动态未知 | P1 | 无 PM 赛前价格时序历史 |
| X8 | 每天实际 >=5% edge 机会数量未知 | P1 | 需要实时采集验证 |

---

## 6. 风险案例

### R1: FLB 在 PM 预测市场不存在

传统博彩 FLB 由散户 longshot bias 驱动（高估冷门，低估热门）。Polymarket 的参与结构不同——有 sharp 做市商、套利者持续修正偏离。若 PM 市场足够高效，favorite 不会系统性被低估，整个策略前提不成立。**无历史数据无法验证。**

### R2: devig 误差吃掉 edge

若 8 家 bookmaker 均值 devig 精度 ±1.2pp（最差情况），那么 <3% gross edge 桶实际 net EV 接近 0 甚至为负。策略需要设定 >=3% 的 gross edge 门槛来保证安全边际。

### R3: Goalserve pregame getodds 无历史深度

老彭 §2.1 确认：`&ts=` 历史回拉是否真正工作未验证，服务端是否保留 17 个月历史快照未知。若路径 A 和路径 B 均失败，回测时间线推迟 3-4 个月（需从今日起实时录制积累数据）。

### R4: PM 赛前流动性不足触发滑点

非主流赛事（冷门网球、低级别足球）深度可能 <$500，$50 单笔也会有明显滑点，实际 net EV 低于理论。策略应限制在 NBA/MLB/五大联赛足球/ATP 主赛的深度市场。

### R5: 容量天花板低于预期

若每天实际 >=5% edge 机会只有 2-3 个（非 5-20 个），+200u 需要 2-3 个月，年化收益 <$1000，性价比不划算。这个 N 完全需要实际采集才知道。

---

## 7. Sensitivity 分析

### 7.1 devig 误差 × gross edge 桶的 net EV 矩阵

`net_EV = gross_edge - devig_err - fee`  (fee ≈ 0.68% at p=0.65)

| gross_edge | devig_err=0.5% | devig_err=1.0% | devig_err=1.5% | devig_err=2.0% |
|---|---|---|---|---|
| 2% | +0.82% | +0.32% | -0.18% | -0.68% |
| 3% | +1.82% | +1.32% | +0.82% | +0.32% |
| 5% | +3.82% | +3.32% | +2.82% | +2.32% |
| 8% | +6.82% | +6.32% | +5.82% | +5.32% |

**结论：>=3% edge 桶在最差 devig 误差（2%）下仍 +EV（+0.32%）；>=5% 桶在任何 devig 误差场景下均稳健 +EV。**

### 7.2 参数敏感性

| 参数 | 基准值 | 敏感方向 | 影响 |
|---|---|---|---|
| 每日机会数 N | 未知 | 若 N<3，年化 EV 极低 | 容量决定策略价值 |
| 平均 gross edge | 5% | 若实际 <3%，devig 误差影响大 | 质量决定收益 |
| PM 赛前流动性 | $1K-$15K | 低流动性市场滑点显著 | 影响单笔可下注额 |
| FLB 是否真实存在于 PM | 未知 | 若不存在，策略无 alpha | 最大风险 |

---

## 8. 走到 +200u 的路径

**前提条件 (必须全部满足):**
1. FLB 在 PM 体育盘确实存在 (散户 longshot bias)
2. >=5% edge 机会每天 >=5 个（需实测）
3. Goalserve pregame getodds 提供可靠 fair value
4. 流动性足够支持每笔 $50+

**数量级 (conditional):**

- 在最保守假设（5 机会/天，$50/笔，4.32% net EV）下：每日 EV = $10.8，19 天到 +$200u
- 在乐观假设（20 机会/天，$200/笔）下：每日 EV = $172.7，2 天到 +$200u
- **但这些数字完全建立在未验证的前提上**

---

## 9. 推荐行动路径

### 9.1 优先级 1：立刻开始实时数据采集（低成本快速验证）

不需要历史数据，也不需要等 OQ-14 批复：

**行动**：在现有 Goalserve pregame getodds 拉取的基础上，开始记录每日「赛前 -6h 时刻的 fair_value vs PM 现价」配对，持续 2-4 周。

**验证目标**：
- 每天有多少 favorite (p>0.5) 被低估 >=3% / >=5%？
- 平均 gross edge 分布是什么？
- 赛后 settlement 收集（PM gamma API 有结算接口）

**时间成本**：低。使用已有基础设施，不改生产代码，只需要一个数据记录脚本（非热路径，Python notebook 可行）。

**2-4 周后即可判断**：FLB 在 PM 是否真实存在，每日机会数，边大小。这是整个策略是否值得建的最快、最便宜的验证路径。

### 9.2 优先级 2：路径 A/B 历史数据（精细回测用）

路径 A（小段 6-05 验证 Goalserve 历史回拉）和路径 B（The Odds API OQ-14）是走向正式 walk-forward 回测的必要前提。但这是 6-12 的截止线，且即使数据到位，正式回测框架（C++ backtest framework v0.2）的第一份报告也要 2026-07-16 才出。

**对于 +200u 的近期目标，路径 1（实时采集验证）更快。**

### 9.3 不推荐：在无数据支撑下直接 paper 运行 FLB

记忆显示 in-play 因无 alpha 导致 paper realized −$20+。赛前 FLB 的结构比 in-play 好得多（无 2.3s 延迟问题、无 churn），但 **FLB 是否真实存在于 PM 仍是未验证假设**。建议先用 2-4 周实时观测确认 edge 频率和大小，再决定是否在 paper 环境运行。

---

## 10. 一句话推荐

**值得建，但先验证。** 赛前 FLB 的成本结构比 in-play directional 好得多（fee 负担仅 13.5% vs 68%，无 churn，无 Goalserve 2.3s 延迟问题），理论 net EV 在 >=3% edge 桶下即使最差 devig 误差也为正。但关键前提「PM 体育盘 favorite 确实被系统性低估 >=3-5%」尚无历史数据验证。**最快验证路径：2-4 周实时采集「赛前 fair_value vs PM 价格」配对 + 赛后结算对照**，成本极低，可在现有基础设施上用 notebook 完成。若 2-4 周后确认 >=5 个/天的 >=5% edge 机会，立即建 paper 运行；若确认不存在或太少，节省了建错误策略的成本。

---

*— 小蒋 (quant-backtest, #20), 2026-06-10*
*上报小梁 (C 主管) first review*
