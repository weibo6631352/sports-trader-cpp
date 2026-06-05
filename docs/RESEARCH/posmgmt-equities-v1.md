> owner: (research agent for 老雷) · last_review: 2026-06-05

# 持仓管理外部最佳实践 — 股票/期货/系统化交易视角 (v1)

调研视角：**股票 / 期货 / 系统化(CTA、量化)交易的持仓管理**。
目标：把成熟市场的持仓管理方法论，落到我们系统的硬约束上（方向真值=赔率源 sharp、费曲线 p(1-p)、目标仓位连续控制、限价不追、二元 CTF token、无 rule-based 止损止盈、RM caps 保命）。

> **一句话结论**：这套体系里**真正能迁移的是"规模(sizing)层"**——分数 Kelly、波动率目标、基于回撤的去险，它们都和我们"target_signed_notional + Kelly + RM caps"的范式同构，且都独立于方向判断。**最不该迁移的是"方向层"的那套打法**——加仓金字塔、砍亏损、scaling out 止盈，它们本质是把"执行动作"当"信号"用（用价格走势替代方向判断），而我们方向真值已经外置到赔率源，这条线迁过来会和我们的架构哲学打架，且在 p(1-p) 费曲线 + 限价不追下大量失效。

---

## 一、核心方法 / 原理

### 1. 仓位规模 (Position Sizing)

