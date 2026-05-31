# 目标仓位连续控制 — 策略架构方向 v1

> owner: 老雷 (GM) · last_review: 2026-05-31 · 性质: 策略北极星方向 (老板 2026-05-31 口述思想汇总)
> 配套: docs/MEETINGS/2026-05-31-binary-market-context-design.md (盘口上下文会两轮)
> 老板原话精神: "围绕我这个思想去做" · "我是这个意思也不一定对" → 本文是方向, 不是冻结 spec, 实现时校准。

---

## 1. 老板思想汇总 (2026-05-31, 一串口述拼成的完整架构)

| # | 原话要点 | 架构含义 |
|---|---|---|
| 1 | 盘口要有父级引用 + 双边 YES/NO 订单簿 | 操作单位=盘口, 上下文要完整(上行父级 + 下行双边) |
| 2 | 所有数据本质是一棵树; 直播源也进树挂 Event | 统一逻辑数据树 (Sport→Event→Market→Token→book), 比分挂 Event 节点共享 |
| 3 | 刷新频率无所谓, 各刷各的, 只要相对都是最近刷新 | 不需整树同步刷新; 每节点各自保新鲜 + 联合新鲜度 |
| 4 | 新鲜度是模型输入, 绝不当守门 (怕错过进/出场止损) | staleness→特征, 不 fail-closed gate |
| 5 | 很多守门也要这么想, 不能草率守门 | 守门分两类: 保命硬门(留) vs 信号质量草率门(降为输入/软权重) |
| 6 | 没有进/出场信号; 模型输出**双边目标仓位**, 据此调仓, Kelly 协调 | 范式从「信号触发」→「目标仓位连续控制」 |
| 7 | 盘口策略在适当时机重新评估 | 事件驱动 re-eval (book/score 更新触发) |
| 8 | 最新持仓也要进模型 | 当前持仓 = 模型输入 (库存感知) |

**一句话:** 在统一市场树上,对每个盘口、在适当时机、用完整上下文(父级+双边簿+直播源+新鲜度+**当前持仓**)喂模型,模型输出**目标双边仓位**(非进出场信号),Kelly 协调规模,控制器连续把**当前仓位**驱动到**目标仓位**;守门只在保命层硬拦,信号质量层不草率守门,出场/止损自然涌现且绝不被信号质量门挡。

---

## 2. 核心控制环 (target-position control)

```
每个盘口, 适当时机(book/score 更新等触发)重新评估:
  ┌─ 模型输入 = 盘口完整上下文 ────────────────────────────────┐
  │  · 父级: event_id / neg_risk / 兄弟盘口 (跨盘口一致性)      │
  │  · 双边: YES 簿 + NO 簿 (microprice/imbalance/cross_spread) │
  │  · 直播源: event 节点比分/钟/状态 (子盘口共享)             │
  │  · 新鲜度: 各源 as_of + 联合新鲜度  ← 输入, 不守门          │
  │  · 当前持仓: 本盘口净仓/双边敞口/avg entry  ← 库存感知      │
  │  · 市场质量: match_confidence / vig 等                      │
  └────────────────────────────────────────────────────────────┘
                            ↓
            模型输出 = 目标双边仓位 target_pos (非 entry/exit 信号)
                            ↓
       Kelly 协调: (edge, 置信) → 最优 fraction → target_pos 规模 + 方向
                            ↓
        控制器: order = target_pos − current_pos  (双向; 连续 rebalance)
                            ↓
        守门: 只在保命层硬拦 (RM caps/回撤/净edge<0); 信号质量层不草率守门
                            ↓
        出场/止损 = 涌现 (target 缩小/翻转即出场; 出场路径绝不被新鲜度/信号质量 gate)
```

**Kelly 怎么协调 (老板问的点):** Kelly 不是"要不要下单"的开关,是"目标仓位该多大"的标尺。模型给 edge + 置信 → Kelly 算最优 bankroll fraction → ×bankroll = 目标仓位规模;被低估边定方向(双边二元:净多 YES = 净空 NO)。**Kelly 决定 target_pos 的大小, 模型决定 target_pos 的方向/存在, 控制器决定怎么从 current 走到 target。**三者各司其职。

---

## 3. 现状 → 目标 的差距 (delta)

| 维度 | 现状 (信号/触发范式) | 目标 (目标仓位范式) | 差距 |
|---|---|---|---|
| 输出 | SizingCalculator 出 Kelly notional → 一次性 BUY OrderIntent | Kelly notional = **目标仓位**(带符号双边) | 重新诠释为 target, 非 one-shot |
| 控制 | 无; 每 tick 各自决定是否下单 | order = target − current, 连续 rebalance | **新增控制器** |
| 持仓入模 | 部分(sizing 看 exposure; FeatureSnapshot 有 rm_exposure_pct) | 本盘口净仓/双边敞口/avg entry 显式入模型输入 | **补 per-盘口持仓特征** (老板 #8) |
| 守门 | advisory/has_real_fair/devig_ok/staleness 多个硬门 | 保命门留; 信号质量门降为输入/软权重 | **守门审计 + 重分类** |
| 出场 | 无(M1 entry-only buy) | target 缩小/翻转即出场, 双向 order | **开放卖出/减仓 (老韩 C1 signed cap)** |
| re-eval | 固定 tick interval | 事件驱动(book/score 触发) | 触发机制演进 |
| 输入丰富度 | de-vig + 单边为主 | 父级+双边+直播源+新鲜度 | **A2-A5 已起步**(本日已落) |

---

## 4. 已落地 (2026-05-31, 与方向同向的第一批)

- 双边微观结构进 QuoteFeatures + 训练数据 (cross_spread/no_microprice/双边 imbalance/devig_ok)
- 联合新鲜度 joint_as_of (输入, 不 gate)
- read-skew 修复 (event 比分子盘口共享一致快照)
- join 边质量 match_confidence/as_of (输入, 不 gate)
- **撤掉** cross_spread→CI 的草率守门 (改为纯输入) — 守门原则的第一次应用

## 5. 分阶段路径 (MVP 优先 + 向目标演进; 待 GM 拍板)

- **P0 (现在, 便宜, 同向)**: 当前持仓入模型输入 (老板 #8) — per-盘口净仓/双边敞口/avg entry 进 QuoteFeatures(观测/训练) + 模型特征。纯加性, 不动 gate。
- **P1 (设计, 需评审)**: **守门审计** — 全库 gate 逐个分类「保命硬门(留) / 信号质量草率门(降输入或软权重)」, 老韩(RM)+小梁(quant)+老郭(架构) 评审。出场路径「绝不被信号质量 gate」立硬红线。
- **P2 (大设计+build)**: **目标仓位控制器** — Kelly notional→target_pos, order=target−current 连续 rebalance, 双向(开/平/反), 事件驱动 re-eval。小梁(Kelly主权)+老韩(RM signed cap C1)+老周(架构)。是真实生产策略的核心, 替换 M1 一次性 BUY 范式。
- **永不**: 在出场/止损路径加任何新鲜度/信号质量 gate。

## 6. 红线对齐

- 新鲜度/信号质量 = 输入, 不守门 ([[freshness-input-not-gate]] memory)。
- 保命层 (RM caps/回撤/净edge<0/R-11/R-12/R-20/私钥) 仍硬门 — 这是「纪律高于收益」, 不动。
- 目标仓位双向化需老韩重裁 condition cap 的 signed-sum 语义 (现有 C1 议题)。
- 回测/实盘同逻辑: 控制器逻辑回测实盘共用一份。
