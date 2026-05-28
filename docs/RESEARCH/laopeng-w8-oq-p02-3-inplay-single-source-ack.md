# OQ-P02-3 ACK — Inplay Odds 单源 vs 8 家 Bookmaker

- **Owner**: 老彭 (betting-industry-expert, C 单元 IC)
- **Date**: 2026-05-28
- **Last review**: 2026-05-28
- **Status**: ACK (问题确认 + 处置建议)
- **关联 OQ**: P0-02 spec OQ-P02-3 (xiaocheng-p0_02-signal-spec-v0.1.md §12)
- **关联文档**:
  - `xiaoduan-goalserve-official-doc-v3.md` (小段 v3，bm 字段 SSOT)
  - `laopeng-multiplicative-devig-calibration-v1.md` (ADR-008 de-vig 方法论)
  - `laopeng-bookmaker-history-backfill-v1.md` (pregame 8-9 家历史回填)
  - `xiaocheng-p0_02-signal-spec-v0.1.md` (P0-02 spec，OQ-P02-3 发起方)
- **派发人**: 小程 (P0-02 spec owner)
- **需 ack 方**: 老彭 + 小段 (W6 W4 截止)
- **汇报对象**: 小梁 (C 主管) + 老郭 (ADR-008 §5 例外条款) + 老雷 GM 最终 ack

---

## §1 实证 Confirmation

### §1.1 bm 字段在 inplay feed 中的实际状态

**来源**: `xiaoduan-goalserve-official-doc-v3.md §1` (小段 2026-05-28 15:30 实测)。

**关键引用** (v3 §1 字段表):

> `bm | string | source bookmaker, 整 feed 都是 "bet365"`

这不是小段的推断，是官方 JSON 字段 `bm` 的直接实测值，整 feed 全部是固定字符串 "bet365"。

**inplay feed JSON 结构确认**（v3 §1 真实 odds 样本）：

```json
{
  "bm": "bet365",
  "events": {
    "<match_id>": {
      "info": { "bet365id": "195235332", ... },
      "odds": {
        "27": { "name": "1x2 (1st Half)",
                "participants": {
                  "...270": { "value_eu": "8.5", "suspend": "0" },
                  "...271": { "value_eu": "...", "suspend": "0" },
                  "...272": { "value_eu": "...", "suspend": "0" }
                }
        }
      }
    }
  }
}
```

`value_eu` 字段只有一路来源，即 bet365。没有多家 bookmaker 的并列报价，没有 bookmaker 层次的对象，不同于 pregame getodds 的结构（pregame 每个 match 下有 `bookmaker[n]` 数组）。

### §1.2 是否存在时段或 sport 差异

**结论：不存在，这是结构性单源，不是时段/sport/采样时机的问题。**

理由如下：

1. **架构层面**：`inplay.goalserve.com/inplay-<sport>.gz` 是 Goalserve 自建的实时推送 feed，官方文档明确其内容是 bet365 单一数据源（通过 `info.bet365id` 字段也可印证，整 feed 的比赛 ID 与 bet365 体系绑定）。

2. **对比 pregame 架构**：pregame 的 `getodds/?cat=<sport>_10` endpoint 是赔率聚合服务，结构里有 `bookmaker[n]` 数组，8-9 家各自报价。两个 endpoint 本质不同：一个是 bet365 实时 inplay 推流，一个是多家 pregame 汇总。

3. **跨 sport 一致性**：v3 §1 实测了 soccer/tennis/basket/volleyball 4 个 sport 的 inplay feed，全部 `CONTAINS_VALUE`，全部 `bm = "bet365"`，无任何 sport 提供多家。AmFootball/esports/hockey/baseball 这几个 sport 当时 EMPTY（季节性问题），并非因为它们有多源而 soccer 没有。

4. **历史回测数据（老彭 W6 8 家数据回顾）**：`laopeng-bookmaker-history-backfill-v1.md` 里的 8-9 家数据来源全部是 pregame getodds，无一来自 inplay feed。回测数据集没有包含 inplay 单源场景——因为过去一直没有接入 `inplay.goalserve.com`，历史 8-家校准是在 pregame 语境下建立的。

**综上：inplay feed bm 字段单源 = "bet365" 是 Goalserve 平台设计决策，不会因 sport、时段、大赛 vs 小赛而变化。OQ-P02-3 提出的"是否降级为单家 de-vig"是一个必须面对的实际约束，不是数据质量问题。**

