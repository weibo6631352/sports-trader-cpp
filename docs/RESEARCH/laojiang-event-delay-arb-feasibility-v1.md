# 事件延迟套利可行性评估 v1

**owner:** 老姜 (microstructure, A 部)
**last_review:** 2026-06-10
**召唤背景:** GM halt in-play directional paper 后，评估「事件延迟套利」在当前 infra 下是否可行，是否值得建。
**证据基线:** shorthorizon-locking-feasibility-v1.md + quant-microstructure-eventdelay-arb-feasibility-v1.md + laolei-gs-latency/ 实验 + laolei-pm-inplay-no-edge-decision-2026-06-04.md + laolei-freesource-latency/FINDINGS.md

---

## 一句话裁决

**有条件的边际可行，但前提没有全部满足，不能现在就建。**

工程链路延迟 (~3.1s) vs PM 重定价延迟 (中位 13.2s) → 理论窗口 ~10s 存在。
但这个窗口不等于 edge：Goalserve 相对 PM 的真实 lead 是正数，但能否在有流动性的盘口稳定兑现，取决于两个命门——而这两个命门当前都没有实测数据支撑。

---

## 1. 范式区分：事件延迟套利 ≠ in-play directional

在做最终裁决之前，必须严格区分：

| 维度 | In-play directional（已 DEAD）| 事件延迟套利（本文评估）|
|---|---|---|
| 信号来源 | Goalserve 赔率 vs PM mid 的持续偏离 | 离散比赛事件（进球/得分/红牌）发生后 PM 重定价滞后 |
| 死因 | Goalserve 滞后 bet365 2.3s，我们追「移动靶」时靶已离开 | 另一回事：PM 重定价本身就比 Goalserve 分慢 13.2s |
| 信号类型 | 连续追踪 | 单次事件触发 |
| 关键延迟对比 | 我们的信号延迟 vs PM 的反应延迟 | **Goalserve 得到事件 vs PM 完成重定价** |

**结论：in-play directional 的死因（Goalserve 落后 bet365 2.3s 追不上 PM）对事件延迟套利不是直接命门。** 事件延迟套利的命门是：在离散事件发生后，我们感知到事件的时刻，PM 是否已经完成重定价。

---

## 2. 核心证据链

### 2.1 PM 重定价速度（实测，shorthorizon-locking-feasibility-v1.md §C2）

数据：322,880 行 FV 快照，6,630 个比分变化事件，20.5h 跨度（2026-05-31/06-01）

| PM 重定价 | 时间 |
|---|---|
| 最快观测 | 2.4s |
| p25 | 8.5s |
| **中位** | **13.2s** |
| p75 | 20.5s |
| p90 | 32.0s |
| 无可观测重定价（120s 内 < 2c 移动）| 43.5% 的事件 |

**关键含义：** 事件发生后，PM 中位 13.2s 才完成重定价（≥2c 移动）。有 35.3% 的事件在 5-10s 区间完成，仅 0.3% 在 5s 内完成。这是机会窗口的来源。

### 2.2 我方端到端延迟（实测，quant-microstructure-eventdelay-arb-feasibility-v1.md §1）

| 延迟段 | 值 |
|---|---|
| Goalserve 服务端数据刷新间隔（实测三运动）| 中位 2.015–2.023s |
| 我方轮询等待（1s 间隔，均值）| 500ms |
| Goalserve HTTP 全程（EC2 伦敦）| 77ms |
| 解析 + Publish | ~5ms |
| paper_loop tick 等待（当前 500ms）| 0–500ms |
| CLOB POST（新建 TLS）| 60ms |
| **端到端中位** | **~3.1s** |
| **端到端最差** | **~4.5s** |

**vs PM 重定价的余量：**

| 情景 | 我方延迟 | PM 重定价 | 窗口余量 |
|---|---|---|---|
| 中位 | 3.1s | 13.2s（中位）| **+10.1s** |
| 最差 | 4.5s | 8.5s（p25）| **+4.0s** |
| 极端最差 | 4.5s | 2.4s（最快）| **-2.1s（追不到）** |

### 2.3 够本移动的频率和条件（shorthorizon §B §C）

| 条件 | H=60s >BE 比例 | 中位 BE |
|---|---|---|
| 极端价位（mid<0.15 / >0.85）| **9.3%** | 1.7c |
| 中间段（0.15–0.35 / 0.65–0.85）| 5.9% | 3.8c |
| 近平（0.35–0.65）| 1.4% | **6.45c（不可行）** |

够本移动中 **70%+ 发生在比分事件 10s 内**，80% 在 30s 内（§C1）。这不是噪声游戏，是事件驱动的。

### 2.4 PM live 盘覆盖（现有系统 + memory [pm-live-inplay-esports-only]）

- PM 传统体育多为 futures，**真正 liquid in-play 单场盘主要是电竞（CS2/LoL）**
- 已匹配盘口：系统实测约 227/420（54%）匹配到 Goalserve event，其中真正有 sharp 源的约 8/170（4.7%，做市裁决实测）
- 传统体育 in-play 事件套利候选：真实候选池 **约 10–50 个 market**，不是 400+

