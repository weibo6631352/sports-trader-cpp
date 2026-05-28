# ADR-016 G2 Sharpe Bootstrap CI 下界阈值正式入档

- **ADR 编号:** 016
- **标题:** G2 CI 下界 0.5 (vs 北极星 1.5) — paper 解锁低门槛 + 4 stage 梯度
- **状态:** Accepted
- **Owner:** 小董 (stats-inference-advisor, E-023)
- **会签人 (5 方):**
  - 小梁 (financial-expert, C 单元 owner, E-018) — 阈值最终仲裁人, W6 第 1 周 ack
  - 老韩 (risk-engineer, B 单元 owner) — RM 视角 stage 阈值
  - 老雷 (GM) — ground truth 入档决议人
  - 老钱 (CPO) — 产品方向 + 北极星一致性
  - 老彭 (betting-expert, E-030) — 历史 Sharpe 实证校准
- **Date:** 2026-06-01
- **Last review:** 2026-06-01
- **关联文档:**
  - `include/stcpp/stats/gate_evaluator.hpp` (SSOT 代码实现)
  - `tests/unit/test_gate_evaluator.cpp` (T_G2_StageThresholds 5 case)
  - `docs/RESEARCH/xiaodong-m45-gate-framework-v1.md` (v1.1 起引用本 ADR)
  - `docs/MEETINGS/2026-06-01-vote-xiaoliang-arch-challenge.md` §2 Smell-4
  - `docs/MEETINGS/2026-06-01-gm-arch-vote-rulings.md` §P1 小梁#P2

---

## 1. 问题陈述

Sprint-1 retro (2026-05-28) 结束后出现两份不一致的 G2 阈值记录:

| 来源 | 阈值 | 形式 |
|---|---|---|
| 小梁 retro speech §2.6 (`xiaoliang-speech.md`) | 0.3 | 口头表达, 未正式会签 |
| 小董 W4 代码 `gate_evaluator.hpp` `kG2_SharpeCILow` | 0.5 | 代码实现, SSOT |

两份文档都以小梁名义出现, 引发 ground truth 歧义. 老板 (GM 老雷) 在 W5 架构 Challenge 拍板会将此问题升为 **P0 ground truth issue**, 要求 W6 第 1 周正式入档.

---

## 2. 上下文与历史

### 2.1 时间线

- **2026-05-28 Sprint-1 retro:** 小梁 speech §2.6 说 "G2 CI 下界 > 0.3". 这是口头数字, 未签字进 gate_evaluator.hpp.
- **W4 Wave 20:** 小董落 `gate_evaluator.hpp`, 代码写 `kG2_SharpeCILow = 0.5`. research doc v1 §3.2 也写 0.5. 这是唯一落盘的正式实现.
- **2026-06-01 W5 架构 Challenge 投票:** 小梁在自己的投票文档 §2 Smell-4 明确表态: "以小董 v1 为准 (0.5). 我 sprint-1 retro speech 的 0.3 是口头数字, 没正式会签进 gate_evaluator.hpp. **我需要补一个正式会签确认 0.5 入档**."
- **2026-06-01 GM 拍板:** 小梁#P2 列为 W6 第 1 周 P1 必交, "老板视角的 ground truth issue".

### 2.2 单一值 0.5 的统计背书 (小董提供)

**核心论点:** paper 解锁窗口只有 14 天约 50 笔交易, 在此样本量下 Sharpe bootstrap CI 的宽度决定了可设多高的阈值.

设 paper trade PnL ~ i.i.d., 点估 Sharpe S_hat = mean / std × annualizer. 对 per-trade (非年化) Sharpe, 理论渐近 SE:

```
SE(S_hat) ≈ sqrt( (1 + S_hat² / 2) / (n - 1) )
```

在 n = 50, S_hat = 1.5 (北极星目标) 时:

```
SE ≈ sqrt( (1 + 1.5²/2) / 49 ) ≈ sqrt(2.125/49) ≈ 0.208
```

95% percentile bootstrap CI 宽度 ≈ 2 × 1.96 × SE ≈ 0.82. 即 CI 约为 [1.5 - 0.41, 1.5 + 0.41] = **[1.09, 1.91]**. CI 下界 = 1.09, 远高于 0.5, 也远高于 0.3.