#### 1.1 Kelly / 分数 Kelly
- **全 Kelly** 给出长期增长率最优的下注比例，但波动极大：长跑中约 1/3 概率经历 50% 回撤，理论可达 50–80%+ 回撤。([Medium — Dangers of Full Kelly](https://medium.com/@tmapendembe_28659/the-dangers-of-full-kelly-criterion-why-most-traders-should-use-fractional-kelly-criterion-instead-0338e3bcc705))
- **半 Kelly**：保留约 75% 的增长率，但最大回撤砍掉约一半。几乎所有专业玩家/基金用 1/4 ~ 1/2 Kelly。([同上](https://medium.com/@tmapendembe_28659/the-dangers-of-full-kelly-criterion-why-most-traders-should-use-fractional-kelly-criterion-instead-0338e3bcc705))
- **为什么分数 Kelly**：全 Kelly **放大估计误差**——如果你对真实胜率的估计偏几个百分点，全 Kelly 会严重超注，风险骤升甚至走向 risk of ruin。分数 Kelly 是对"模型有误差"这一事实的对冲。([Kelly criterion — Wikipedia](https://en.wikipedia.org/wiki/Kelly_criterion))
- **同时持多注 / 相关性**：基础 Kelly 假设**逐注串行**下注。多注并发时若各自全 Kelly，合计可超 100% bankroll。粗暴修正：`Adjusted Kelly = Base Kelly ÷ 并发注数`；严格修正需考虑各注**相关性**做联合优化。([Wizard of Odds — Variance & Bankroll for Props](https://wizardofodds.com/article/variance-and-bankroll-for-player-props/) / [betstamp — Kelly](https://betstamp.com/education/kelly-criterion))

#### 1.2 波动率目标 (Vol Targeting) / 逆波动率
- **原理**：调整仓位使**组合波动率恒定**而非名义敞口恒定——波动率升则缩仓，降则加仓（必要时加杠杆）。常用 ATR 或历史波动率度量。([For Traders — Vol-Based Sizing](https://www.fortraders.com/blog/volatility-based-position-sizing-explained))
- **逆波动率配权**：资本按 1/σ 分配，低波动标的拿大仓。实证：等权 → 逆波动率(滚动 12 月 std)，Sharpe 0.99→1.54，最大回撤 −30.84%→−13.81%。([Quantified Strategies](https://www.quantifiedstrategies.com/volatility-based-position-sizing/))
- **Man Group 实证（关键，决定迁移边界）**：Vol targeting **只对股票/信用类资产**(及含其的 balanced / risk-parity 组合)改善 Sharpe，因为这些资产有"收益-波动负相关"的杠杆效应，**等价于叠了一层动量 overlay**。**对债券/汇率/商品 Sharpe 改善可忽略**。但**降左尾/降回撤对所有资产类都有效**——极端负收益通常发生在高波动时，那时 vol-target 已自动缩了敞口。([Man Group — Impact of Vol Targeting](https://www.man.com/insights/the-impact-of-volatility-targeting))

#### 1.3 风险平价 / 固定分数
- **风险平价**：按风险贡献(而非名义)均摊到各 leg。
- **固定分数 (fixed-fractional)**：每笔限定占总资本 1–3%；相关资产合计敞口设上限(如 2%)防同向放大。([Dynamic Position Sizing — ITI](https://internationaltradinginstitute.com/blog/dynamic-position-sizing-and-risk-management-in-volatile-markets/) / [Tradetron — Reducing Drawdown](https://tradetron.tech/blog/reducing-drawdown-7-risk-management-techniques-for-algo-traders))

### 2. 动态调仓 (Scaling / Pyramiding)

- **Scaling in/out**：分批建/平仓，按论点被逐步确认而加码、按目标达成而分批止盈。优点是平滑不确定性、分散执行；**核心难点在执行**——快市价差扩大会让分批成本远超预期，必须靠"干净规则 + 快速订单处理 + 限价位"控成本。([heygotrade — Scaling](https://www.heygotrade.com/en/blog/scaling-in-and-scaling-out/) / [gomarkets — Scaling](https://www.gomarkets.com/en-au/articles/scaling-in-and-scaling-out-advanced-position-management-for-every-market))
- **金字塔加仓 (pyramiding)**：只往**盈利**仓加码（如 50%/30%/20% 三段，价格每确认一段才加），是趋势跟踪的结构互补。**只在有清晰方向动量的市场有效；震荡市必被 whipsaw 反复打**。([Turtle Trader — Average Up](https://www.turtletrader.com/average-up/) / [QuantStrategy](https://quantstrategy.io/blog/the-ultimate-guide-to-pyramiding-strategy-in-trading/))
- **加赢家 vs 砍亏损 (asymmetry)**：金字塔加赢家 + 快砍亏损，制造"亏损小而有界、盈利让其长大"的非对称，长期复利。对立面"averaging down(亏损加仓)"被视为大忌（Paul Tudor Jones "losers average losers"）。([Turtle Trader](https://www.turtletrader.com/average-up/) / [Medium — Pyramiding vs Averaging](https://medium.com/@masoon.holdings/iconic-pic-of-paul-tudor-jones-with-losers-average-loosers-pasted-on-the-wall-1552acae650e))
- **趋势 vs 均值回归的持仓差异**：
  - **趋势**：进场顺势、趋势在则持有→**天然支持加仓**(捕大波段)，胜率低但盈亏比高。
  - **均值回归**：押"过度偏离会回归"，目标是**多笔小赢**，**不 scaling in**，胜率高但**尾部亏损可能很重**(回归不发生时)。([Quantified Strategies — MR vs TF](https://www.quantifiedstrategies.com/mean-reversion-vs-trend-following/))

### 3. 回撤控制 (Drawdown Control)

- **波动率调节敞口 = 主力工具**：极端波动期 vol-target 自动 de-risk，在风险最高时缩敞口，既降回撤又防 panic-selling。正确做的 position scaling 可在收益基本不变下砍 ~25% 最大回撤。([stoffelwealth — Vol Targeting](https://stoffelwealth.com/volatility-targeting-a-guide-to-stabilizing-portfolio-risk/) / [Tradetron](https://tradetron.tech/blog/reducing-drawdown-7-risk-management-techniques-for-algo-traders))
- **基于回撤的去险 (drawdown-based de-risking)**：账户/策略回撤触阈则系统性降杠杆，回升再恢复。是"动态仓位 + regime filter"的一部分。([Macrosynergy — Drawdown Control](https://macrosynergy.com/research/drawdown-control/) / [tradefundrr](https://tradefundrr.com/drawdown-control/))

---

## 二、迁移性分析

我们的范式（复述硬约束）：方向真值=赔率源 sharp；ML/量化不单独驱动加减仓；订单簿只做执行/库存/逆选，不判方向；阈值建概率空间；fee=shares×rate×p×(1−p)，rate≈0.03，限价不追；范式=`target_signed_notional + reservation 买卖价`，控制器 `order = target − current` 被动限价 rebalance；Kelly 定 target 规模；**无 rule-based 止损止盈**(都是 target 缩小/翻转副产品)；二元 CTF token 净空头用持对边表达；RM caps 账户级保命。

### A. 能迁移（与我们范式同构，建议采纳）

| 方法 | 为什么能迁 | 落地形态 |
|---|---|---|
| **分数 Kelly (1/4 ~ 1/2)** | 我们已用 Kelly 定 target。"模型有误差→必须打折"在我们这里**更强**，因为方向真值来自外部 sharp 但**概率估计仍有 de-vig/对齐误差**。 | Kelly 系数 λ 设 0.25–0.5；不要全 Kelly。配合现有 `λ` 参数。 |
| **并发注 Kelly 缩放** | 我们同时持多个市场/盘口，各自全 Kelly 会超账户。 | 对账户级总 target 做 `÷并发数` 或相关性折扣（同赛事多盘口高度相关）；归口 RM caps。 |
| **逆波动率/波动率目标（用于 sizing，不用于方向）** | Vol-target 是**纯规模层**调节，与方向解耦——完美契合"订单簿/价格波动只调规模不判方向"。 | 用市场价波动(或赔率源更新频率/价差)缩放 target 规模：盘口越乱→target 越小。**降尾/降回撤对所有资产类有效**，这条最稳。 |
| **基于回撤的去险** | 纯账户级保命门，正是 RM caps 的精神。 | RM 增"回撤触阈→全局 target 乘子<1"，回升恢复。不引入方向判断。 |
| **scaling in/out 的"分批 + 限价 + 控执行成本"思想** | 我们 `order = target − current` 被动限价 rebalance **本质就是连续版的 scaling**。 | 已具备。强化点：rebalance 步长别太激进，靠 reservation 价被动吃，呼应"快市别用市价"。 |
| **固定分数 / 相关敞口上限** | 账户级，与方向无关。 | RM caps 已有；补"同赛事/同联赛相关敞口上限"。 |

### B. 不能迁移 / 有坑（与架构哲学冲突或被费曲线/限价不追打死）

| 方法 | 为什么不能迁 / 坑在哪 |
|---|---|
| **金字塔加仓（加盈利仓）** | 本质是**用价格走势当方向信号**("市场顺我→加")。我们方向真值=外部 sharp，**加仓应由 sharp 概率变化驱动，而非我们持仓的浮盈**。把浮盈当加仓信号 = 让 ML/价格悄悄驱动方向，**违反"ML 永不单独驱动加减仓"**。✅正确替代：sharp 概率向我们方向移动 → target 自然变大 → 控制器自动加；这已经是我们范式的免费副产品，**不要再叠一层 pyramiding 规则**。 |
| **rule-based 止损 / 止盈 / scaling out 止盈** | 我们刻意**无 rule-based 止损止盈**(都是 target 缩/翻的副产品)。叠 rule-based 止损会与 target 控制器**打架**(止损平了又被 rebalance 拉回)。趋势系统的"砍亏损"靠的是固定止损位，我们对应物是 **target 缩小/翻转**，由 sharp 驱动，**别引入独立止损规则**。 |
| **averaging down（亏损加仓）** | 表面是大忌，但**我们这里要小心反向误判**：当 sharp 概率没变、只是我们持仓浮亏（市场价短期偏离），控制器**理应维持/补回 target**（这看起来像"亏损加仓"但其实是"维持 sharp 锚定的目标仓"）。**坑**：不能照搬"亏损绝不加仓"的教条，否则会在市价噪声偏离时错误砍掉 sharp 认可的仓位。判据永远是 **sharp 概率有没有变**，不是浮盈浮亏。 |
| **Vol-target 借股票"收益-波动负相关→动量 overlay"那部分增益** | Man Group：该 Sharpe 增益**只对股票/信用**成立，源于其特有的杠杆效应。我们是二元概率市场，**没有理由假设同样的负相关**。✅可迁的只有 vol-target 的**降尾/降回撤**部分（对所有资产类成立）。**别指望 vol-target 给我们带 alpha，只当它是风控**。 |
| **趋势 vs 均值回归的"加仓/不加仓"打法** | 这是**方向策略的分类**，我们不做方向分类——方向永远来自 sharp。这套二分法对我们**不适用**，最多作为"sharp 信号是延续型还是回归型"的描述性参考。 |
| **市价追价的任何 scaling 变体** | 费曲线 p(1-p) + 限价不追 = 任何"追价加仓/止损市价砍"都会**双重受损**(吃价差 + 付 fee)。所有调仓必须落到 reservation 限价被动成交。 |

### C. 额外的"费曲线特有"陷阱（成熟市场没有，但我们要注意）

- **fee = shares × rate × p × (1−p)** 在 p≈0.5 处最贵、在极端 p(接近 0/1)处近乎免费。这意味着：
  - **靠近 0.5 的盘口，任何高频 rebalance/scaling 的 fee 摩擦最重**——边际 edge 必须明显跑赢 0.03×0.25=0.0075/share 的峰值费率才动手。建议在 sizing/rebalance 阈值上**让"是否调仓"对 p(1-p) 敏感**：p≈0.5 时调仓门槛抬高，p 极端时门槛降低。
  - 成熟市场的连续价格没有这条"中间最贵"的成本曲面，所以它们的高频 scaling 经验**会系统性低估我们在 p≈0.5 区的摩擦**。

---

## 三、对我们的具体建议（可落地）

1. **Kelly 系数固定在分数 Kelly（λ ≈ 0.25–0.5），明确写进风控参数并禁全 Kelly。** 理由：方向虽锚 sharp，但概率估计(de-vig + 对齐)仍有误差，全 Kelly 放大误差→ risk of ruin。配合现有 λ 升级评审（已有 λ→0.35 待回测+老韩签的工作流）。

2. **对并发/相关持仓做 Kelly 缩放。** 同赛事多盘口、同联赛多场高度相关，account_equity 单一口径下用 `÷并发数` 或相关性折扣压总 target，归口 RM caps，防"各自半 Kelly 合计超账户"。

3. **引入"波动率/盘口噪声目标"作为纯规模乘子（不判方向）。** 用市场价波动 / 价差宽度 / 赔率源更新抖动算一个 [0,1] 乘子缩 target：盘越乱→target 越小。**定位为风控(降尾降回撤)，不当 alpha**。与现有"订单簿只做执行/库存/逆选"一致——它只调规模不碰方向。

4. **RM 增"基于回撤的全局 target 乘子"。** 账户/策略回撤触阈→全局乘子<1，回升恢复。纯账户级保命门，不引入方向判断，符合 RM caps 精神。

5. **明确写下"不引入"清单，固化架构边界**（防未来有人照搬股票套路）：
   - ❌ 不加 rule-based 止损/止盈（与 target 控制器打架；我们的"止损"=sharp 驱动的 target 缩/翻）。
   - ❌ 不加"加盈利仓/砍浮亏仓"的 pyramiding 规则（那是用价格当方向信号，违反 ML/价格永不单独驱动方向；正确加仓=sharp 概率移动→target 变大→控制器自动执行）。
   - ❌ 不照搬"亏损绝不加仓"教条（市价噪声偏离≠sharp 变化；判据永远是 sharp 概率，不是浮盈浮亏）。
   - ❌ 任何调仓不走 reservation 限价（费曲线+限价不追，市价追价双重受损）。

6. **让 rebalance/调仓阈值对 p(1-p) 敏感。** p≈0.5 区 fee 峰值，抬高"是否动手"门槛；p 极端区 fee 近零，门槛可降。这是成熟市场没有、必须本土化的一条。

---

## 附：来源
- [Medium — The Dangers of Full Kelly](https://medium.com/@tmapendembe_28659/the-dangers-of-full-kelly-criterion-why-most-traders-should-use-fractional-kelly-criterion-instead-0338e3bcc705)
- [Kelly criterion — Wikipedia](https://en.wikipedia.org/wiki/Kelly_criterion)
- [Wizard of Odds — Variance & Bankroll for Player Props](https://wizardofodds.com/article/variance-and-bankroll-for-player-props/)
- [betstamp — Kelly Criterion](https://betstamp.com/education/kelly-criterion)
- [Man Group — The Impact of Volatility Targeting](https://www.man.com/insights/the-impact-of-volatility-targeting)
- [Quantified Strategies — Volatility-Based Position Sizing](https://www.quantifiedstrategies.com/volatility-based-position-sizing/)
- [For Traders — Volatility-Based Position Sizing Explained](https://www.fortraders.com/blog/volatility-based-position-sizing-explained)
- [Turtle Trader — Average Up](https://www.turtletrader.com/average-up/)
- [QuantStrategy.io — Pyramiding Strategy](https://quantstrategy.io/blog/the-ultimate-guide-to-pyramiding-strategy-in-trading/)
- [Medium — Pyramiding vs Averaging](https://medium.com/@masoon.holdings/iconic-pic-of-paul-tudor-jones-with-losers-average-loosers-pasted-on-the-wall-1552acae650e)
- [Quantified Strategies — Mean Reversion vs Trend Following](https://www.quantifiedstrategies.com/mean-reversion-vs-trend-following/)
- [Macrosynergy — Drawdown Control](https://macrosynergy.com/research/drawdown-control/)
- [Tradetron — Reducing Drawdown](https://tradetron.tech/blog/reducing-drawdown-7-risk-management-techniques-for-algo-traders)
- [stoffelwealth — Volatility Targeting](https://stoffelwealth.com/volatility-targeting-a-guide-to-stabilizing-portfolio-risk/)
- [heygotrade — Scaling In and Out](https://www.heygotrade.com/en/blog/scaling-in-and-scaling-out/)
- [gomarkets — Scaling In and Scaling Out](https://www.gomarkets.com/en-au/articles/scaling-in-and-scaling-out-advanced-position-management-for-every-market)
- [International Trading Institute — Dynamic Position Sizing](https://internationaltradinginstitute.com/blog/dynamic-position-sizing-and-risk-management-in-volatile-markets/)