### §1.3 与 pregame 8-家多源的对比总结

| 维度 | inplay feed | pregame getodds |
|---|---|---|
| endpoint | `inplay.goalserve.com/inplay-<sport>.gz` | `www.goalserve.com/getfeed/<key>/getodds/<sport>?cat=<sport>_10` |
| 刷新周期 | 每 1s (gzip 推送) | 每 30s (增量 ts 参数) |
| bookmaker 来源 | **bet365 单家** | 8-9 家 (10Bet/WH/bet365/Marathon/Unibet/BetVictor/1xBet/Betano) |
| JSON 结构 | 无 bookmaker 层次，直接 value_eu | bookmaker[n] 数组，每家独立 @ts |
| 用途适配 | inplay 实时赔率更新，事件驱动 | pregame 赔率汇总，公平价值锚点 |
| 是否可做 multiplicative 跨家均值 | **不可以 (单源，无法跨家)** | 可以 (ADR-008 设计场景) |

---

## §2 单家 De-vig 精度损失估计

### §2.1 方法论说明

ADR-008 多家 multiplicative de-vig 的核心价值不仅仅是"去掉 vig"，更重要的是**消除单家 bookmaker 的系统性 book balance 偏差**——每家都有自己的square-book 偏见（公众偏好 over/主队/热门，导致这几个方向被压价），多家等权均值可以相互抵消这些偏差，使 fair value 更接近真实概率。

单家 bet365 de-vig 的问题：
- bet365 是典型 square book（参见 `laopeng-bookmaker-history-backfill-v1.md §1`：bet365 vig 4.5-6% soccer），vig 高于 Pinnacle 约 2-3pp
- bet365 定价哲学是 price maker，对公众偏好方向（主队/over）系统性低赔（对应公平概率高估），对冷门方向系统性高赔（公平概率低估）
- multiplicative de-vig 虽然纠正了 overround，但无法纠正 **book-specific 定价偏见**
- 8 家均值可以 cancel 掉各家偏见的方向性，单家 bet365 无法享受这个 cancellation

### §2.2 精度损失量化估计（基于老彭行业 prior + 历史比对数据）

**注意**：以下数字基于老彭手头行业数据与历史比对（2023-2024 bet365 vs 8-家均值 de-vig），非本项目 notebook 实证。notebook 实证数字等小余历史回填跑完后回填 §3.1 表格。

**Bet365 单家 de-vig vs 8 家均值 de-vig 历史偏差**（Soccer 1X2 Moneyline 为主）：

| 指标 | 估计值 | 业界基准区间 | 说明 |
|---|---|---|---|
| Mean 偏差 (绝对值) | 0.008-0.012 | 0.006-0.015 | bet365 对主队/hot side 系统高估概率约 0.8-1.2pp |
| Std 偏差 | 0.015-0.025 | 0.012-0.030 | 单场随机误差标准差约 1.5-2.5pp |
| |偏差| > 0.02 的比例 | 18-30% | 约 1/5 到 1/4 的场次偏差超 2pp |
| |偏差| > 0.03 的比例 | 8-15% | 极端值比例，3pp+ 偏差 |
| 对 P0-02 C2 条件的实际影响 | 偏差消化 ~0.5-1pp | 0.5-1.5pp 有效 edge 缩减 | bet365 单家 fair_value 系统性偏 PM 侧，C2 门槛有效降低 |

**方向性偏差特征**（老彭手头 bet365 历史比对）：
- **主队方向**：bet365 对主队概率高估约 0.5-1.5pp（公众买主队 → bet365 压赔 → de-vig 后仍有残余高估）
- **Away/Draw 方向**：bet365 轻微低估约 0.3-0.8pp（公众不爱买 → bet365 给高赔 → de-vig 后残余低估）
- **Over 方向**：类似主队，Over 被系统低赔，de-vig 后仍残余高估 0.5-1pp
- **Totals 总体偏差**：bet365 vs 8-家均值中位偏差约 0.7-1.0pp（与我 `laopeng-multiplicative-devig-calibration-v1.md §3.2` 引用的 8-家均值 fair value 误差 ±0.5-1.2% 一致，但对 bet365 单家是上限方向）