但问题在于 paper 实际 Sharpe 点估未必达到 1.5. 若点估 S_hat = 0.8 (中等信号):

```
SE ≈ sqrt( (1 + 0.8²/2) / 49 ) ≈ sqrt(1.32/49) ≈ 0.164
CI ≈ [0.8 - 0.32, 0.8 + 0.32] = [0.48, 1.12]
```

CI 下界 0.48 < 0.5 → 刚好接近边界. 这说明 **阈值 0.5 对中等信号是现实可过的**, 不是拍脑袋.

若阈值设 0.3 (retro 口头值):

```
S_hat = 0.6 时 SE ≈ 0.148, CI ≈ [0.31, 0.89] → CI lower 0.31 > 0.3 pass
```

0.3 门槛下, Sharpe 点估 0.6 就能通过. 这对 paper 解锁是否足够? 小梁判断: 不够严. **0.5 作为 paper 解锁才是正确的**, 因为 live 切换的机会成本 (实盘 1 季度 Q1 KPI = 0.8) 决定了 paper 解锁必须有一定把握, 而不是几乎不管用的低门槛.

**阈值 0.3 的定位:** 0.3 不是废弃值, 它是 **M2 alpha 检测门槛** (W6-W8 early alpha signal check). 在极早期用 0.3 检测信号是否存在有统计意义 — 但这不是解锁 paper → live 切换的门槛.

---

## 3. 决策

### 3.1 G2 单值确认: M4.5 paper 解锁 = 0.5

- **采纳小董 W4 代码 SSOT**
- **废弃小梁 retro 口头 0.3 (仅用作 M2 alpha 检测)**
- `kG2_SharpeCILow` = 0.5, 引用 `kG2SharpeCILowerByStage[GateStage::M4_5]`

### 3.2 G2 4 Stage 阈值梯度 (本 ADR 扩展)

| Stage | 枚举值 | 阈值 | 场景 | 时间点 |
|---|---|---|---|---|
| M2 | `GateStage::M2` | **0.3** | alpha 存在性检测 (早期信号检查) | W6-W8 |
| M4_5 | `GateStage::M4_5` | **0.5** | paper 2 周稳定解锁门槛 (本 ADR SSOT) | ~M4.5 (纸盘 14d) |
| M5_Q1 | `GateStage::M5_Q1` | **0.8** | live 第 1 季度稳态 KPI | M5+ live Q1 |
| NorthStar | `GateStage::NorthStar` | **1.5** | T+36 月北极星 KPI (CLAUDE.md §2) | T+36 月 |

阈值严格单调递增 (0.3 < 0.5 < 0.8 < 1.5), 测试 `T_G2_StageThresholds::StageArrayMonotonicallyIncreasing` 保证.

### 3.3 Stage 阈值数学根据

**M2 = 0.3:** 极早期, n < 30 笔, 样本量不足以支撑更高阈值. SE(S_hat) 在 n=20, S_hat=0.5 下约 0.23. CI lower = 0.5 - 1.96×0.23 = 0.05. 要求 CI lower > 0.3 需要 S_hat ≈ 0.75+, 说明信号至少是温和正向的, 不是噪声堆出来的. 这是 alpha 检测必要不充分条件.

**M4_5 = 0.5:** 见 §2.2 推导. n=50, S_hat=0.8 时 CI lower ≈ 0.48, 接近边界. 阈值 0.5 是"中等信号可过, 弱信号不过"的分界线, 与 paper → live 决策的风险容忍度一致.

**M5_Q1 = 0.8:** live 第 1 季度, 实盘数据 n ≥ 200+, SE 降至 ~0.07. CI lower > 0.8 意味着点估 Sharpe ≥ 0.95+. 这是"live 跑有效"的量化标准, 区别于 paper 解锁的"信号存在".

**NorthStar = 1.5:** CLAUDE.md §2 明文写"单策略 Sharpe ≥ 1.5". T+36 月实盘 n ≥ 5000+, SE < 0.02, CI lower 与点估几乎重合. 这条是长期 KPI 而非解锁 gate.

