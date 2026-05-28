# W5 末 架构 Challenge 投票 — 小梁 (C 主管, financial-expert)

- Owner: 小梁 (E-018, C 单元 Manager)
- Date: 2026-06-01 (W5 末, Wave 27 架构 challenge 投票会)
- Meeting: ADR-009 v2 = Sonnet 架构 challenge 投票
- Last review: 2026-06-01
- 约束: 主管 cpp 行数 = 0 守住 / 数字说话 / 不替别人表态

---

## §1 5 议题投票

### 议题 1: paper/live 共享 binary (R-2)

**投票: A (共享 binary)**

量化理由:

我在 Sprint-1 retro §2.5 (R-12/R-13 新增) 和 ADR-003 GM-1 (三签解锁含我) 都依赖"paper RM 配置与 live 完全相同"这一前提. 如果分叉, 以下三条数学性质失效:

1. Paper Sharpe 与 live Sharpe 的 **PSD (Paper-Prod Sharpe Deviation)** 无法解释来源. 分叉后的偏差可能是策略 alpha 差异, 也可能是 KELLY_FRACTION / FILL_RATE_FLOOR / MAX_SLIPPAGE 配置差异 — 两者不可区分, PSD 监控失去意义.
2. M4.5 7 hard gate (小董 G1-G7) 的"G7: paper > shadow_random paired p < 0.05"是基于 paper 与 live 同一份信号路径和 RM 逻辑, 分叉后 G7 检验的不是同一分布下的 null.
3. 我对老韩 RM v0.2 PR-1 签字时明确写: **"任何 backtest-only RM bypass = 欺骗自己"**. 共享 binary 是这条金融红线的工程实现, 分叉即破线.

反对选 B 的理由: 分叉 binary 的好处通常被表述为"backtest 跑得更快 (省 RM 开销)". 但 RM p99 ~100us, backtest 批量跑时的瓶颈在磁盘 IO 和 feature 重算, 不在 RM. 省的是 noise, 丢的是数学正确性. 不对称.

**条件**: R-12/R-13 (KELLY_FRACTION + FILL_RATE_FLOOR 等红线字段 hash 一致) 必须在 CI 中 enforce, 否则共享 binary 只是 build-time 形式合规.

---

### 议题 2: WSS 拓扑

**投票: C (2×4)**

量化理由:

WSS 拓扑本质是 **信号更新频率 vs 连接故障域** 的权衡. 从我的视角出发:

1. 信号刷新频率: 我的 P0-01 触发条件是 `|fair - poly_mid| >= 0.05`. poly_mid 来自 Polymarket WSS L2Update. 如果连接数过少 (A: 1×8), 单条连接故障导致 poly_mid 过期, 触发 `STALE_MARKET` reject — fair value anchor 仍在更新但无法比对, edge 信号不触发, 隐性 opportunity cost.

2. 连接故障代价 vs alpha: 假设每条 WSS 连接年化故障 H 小时, 覆盖 k 个市场. 故障期间这 k 个市场全部触发 STALE reject. 减少 k (多连接分摊) 直接减少 STALE 窗口期的信号损失. 但连接数过多 (B: 4-5×2) 带来的协议握手开销 + R-12 非阻塞 event loop 的连接管理复杂度上升, 在高延迟跨洋链路下每条连接的 reconnect 开销不可忽略.

3. C (2×4): 2 条主备, 各覆盖 4 市场. 单条故障仅影响 4 市场的 STALE 窗口, 不影响另 4 市场信号. 比 A 故障域小 2x, 比 B 连接管理复杂度低 2-2.5x.

**补充条件**: vCPU pin 确定后, WSS 拓扑的实际 CPU 开销需要老姜出 bench 数字再最终确认.

---

### 议题 3: 跨洋部署

**投票: C (M4.5 前单点, M5+ 多点)**

量化理由:

这是**分阶段决策**, 不是永久单选. 我的逻辑:

1. M4.5 前 (paper 阶段): 部署复杂度是 paper engine 到 live 的**噪声因子**, 不是 alpha 因子. 多点部署引入的数据一致性问题 (两个节点看到的 poly_mid 时间戳可能不同) 会干扰 PSD 监控和 M4.5 7 gate 的统计窗口解释. 单点更干净.

2. M5+ (live 阶段): 老吴 v0.1 已论证"主节点放 us-east-1, RTT < 5ms". 但 live 模式下, 单节点的可用性直接影响 PnL. 北极星 99.9% 在线率 = 年化最多 8.7h 停机. 单点硬件故障 MTBF 通常 < 20000h (2.3 年), 不满足 99.9% 要求. 多点 (同城 us-east-1 主 + Hetzner Ashburn 备) RTO < 5min, 满足.

3. 提前到 M5 之前扩多点的代价: 工程复杂度增加 (两节点状态同步, 尤其是 position WAL + bankroll 一致性), 在 paper 阶段这些不是 bottleneck 问题, 是不必要的 infra debt.

**量化阈值**: M5+ 起步时 bankroll > $20K live 后, 单节点停机 8.7h 对应 expected opportunity cost = $20K × annual_return × (8.7/8760) — 远大于多点部署成本. 触发时机合理.

---

### 议题 4: ML 时机

**投票: A (M4.5 后)**

量化理由:

这是我在 Sprint-1 retro §2.6 对小董 G1-G7 全认后一直维持的立场, 这里给完整数学理由:

1. **M4.5 gate 是 ML 上线的充分前提条件, 不是充分条件**. G2 (Sharpe bootstrap CI 下界 > 0.5) 需要 paper 跑够数据 (G6: ≥50 笔). 在 paper 数据积累到位之前, ML 的 training label 不足以做有意义的模型评估. W6 shadow 或 M2 shadow 的"训练数据量"在统计上不够支撑信号 lift 的置信度.

2. **ADR-008 multiplicative de-vig (我会签) 是 ML 的 fair value 锚**. 若 de-vig 算法本身在 W6-M4.5 期间还在校准 (老彭历史 vig 实证 W6 EOW 才出), 用校准中的 fair_value 作为 ML 的 feature 会引入 label noise, 导致 ML 模型对 P0-01 的 lift 无法与 de-vig 误差解耦.

3. **小邓 ML roadmap v2 的 GBM (no-vig + score_model) 路线**: GBM 的 feature importance 必须在稳定的 fair_value 锚源上才有可解释性. 先锁 de-vig (M4.5 前), 再加 ML (M4.5 后), 是正确的迭代顺序, 与 ADR-008 依赖链一致.

反对 B (W6 shadow) / C (M2 8/6 shadow): shadow 本身我不反对, 但 **shadow 的 PnL attribution 在 M4.5 7 gate 通过前没有意义** — 你不知道 shadow PnL 是来自 alpha 还是来自 de-vig 参数还在漂移. M4.5 后才有 baseline 可比.

**条件**: 小邓 ML-R1 (ML 不进生产决策路径) + ML-R2 (4 stage join 完整测试, 含 stage3/4) 必须在 M4.5 前完成, 否则 M4.5 后 ML 上线仍然有 data pipeline 债.

---

### 议题 5: vCPU pin

**投票: C (M4.5 前 single core)**

量化理由:

vCPU pin 与我的信号计算 latency 相关, 具体是 T3 strategy_engine (signal tick p99 ~500us) 和 T3 同线程的 RM evaluate (p99 ~100us). 从 Kelly sizing 角度:

1. **现阶段 p99 预算**: 信号计算 500us + RM 100us + AuditEmitter (BLAKE3 stub) ~50us = 650us. 在 paper 阶段, 跨洋 RTT (~5ms to Polymarket WSS origin) 是**主导延迟**, 本机 650us 是 noise. 多核 pin 的收益 (减少 cache miss + NUMA cross-traffic) 在 paper 阶段无法测量. 单核更简单, 更容易 profile 真实 bottleneck.