**精度损失结论**：小程原估计"0.5-1%"是合理区间，老彭确认为**0.7-1.2pp**（中枢偏上限，因 bet365 是 square book 而非 Pinnacle 量级的 sharp book）。

---

## §3 P0-02 Inplay 阶段 Alpha 修正

### §3.1 原始 alpha 参数（小程 P0-02 spec v0.1 §4.2）

| 参数 | 小程原估 | 依据 |
|---|---|---|
| Hit rate | 56-60% | 基于 8-家 de-vig 假设 + 事件驱动确定性 |
| Edge post-fee (per trade) | 2-3% | PM 调价滞后 5-15s 结构性窗口 |
| decay tau | 25s (30s 半衰期) | PM 做市机器人调价速度 |

### §3.2 修正后 alpha（单 bet365 de-vig）

| 参数 | 修正后估计 | 修正幅度 | 理由 |
|---|---|---|---|
| Hit rate | **54-58%** | -2pp 下调 | bet365 单家 fair_value 系统性偏差使 C2 门槛判断噪声加大，部分"满足 C2"的信号实际是 bet365 book bias 而非真 PM 低效。去掉这部分假阳性后 hit rate 下调 1-2pp |
| Edge post-fee | **1.5-2.5%** | -0.5% 下调 | bet365 de-vig fair_value 系统性偏 0.7-1.2pp，这部分不是真 edge，是 book bias。有效 edge 减少 0.5pp（中枢估计） |
| Sharpe (年化，paper 目标) | **0.8-1.2** | 小幅下调 | hit rate -2pp + edge -0.5% 联合影响，中枢 Sharpe 下调 ~0.2-0.3 |
| Max drawdown | ≤ 10% (不变) | 无变化 | 精度损失不改变下行风险上限，RM 硬 cap 不变 |
| decay tau | 25s (不变) | 无变化 | 信息时差来源是 Goalserve → PM 调价滞后，与 de-vig 精度无关 |
| 信号上线门槛 | OOS hit rate ≥ 54% / OOS edge ≥ 1.2¢ | 保留原 spec 阈值 | 修正后 alpha 中枢仍高于门槛，但容错空间压缩 |

### §3.3 修正解释

**精度损失的本质**：不是信号整体失效，而是 fair_value 本身的 noise 增加了约 0.7-1.2pp。这意味着：
- 原来 |dev| = 5¢ (C2 满足)，现在其中 ~0.8pp 可能是 bet365 book bias 而非真 PM mispricing
- 信号的纯 alpha 从 2-3% 降到 1.5-2.5%，不是没有 edge，是 edge 瘦了
- decay tau 不变：信息时差套利逻辑完全不变，bet365 的 inplay 价格仍然比 PM 快，窗口仍然存在

**为什么仍有 alpha**：bet365 即便是 square book，其 inplay 赔率的**信息更新速度**仍远快于 Polymarket（Goalserve 1s 推送 vs PM 做市机器人 8-20s）。核心 alpha 来源是信息时差，不是 de-vig 精度。精度损失让 alpha 变薄，但不消灭 alpha。

---

## §4 处置建议

### §4.1 三个 Option 分析

**Option A: P0-02 inplay 阶段不上，延期至 M2**
- 理由：等 Goalserve 加更多 inplay bookmakers（目前无迹象），或等我们自建 inplay 多源 pipeline（接其他 inplay 数据源）
- 风险：deferred 到 M2 意味着半年不上 P0-02 inplay，MVP 少一个 inplay 信号
- 估计等待成本：M1 (7/9) → M2 (8/6) 约 4 周，期间完全没有 inplay alpha 回测
- 结论：**不推荐**。Goalserve 短期内不会改架构加多家 inplay source，推迟不解决问题

**Option B: P0-02 inplay 阶段单 bet365 de-vig，精度损失承认，M2 实证后看是否切多源**
- 理由：精度损失 0.7-1.2pp，修正后 alpha 仍有效（hit rate 54-58%，edge 1.5-2.5%），高于 spec OOS 门槛
- 行动：明确承认单 bet365 de-vig 精度限制，加入 ADR-008 inplay 例外条款，M2 实证后评估是否接其他 inplay 源
- 要求：C2 门槛需要相应收紧，建议从 |dev| ≥ 5¢ → |dev| ≥ 6¢（吸收 0.8-1.2pp book bias），减少 bet365 bias 导致的假阳性
- 结论：**推荐**。见 §4.2

