# ADR-037: 数据/模型战略 — 信息源中立 + 自估赔率 + AI 量化模型

- **ID:** ADR-037
- **Date:** 2026-05-29
- **Owner:** 老雷 (GM) 拍板, 老板直接指令
- **Status:** Accepted
- **触发 (老板 verbatim 2026-05-29, 四连):**
  1. "goalserve 有 inplay 接口可不止是赔率, 有比分, 等很多值得深挖的东西, 我们还有 polymarket 的订单簿信息, 我们不是要做 ai 量化模型吗, 这些都是有用信息呀。"
  2. "将来假如我们切换了更好的数据源, 估计也差不多是这些信息, 可能只是准确性和时效性更强了。"
  3. "甚至赔率我们也可以自己估算。"
  4. "我们的 goalserve 可是付费的啊, 不是免费的, env 文件不是还有它的 key 吗。"
  5. "我现在不迁移上云, 是因为他们有白名单审核机制, 上云不利于我们的开发调试。"

---

## 1. 背景 — 推翻"数据零起点"误判

GM 此前 (drive directive v2 §10.2) 把"无付费历史 bookmaker 赔率"误判为"数据零起点 P0 blocker", 并把盈利证明绑死在 de-vig bookmaker 赔率上, 进而要花 $500/月买 The Odds API (老板已否)。

**老板纠正: 我们根本不缺数据。** 现有免费实时源已含 AI 量化模型所需的全部信息类型。

## 2. 决策

### 2.1 信息源中立架构 (vendor-agnostic)

把数据抽象成 **信息类型 (information types)**, 数据源是可替换的 **适配器 (adapter)**:

| 信息类型 | 当前已订阅源 | 未来源 (示例) |
|---|---|---|
| 比分 / 赛况 (score/clock/period) | **Goalserve inplay/livescore (付费, 已订阅, key 在 .env)** | 任意 inplay 源 |
| 球队/球员统计 (stats/events) | **Goalserve inplay 深挖 (付费)** | 同类源, 更准更及时 |
| 订单簿 (full-depth orderbook) | Polymarket CLOB WSS (公开) | Polymarket (唯一交易场) |
| bookmaker 赔率 (可选 alpha) | Goalserve odds (付费, 已含) | 第三方付费源 (以后想换再换, $500 The Odds API 已否) |

**澄清 (老板原话 4):** Goalserve 是**已订阅的付费源** (key 在 .env), 不是免费 — 我们早已为这份数据付费, 战略是**把已付费的 Goalserve 吃干榨净** (inplay 深挖, 不止赔率), 而非再花钱买额外源。**Goalserve key 同私钥一样, 绝不删除。**

**核心不变量 (老板原话 2):** 未来换更好的源, 信息类型基本一致, 只是准确性/时效性更强 → **换源 = 换 adapter, 上层 feature schema + model 不变**。任何模块禁止硬编码单一 vendor 字段, 必经 adapter 归一化层。

### 2.1b 部署 — 本地优先, 暂不上云 (老板原话 5)

- **暂不迁移上云**: 云有白名单审核机制 + 上云不利于开发调试
- **paper runtime 先在本地跑** (开发/调试友好)
- **Frankfurt/AWS server 采购从关键路径移除** (原 drive directive 的 R-W10-FRANKFURT blocker 关闭), 上云待白名单/合规时机成熟再议
- 这反而**消除一个关键路径外部依赖**, paper runtime 不再等 server 采购

### 2.2 自估赔率 / fair-value (老板原话 3)

**我们自建 pricing model 从原始信息估 fair-value, 不依赖 bookmaker 赔率作为唯一真值。**

- 输入: 比分 + 赛况 + 统计 + 订单簿微观结构
- 输出: 我们自己的 fair-value 概率 (per outcome)
- alpha = 自估 fair-value vs Polymarket 市场价 的偏离
- de-vig bookmaker 赔率**降为众多 alpha 信号之一** (交叉验证 / 冷启动先验), 不是命根; 无它系统照样能跑

### 2.3 AI 量化模型 (北极星)

```
已订阅实时信息 (Goalserve inplay 比分/统计/事件[付费] + Polymarket 全订单簿[公开])
  → adapter 归一化 (vendor-agnostic schema)
    → feature store (Parquet, point-in-time, R-20 4ts)
      → AI 量化模型 (离线训练 → ONNX/Treelite)
        → C++ 推理 (热路径, Python 不进生产)
          → 自估 fair-value + edge
            → 信号 → RM → VirtualMatcher (paper) → PnL
```

## 3. 影响

- **解除"额外付费源"依赖**: 盈利证明改用**已订阅的 Goalserve (付费) + Polymarket 公开订单簿** (forward-record 累积 + Goalserve 历史可得部分 + 自估 fair-value), 不再额外花钱。net edge 数字时间线放宽, 但不被"再买源"卡死
- **大批 idle 人员 unblocked**: D 单元 (inplay/orderbook ingestion + feature pipeline)、ML (模型)、algo (自估定价)、quant (backtest/信号) 全部有活
- **回测=实盘同源红线强化**: adapter + feature store 同一套喂 backtest 和 paper, VirtualMatcher/FillRateModel 复用 (小袁已留接口)

## 4. 协同工作流 (老板纠正: 用 git/worktree/PR)

**即日起所有代码任务走 ADR-029 worktree + branch + PR 协同流** (不再"主工作区改不 push"):
- 每个 IC 编码任务在独立 worktree 开分支
- 完成 `git push` + `gh pr create`
- review (老高质量 / 老郭架构 / owner) 后 merge
- 文档任务同样走 PR

## 5. 落地

- [x] ADR-037 立 (本文件)
- [ ] drive directive 数据战略段重写 (R-DATA-ZERO 关闭, 改 vendor-agnostic)
- [ ] 大规模 mobilization wave: D/ML/algo/quant idle 人员铺活 (worktree+PR)
- [ ] adapter 归一化层 + feature schema 设计 (小田#24 + 小段 + 小冯)
- [ ] 自估 fair-value 定价模型 PoC (小肖 + 小邓)

---

**最后更新:** 2026-05-29 by 老雷 (GM)