2. **M4.5 后 live 模式**: 接真 signer (old 孙 v5.1 IPC) + CLOB submit 后, 链路延迟预算被真实量化. 届时 vCPU pin 的实际收益 (老姜 bench: SPSC + vCPU0-6 拓扑) 有数据支撑. 提前 pin 是没有 feedback loop 的优化, 违反"数字说话"铁律.

3. 选 A (7 vCPU) 的风险: 在没有真实 workload 的情况下, 7 vCPU pin 配置是 over-engineering. 如果 M4.5 后发现信号计算不是 bottleneck (很可能如此, 因为 Polymarket 体育市场 update rate 是 ~1 event/s, 远不是 HFT), 7 vCPU pin 的 infra 复杂度是净负债.

**条件**: M4.5 后老姜出 vCPU 实测 bench (p50/p99/p99.9 三档), 决定 M5+ vCPU pin 方案. 选 A/B/D 哪个由数字决定, 不由预感决定.

---

## §2 量化 Smell 清单

我在本次 challenge 会中抛出以下 architecture smell, 请 summit 参会人参考:

**Smell-1: P0-01 到底需要 12 个信号吗?**

小程 signal catalog v1 规划了 P0-01 (Pinnacle/Goalserve no-vig) 到 P2-12 共 12 个信号. 从 Kelly 组合角度:

- 相关信号增加 Kelly size 的方式是乘以 sqrt(N) (假设独立). 但 Polymarket 体育市场 12 个信号的 edge 来源高度相关 (都锚在 fair_value, 都受同一 de-vig 误差影响). 相关因子 rho 高时, Kelly 组合收益趋近于单信号, 但监控/维护/回测成本是 N 倍.
- 我的量化建议: MVP 先跑 **P0-01 + P1-04 + P1-05 = 3 个独立性最强的信号** (no-vig + momentum + live section divergence), 用 M4.5 后 14 天数据算信号相关矩阵. 若 rho < 0.3 才值得扩到 5-6 个. 12 个是研究项目的数量, 不是 MVP 的数量.

**Smell-2: backtest vs paper consistency (R-2) 是否过严?**

我自己立了 R-12/R-13 (hash 一致). 但我要质疑自己: backtest 的 KELLY_FRACTION 是否应该允许与 paper 不同?

结论: 不允许. 原因是 M4.5 G2 (Sharpe bootstrap CI) 的校准依赖 backtest 和 paper 在相同参数下的 Sharpe 对比. 若 backtest KELLY_FRACTION = 0.5, paper KELLY_FRACTION = 0.25, Sharpe 的差异无法归因. 过严的代价 (backtest 跑速稍慢) 远小于数学解释能力损失.

但有一个合理的放松点: **backtest 允许关闭 BLAKE3 audit chain** (纯计算性能). R-12 的 hash 一致要求应限定在"风控参数"而非"所有 build flags". 这是值得 sprint 讨论的精确边界.

**Smell-3: multiplicative de-vig 是否过简?**

ADR-008 我选了 D (multiplicative, ~30 行). 我现在公开质疑自己:

- multiplicative 假设 "vig 在 yes/no 对称分摊". Goalserve 8-9 家 retail bookmaker 的实证数据 (老彭 W6 EOW 才出) 可能显示这个假设在某些盘口 (如 totals over/under) 有系统偏差.
- 量化风险: 若 vig 不对称, fair_value 系统性偏 0.5-1%, 触发 P0-01 的是噪声 edge 而非真实 alpha. 在 taker 3% fee 下, 这意味着 expected edge 净额可能是负的.
- **我的立场**: M4.5 前用 multiplicative (简单, audit 易). M4.5 后用老彭历史实证数据检验对称假设. 若 RMSE > multiplicative 误差的 0.5%, 启动 Shin 升级评估 (老郭 ADR-008 upgrade path 已规划).
- **Shin 升级时机指标**: 老彭 vig 实证中任意盘口的 |p_yes_devig - 0.5*(1 - vig)| > 0.01 连续 3 周 → 触发升级评估.