---

## 4. 红线约束

- **R-2:** backtest / paper / live 同一 evaluator binary (ADR-011 A). G2 各 stage 阈值均通过 `kG2SharpeCILowerByStage` 同 array 查询, 不允许不同 binary 用不同值.
- **R-20:** GateMetrics 4 ts 不变 (本 ADR 不触碰 PIT 契约).
- **ADR-008 配套:** de-vig multiplicative 算法影响 fair_value 锚 → G2 测的是 paper PnL Sharpe, 不直接受 de-vig 方法影响, 但 alpha 来源是 de-vig 后的 edge. de-vig 锁定 (M2 后) 是 G2 bootstrap 结果可解释的前提.

---

## 5. 不耻下问 (跨单元 ask)

| 问题 | 求助对象 | 状态 |
|---|---|---|
| 0.5 vs 0.3 最终仲裁 (mandate 拒接代码) | 小梁 (会签 owner) | W6 第 1 周 ack |
| RM 视角 stage 阈值是否合适 (M5_Q1=0.8 是否过松) | 老韩 | W6 ack |
| 历史 Sharpe 实测 — M4_5=0.5 现实概率 | 老彭 (betting-expert, W6 EOW) | W6 EOW |
| ML shadow 启动 (M2 后) 与 M2 0.3 gate 时序配合 | 小邓 (ML) | ADR-014 已定, 不阻 |
| 北极星 1.5 与 CLAUDE.md §2 一致性 | 老钱 (CPO) | CLAUDE.md 已写明, ack 即可 |

---

## 6. 会签区 (W6 第 1 周内)

| 角色 | 姓名 | 状态 | 备注 |
|---|---|---|---|
| 统计 spec 起草 | 小董 (E-023) | 已起草 2026-06-01 | 本 ADR owner |
| 阈值最终仲裁 | 小梁 (E-018) | 待 W6 第 1 周 ack | Smell-4 已口头表态 0.5, 补书面 |
| RM 风控视角 | 老韩 | 待 W6 ack | stage 阈值合理性 |
| GM ground truth | 老雷 | 待 W6 ack | ground truth P0 入档授权 |
| CPO 产品方向 | 老钱 | 待 W6 ack | 北极星一致性确认 |
| 历史 Sharpe 实证 | 老彭 (E-030) | 待 W6 EOW | W6 EOW vig 实证同批出 |

---

## 7. 被拒备选方案

### 方案 A: 维持 retro 口头 0.3 作为 M4.5 阈值

拒绝理由: 0.3 太低, S_hat 0.6 的弱信号即可通过. paper → live 切换有实盘资金风险, 不能用 alpha 检测级别的低门槛. 小梁 2026-06-01 明确表态废弃.

### 方案 B: 直接用北极星 1.5 作为 M4.5 阈值

拒绝理由: 14 天 50 笔下几乎不可能 CI lower > 1.5 (见 §2.2 SE 计算). 拦截真正有 alpha 的策略进入 live. 过严等于永远不解锁.

### 方案 C: 单一固定值, 不做 stage 梯度

拒绝理由: 业务演进需求 — M2 期需要早期 alpha 信号检测, paper 解锁需要中等门槛, live Q1 需要更高稳态标准, 北极星是长期目标. 一个值无法同时服务四个阶段的不同统计需求.

---

## 8. 影响评估

- `include/stcpp/stats/gate_evaluator.hpp`: 加 `GateStage` enum + `kG2SharpeCILowerByStage` array (改 1 行 + 新增约 20 行). `kG2_SharpeCILow` 改为引用 M4_5 slot, **数值不变** (仍 0.5).
- `tests/unit/test_gate_evaluator.cpp`: 新增 `T_G2_StageThresholds` 5 case.
- `docs/RESEARCH/xiaodong-m45-gate-framework-v1.md`: 升 v1.1, G2 章节加 4 stage 表.
- **不影响:** G1/G3-G7 阈值不动. GateMetrics 结构不动. R-20 4 ts 不动.

---

**END — ADR-016**

— 小董 (stats-inference-advisor), 2026-06-01