### 2.5 免费源探索（laolei-freesource-latency/FINDINGS.md，2026-06-09）

- ESPN 隐藏 API：比分好，**无赔率/fair**
- sofascore：疑似封禁，ToS 风险高
- 结论：**所有聚合 API 都比直接看比赛的人慢**；免费源没有解锁速度优势的路径

---

## 3. 三个核心问题的正式答复

### Q1：PM 重定价一个进球需要多久？

实测中位 **13.2s**，p25=8.5s，最快 2.4s。43.5% 的事件在 120s 内无可观测重定价（市场对该事件不敏感，或无流动性）。

### Q2：我方信号源相对 PM 重定价，领先还是滞后？

**领先，中位约 +10.1s。**

时序对比：
- Goalserve 感知事件（数据刷新）→ 我方端到端完成下单：中位 ~3.1s
- PM 完成重定价（≥2c）：中位 13.2s
- **净领先：约 10s（中位情景）**

注意：Goalserve 本身落后 bet365 约 2.3s，但 **PM 重定价的中位是 13.2s，远慢于 Goalserve 的 2s 周期**，所以相对领先仍然成立。这与 in-play directional 的死因方向完全不同：那里 PM 由「直接看比赛的人」在 ~0.3s 内重定价；这里的「事件后重定价」是散户跟进过程，慢得多。

### Q3：当前 infra 的 latency budget 够吗？

**理论上够，但有三个瓶颈需要改造才能兑现。**

- Goalserve 数据刷新间隔（2s，不可控）
- paper_loop 500ms tick 轮询（可控，改触发器方案）
- CLOB 新建 TLS（60ms，可优化为 25ms 复用连接）

若把 tick 改为 score_change 事件触发器（方案 B，quant-microstructure §4.2），总延迟可降至 ~1.5s，窗口余量扩大到 ~11.7s。

---

## 4. 命门分析

### 命门一：acceptingOrders 连续性（P0 验证，未完成）

profit-scheme-v3-arb-only.md §2 明确列出：PM 进球后是否 `acceptingOrders=true` 是引擎 A 的**生死门**。进球时段 PM 是否 suspend 订单，是工程上完全不可假设的——在有进球的真实比赛场次上必须实测。

**当前状态：未验证。** shorthorizon 报告的 43.5% 事件无重定价，一部分原因很可能就是 PM suspend 期间无法成交。

### 命门二：真实流动性 + 进场深度（near_half 不可行）

- near_half（0.35–0.65）：BE=6.45c，H=60s >BE 仅 1.4%，**明确不可行**
- 极端价位（<0.15 / >0.85）：BE=1.7c，>BE 比例 9.3%，理论上正期望
- 极端价位的流动性约束：深度有限，单次 >$100 成交有难度

### 命门三：Goalserve 状态码事件触发路径（当前零消费）

profit-scheme-v3-arb-only.md §2「地基 P0」：Goalserve `info.state` 事件码（11003 进球/11008 点球/11006 红牌）已抓进 `rec.gs_state_code`，**但下游零消费**。

当前 paper_loop 是时间驱动（500ms tick），无法对进球事件即时响应。事件触发器是整条链路的工程前提，尚未建。

---

## 5. 容量估算（诚实，不画饼）

引用 quant-microstructure §5.3 估算并做独立校正：

| 参数 | 值 | 依据 |
|---|---|---|
| 每场有效 scoring event | 5–20 次 | 运动类型不同 |
| 有 Goalserve 覆盖 + sharp 源的盘口 | ~50 个（保守，真实有 sharp 的 4.7%×~1000 盘）| 做市裁决实测 |
| PM 有流动性的极端价位 condition | ~10–20 个（在上述 50 中）| shorthorizon 样本占比 |
| 每天有效触发（事件×盘口×流动性满足）| **5–20 次** | 保守 |
| 单次期望净值（极端价位，扣 BE=1.7c）| ~0.5–2c | shorthorizon §3.1 |
| 单次执行 size（深度约束）| $50–$200 | 极端价位深度有限 |
| **日期望 PnL** | **$2–$20** | 上述乘积 |

**到 +200u 的路径：** 需要日 PnL 稳定 $5–$20，累积 10–40 天。但这依赖命门一（acceptingOrders）和命门三（事件触发器）都解决，且实际命中率符合历史分布。量级是「小而正」，不是「可规模化的主策略」。

---

## 6. 与现有架构的兼容性

现有 paper_loop 中已有的基础（profit-scheme-v3-arb-only.md §6「脚手架」）：
- arb_signal / sizing / risk（RJ-ARB 门）已建
- OpenLegLedger 强平已建
- EventMatcher 映射已建
- inplay feed 采集已在

**缺的**（事件触发套利专用新建）：
1. `InplayScoreParser` 扩展消费 `info.state` / `stats` 计数（已捕获但零消费，低风险纯加法）
2. `score_change` 事件触发管道（替换 500ms tick 的关键工程）
3. `market parse acceptingOrders`（验证命门一）
4. `score_to_direction` 规则逻辑（进球→哪方 YES 涨，极简规则）