**Smell-4: M4.5 G2 CI 下界 0.5 还是 0.3?**

Sprint-1 retro 我签的是"G2: Sharpe CI 下界 > 0.3" (小梁 speech §2.6), 但小董 gate framework v1 §3.2 写的是 CI 下界 > 0.5. 两个数字在我的名下出现了不一致.

- 0.3 的论据: 14 天 50 笔样本, bootstrap CI 宽度 ~1.2 (Sharpe 1.5 时 SE ~ 0.4). 下界 0.3 意味着点估 Sharpe ~1.5, CI [0.3, 2.7]. 低门槛: 只要不是全靠运气.
- 0.5 的论据: 小董 v1 写的 (CI 下界 > 0.5). 稍高门槛.
- **我现在的立场**: 以小董 v1 为准 (0.5). 我 sprint-1 retro speech 的 0.3 是口头数字, 没正式会签进 gate_evaluator.hpp. 小董 v1 是代码实现 SSOT. **我需要补一个正式会签确认 0.5 入档**.

---

## §3 优先级排序

按对 MVP (M4.5 前) PnL 影响降序:

| 优先级 | Smell | 影响 | 处置 |
|---|---|---|---|
| P1 | Smell-3 de-vig 对称假设 | 直接影响 P0-01 edge 净额方向正负 | 老彭 W6 EOW 实证结果出后, 我 24h 内 ack 或启动 Shin 评估 |
| P2 | Smell-4 G2 CI 下界 0.3 vs 0.5 | 影响 M4.5 解锁时机 (门槛误差) | 我本周内出正式会签确认 0.5 进小董 v1 SSOT |
| P3 | Smell-1 12 个信号过多 | 影响 W6-M4.5 期间研发资源分配 | 我与老钱 (CPO) 对齐: M4.5 前最多 3 信号, M4.5 后用相关矩阵决定扩展数量 |
| P4 | Smell-2 R-2 backtest audit chain 放松边界 | 影响 backtest 跑速 (非关键路径) | 留 Sprint-2 mid 讨论, 不阻 M4.5 |

---

## §4 不耻下问 (跨单元 ask)

- **@小董** (E-023, stats): G2 CI 下界 0.5 vs 0.3 — 你的代码 gate_evaluator.hpp 是 0.5, 我 retro 口头说过 0.3. 以你代码为准, 但我需要出正式 ack 文档. W6 一请你确认 SSOT 是 0.5 并在 gate_evaluator.hpp 加我签字注释.
- **@老彭** (E-030, betting-expert): de-vig 对称假设实证是 W6 EOW. 请在 Goalserve getodds 7 天采样中**特别标注 totals over/under 盘口** (yes/no 名义上对称但庄家处理可能不同). 这是 Smell-3 的关键数据点.
- **@老钱** (CPO): M4.5 前信号数量上限 — 我建议 3 个 (P0-01 + P1-04 + P1-05). CPO 视角是否接受? 若不接受请给量化理由 (每多一个信号的 expected incremental Sharpe 贡献 vs 研发成本). 否则我按 3 个上限建模 MVP 资源分配.
- **@老郭** (F 协调): de-vig provider 接口抽象 (ADR-008 升级路径) — 你在 ADR-008 评审时问过"是否在 signal layer 抽象 de-vig provider 接口便于未来 Shin 升级". 我支持抽象, 但接口边界由你定. 请在 W7 前出接口设计草稿, 我给数学 spec (入参出参的概率校准需求).

---

**主管签字:** 小梁 (E-018, C 单元 Manager, financial-expert)
**cpp 行数:** 0 (主管不亲力亲为红线守住)
**Last updated:** 2026-06-01 by 小梁