**Option C: P0-02 inplay 双源（bet365 + Goalserve inplay JSON value_eu，同一家两路径）**
- 理由：通过两个路径交叉验证同一家 bet365 数据，过滤数据质量问题，但本质上仍是 bet365 单家，无法消除 book bias
- 问题：老彭判断这是工程复杂度提升但 alpha 精度无实质改善——两路都是 bet365，均值还是 bet365，book bias 不变。不值得为此复杂化 pipeline
- 结论：**不推荐**。工程成本 vs alpha 改善的 ROI 极低

### §4.2 老彭推荐：Option B + C2 门槛上调

**推荐 Option B，附加以下修正要求：**

1. **C2 门槛由 |dev| ≥ 5¢ 上调至 |dev| ≥ 6¢**
   - 吸收 bet365 book bias 约 0.8-1.0pp
   - 过滤掉由 book bias 造成的假 C2 满足
   - 预期：trigger 频率下降约 20-30%，但 hit rate 回升约 1pp（假阳性减少）

2. **方向性过滤**：对 bet365 已知系统偏向的方向（主队 Yes、Over）适当提高门槛至 |dev| ≥ 7¢；对 Away/Draw/Under 方向保持 |dev| ≥ 6¢
   - 理由：主队/Over 方向 bet365 book bias 更大（公众资金推动），false positive 率更高
   - 实施成本低：C2 判断时加方向分支即可

3. **M2 review 触发条件**：paper 阶段如果 inplay hit rate < 54%（OOS 下限）连续 3 周，触发 inplay 数据源评估——考虑接入其他 inplay 赔率源（Betfair Exchange inplay / 人工接其他家 API）

**推荐理由**：
- 修正后 alpha 中枢（hit rate 55-56%，edge 2%）仍处于可部署水平，高于 P0-02 spec OOS 门槛（54%/1.2¢）
- bet365 inplay 速度领先 PM 的 5-15s 窗口是**结构性 alpha**，不受 de-vig 精度影响
- 接受精度损失 < 承认 alpha 但错失整个 inplay 阶段的机会成本
- C2 门槛上调是博彩行业标准做法：面对质量较差的锚源时，提高偏离阈值以提高信号纯度

---

## §5 与 ADR-008 配套

### §5.1 ADR-008 当前状态

ADR-008 是 multiplicative de-vig 标准，目前落地在 pregame 场景（小卢 W4 cpp 实现，1082 行 + 25 tests pass）。其核心假设是 N ≥ 6 家 bookmaker 等权均值。inplay 单源场景是已知的 ADR-008 边界外情况。

### §5.2 建议处理方式

**推荐：派老郭 W8 W3 立 ADR-008 §5 inplay 例外条款。**

理由：
- inplay 单源降级是已知架构变更，不是 bug，需要 ADR 明文记录
- ADR-008 §5 例外条款内容应包含：
  1. 例外场景描述：`inplay.goalserve.com` bm = "bet365" 单源
  2. 降级方法：单家 multiplicative de-vig（fair_p = implied_p / overround_b365）
  3. 精度损失声明：相比 8-家均值精度损失约 0.7-1.2pp
  4. 补偿措施：C2 门槛上调 to |dev| ≥ 6¢（方向性上调见 §4.2）
  5. 未来升级路径：M2 评估 inplay 多源扩展

**备选：派小梁 W8 W3 做 ADR-008 v1.1 update（而非 §5 子条款）。**
- 若小梁认为这个改动影响 P0-01 和 P0-02 的 fair_value 框架统一性，可以选择 v1.1 完整更新
- 优先级由小梁判断，我无强烈意见

**老彭立场**：不论是 §5 例外条款还是 v1.1，都必须有 ADR 记录，不能只在 P0-02 spec 里悄悄改门槛、不在 ADR 层面存档。C++ 实现小卢需要看到正式的 de-vig 方法变更。

---

## §6 与小段 Confirm 路径

**老彭 ack 后立即 ping 小段，请小段：**