---

## 7. 微观结构视角：为何极端价位是甜区

作为微观结构工程师补充一个量化直觉，解释为何 mid<0.15 / >0.85 是唯一可行窗口：

**Microprice 收敛速度 vs BE 的比值在极端价位最优**

在极端价位：
- 订单簿通常较薄（绝对 size 小），一个进球冲击的 mid 移动幅度相对价位更大（delta 效应）
- Polymarket 极端价位的做市商「保险垫」需求低（接近结算），报价更贴近 fair，spread 仅 1c
- 一个进球可以把一支球队的赢盘从 0.1 推到 0.4，移动幅度远超 BE=1.7c
- 反观 near_half：BE=6.45c，而中位赛事进球对 0.5 附近盘口的冲击通常 < 5c（shorthorizon p90 才 0.5c，p95 才 3c），BE 很难被覆盖

**订单流不平衡（OFI）在事件后的形态：**
进球后极端价位盘口出现的 OFI 是单向的（只有一个方向）、爆发式的。这与 near_half 盘口的「双向博弈」OFI 形态完全不同。极端价位在事件后的「OFI 饱和时间」（单边吃尽）可能是 PM 重定价时钟的真实驱动。我们进场的窗口就是这个单向 OFI 饱和过程尚未完成的阶段。

---

## 8. 对比 in-play directional 的本质区别

| 死亡原因 | In-play directional | 事件延迟套利 |
|---|---|---|
| 信号延迟 | Goalserve 落后 bet365 2.3s，PM 由「看比赛的人」在 0.3s 内重定价 → 我们永远追已重定价的价格 | PM 重定价需要散户跟进，中位 13.2s，我们端到端 3.1s → 有窗口 |
| 是否可克服 | 物理不可克服（两层延迟：GS 中继 + PM 快速效率）| 两个命门都是工程可解的（acceptingOrders + 事件触发器）|
| 根因 | 追移动靶（连续信号），PM 已是高效市场 | 抢离散事件重定价（一次性冲击），PM 散户反应慢 |

---

## 9. 最终裁决

### 可行性

**边际可行，但有条件。**

工程延迟链路不是问题（~3.1s vs PM 重定价 13.2s，余量 ~10s）。不可行的是：

1. 命门一（acceptingOrders）未验证 → 进球时 PM 是否 suspend，完全未知
2. 命门二（极端价位深度）约束容量 → 日期望 PnL $2–$20，不是主力策略
3. 命门三（事件触发器）未建 → 当前 500ms tick 架构无法兑现延迟余量

### 相对 in-play directional 的状态

| 维度 | 状态 |
|---|---|
| 范式有效性 | **不同范式，不受 in-play directional 死因污染** |
| 理论窗口 | **存在（实测数据支撑）** |
| 命门验证 | **未完成（P0 尚未跑）** |
| 工程前提 | **未建（事件触发器 + acceptingOrders 解析）** |
| 容量 | **小（日 $2–$20，到 +200u 需数十天）** |

### 推荐行动

**先解命门一，再决定是否建工程。**

具体：
1. **P0 验证**（不写热路径，测试组观测）：跑几场真实有进球的比赛，记录进球前后 PM `acceptingOrders` 状态。若进球期间 suspend > 10s → 事件套利实际窗口被大幅压缩，可能不可行。
2. **若命门一通过**：建 `InplayScoreParser` 扩展（低风险纯加法）+ `acceptingOrders` 解析，配合极端价位过滤，跑 paper 验证实际 hit rate
3. **事件触发器**（命门三，中等工程量）：仅在命门一通过且 paper 有正向信号后才建

### 一句话推荐

**事件延迟套利是当前唯一有实测数据支撑、且不受 in-play directional 死因污染的 in-play 策略。去做 P0 验证（不写代码），命门一决定这条路是否继续。容量上限是小而正（日 $2–$20），不是主要收入来源，是补充策略。**

---

## 附录：关键数字速查

| 参数 | 值 | 来源 |
|---|---|---|
| PM 重定价中位 | 13.2s | shorthorizon §C2 |
| PM 重定价 p25 | 8.5s | 同上 |
| 我方端到端中位 | 3.1s | quant-microstructure §1 |
| 净窗口余量（中位）| ~10.1s | 两者之差 |
| 极端价位 BE | 1.7c | shorthorizon §B2 |
| 极端价位 H=60s >BE 比例 | 9.3% | 同上 |
| Goalserve 落后 bet365 | 2.3s（P50）| gs-bet365-latency-2300ms memory |
| PM 由看比赛者重定价（in-play directional 的世界）| ~0.3s | no-bet365-pm-convergence-edge memory |
| PM 事件后散户跟进重定价（本策略的世界）| 中位 13.2s | shorthorizon 实测 |
| 有 acceptingOrders 数据的 P0 验证状态 | **未完成** | profit-scheme-v3 §2 |
| 日期望 PnL（估算）| $2–$20 | 本文 §5 |