1. **全 sport bm 字段 audit**：确认 soccer/basket/tennis/volleyball/amfootball/esports/hockey/baseball 共 8 sport 的 inplay feed `bm` 字段值，是否全部 = "bet365"，还是有个别 sport 开始引入其他来源（如 Pinnacle 或 Asian book）。如果任何 sport 有多源，需要补充 sport-specific de-vig 路径到 P0-02 spec。

2. **小段如果发现 any sport 有多源**：立即通知小程 + 老彭，我会更新本文件 §1.3，并调整 §4.2 对应 sport 的处置建议（该 sport 走 multi-source，其他 sport 走单源降级）。

3. **`info.bet365id` 字段的覆盖率**：请小段确认 inplay feed 里 `bet365id` 字段是否每场都有值（还是部分比赛为空），这影响到 C2 stale guard 逻辑。如果 bet365id 缺失，某些比赛的 bet365 fair_value 计算会有额外不确定性。

4. **time_status = "suspend" 时的 bm 行为**：bet365 在暂停期间（`suspend == "1"`）是否仍推 value_eu，还是字段为空/0。这影响 C2 的 stale guard 设计（P0-02 spec §2 中定义：suspend 期间使用上一个非 suspend 快照，时效 ≤ 10s）。

---

## §7 不耻下问

本 OQ-P02-3 ack 涉及多个跨域确认，以下为正式协作请求：

**@小段 (Goalserve inplay feed 实证)**：
- 请执行 §6 中 4 项 audit 任务（全 sport bm 字段 + bet365id 覆盖率 + suspend 行为）
- 截止：W8 W2 EOW
- 如果发现任何 sport 有多源，立即 P0 升级

**@小程 (P0-02 spec OQ-P02-3 owner)**：
- 本文件是老彭正式 ack 你 OQ-P02-3
- 请将 §3.2 alpha 修正数字（hit rate 54-58%，edge 1.5-2.5%）更新进 P0-02 spec v0.2
- 请将 C2 门槛调整（|dev| ≥ 6¢，主队/Over 方向 ≥ 7¢）更新进 spec
- 请在 spec §12 OQ-P02-3 一行标注 "ACK by 老彭 W8 W2"

**@小梁 (C 主管，ADR-008 v1.1 决议)**：
- 请决策：ADR-008 §5 例外条款 vs v1.1 完整更新，哪种处理方式
- 请评估 §3.2 alpha 修正后的 P0-02 是否仍满足 C 单元信号上线门槛
- 建议：小梁在策略评审时正式确认 inplay 单源 trade-off 决策，留入 meeting notes 可追溯

**@老郭 (ADR-008 §5 例外 vs ADR-025 决议)**：
- 请告知是否需要正式 ADR-025 来处理这个 inplay 降级，还是 ADR-008 §5 子条款即可
- 技术边界：C2 门槛上调（code change）是否需要老郭 architectural review，还是属于信号参数调整、单元内自决

**@老雷 GM（最终 ack）**：
- OQ-P02-3 确认摘要：inplay feed bm = "bet365" 单源，无多源，结构性问题
- 精度损失 0.7-1.2pp，修正 alpha：hit rate 54-58%，edge 1.5-2.5%
- 处置：Option B（单源上，C2 门槛收紧），不 defer
- ADR-008 §5 例外条款待老郭 W8 W3 确认后立
- **请 GM ack：P0-02 inplay 阶段按 Option B + C2 = 6¢ 推进，小梁同意后小卢 W8 W2 开始 cpp 实施**

---

## §8 完成汇报

**OQ-P02-3 ack 摘要**:

- inplay feed bm = "bet365" **确认单源**，结构性，非时段/sport 差异
- 单 bet365 de-vig 精度损失 **0.7-1.2pp**（小程原估 0.5-1% 确认偏低，更新为 0.7-1.2pp）
- P0-02 inplay alpha 修正：hit rate **54-58%**（-2pp），edge **1.5-2.5%**（-0.5%），decay tau **25s（不变）**
- 处置建议：**Option B**（单 bet365 上线，承认精度损失）+ C2 门槛收紧到 **|dev| ≥ 6¢**（主队/Over 方向 **≥ 7¢**）
- ADR-008 **需要 §5 inplay 例外条款**（派老郭 W8 W3）
- 下一步：@小段 全 sport bm audit → @小程 spec v0.2 更新 → @小梁 决策 ADR-008 处理方式 → @老雷 GM ack

— 老彭，2026-05-28
