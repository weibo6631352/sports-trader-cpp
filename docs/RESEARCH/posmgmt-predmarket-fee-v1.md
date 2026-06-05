> owner: (research agent for 老雷) · last_review: 2026-06-05

# 持仓管理调研 — 预测市场实践 + 手续费经济学

视角：预测市场（Polymarket / Kalshi）持仓管理 + 手续费经济学。
本视角重点：`fee = shares × rate × p × (1−p)` 这条曲线对「在哪进出 / 调仓频率 / 限价 margin / 避免手续费磨损」的量化含义。

与本系统硬约束的对齐：
- 方向真值=外部 sharp（Bet365 de-vig），ML 不单独驱动方向；订单簿只管执行/库存/逆选。→ 本视角不碰方向信号，只回答「给定方向已定，手续费如何决定该不该动、动多大、在哪个 p 动」。
- 已有框架：目标仓位连续控制 + Kelly + reservation 限价 `reservation_buy = fair − fee_per_unit − margin`，无 rule 止损止盈，可持有到结算或中途交易出。→ 本调研的 break-even 表直接喂 `fee_per_unit` 和 `margin` 的取值。

---

## 0. 权威结论先放：费公式与费率（已核对官方文档）

**Polymarket 官方公式**（[docs.polymarket.com/trading/fees](https://docs.polymarket.com/trading/fees)）：

```
fee = C × feeRate × p × (1 − p)
```
- `C` = 成交份额数（shares）
- `feeRate` = 类目费率
- `p` = 成交价 = 隐含概率（0..1）
- `p × (1−p)` = 伯努利方差，p=0.5 时最大 0.25，p→0/1 时趋零

**只有 taker 付费，maker 永远 0 费**（官方原文 "Makers are never charged fees. Only takers pay fees."）。这是本视角最重要的一条结构性事实，下面 §4 展开。

**Polymarket 类目 taker 费率**（官方表，2026）：

| 类目 | taker rate |
|---|---|
| Sports（我们的盘） | **0.03** |
| Crypto | 0.07（旧 doc 写 0.072，官方现表 0.07） |
| Finance / Politics / Tech | 0.04 |
| Economics / Culture / Weather / Other | 0.05 |
| Geopolitics | 0（免费） |

→ 我们系统 hardcode 的 sports rate=0.03 与官方一致；加密 0.072 是旧值，官方现表 0.07，差异不影响 sports 盘。

**Kalshi 用完全相同的曲线形状**（[marketmath.io/blog/kalshi-fees-guide-2026](https://marketmath.io/blog/kalshi-fees-guide-2026)）：`fee = 7% × p × (1−p)` per contract，taker 7% / maker ≈1.75%。两家独立平台都用 `rate × p(1−p)`，证明这是预测市场费设计的通用范式（费∝伯努利方差），不是 Polymarket 特例。

**结算免费**：两家都只在「主动成交」时收费，resolution（持有到结算）本身 0 费。这是「持有 vs 中途交易」成本权衡的根。

---

## 1. Break-even edge 表（本视角核心产出，rate=0.03 Sports）

定义：`fee_per_unit(p) = rate × p × (1−p)`，单位 = USDC/share（因为 1 share 在结算时付 0 或 1，单 share 名义=1）。
edge 也用同一单位（fair 与成交价的概率差，1 share 名义=1）。

**单边（taker 进场一次）break-even edge = `fee_per_unit(p)`** —— edge 必须 > 这个数才不亏手续费。

**往返（taker 进 + taker 中途平，两次付费）break-even edge = `2 × fee_per_unit(p)`**。

`rate = 0.03`：

| p（成交价） | p×(1−p) | fee_per_unit = 单边 BE edge | 往返 BE edge = 2×fee | 单边 BE（cents/share） |
|---|---|---|---|---|
| 0.50 | 0.2500 | **0.00750** | **0.01500** | 0.75¢ |
| 0.45 / 0.55 | 0.2475 | 0.00743 | 0.01485 | 0.74¢ |
| 0.40 / 0.60 | 0.2400 | 0.00720 | 0.01440 | 0.72¢ |
| 0.35 / 0.65 | 0.2275 | 0.00683 | 0.01365 | 0.68¢ |
| 0.30 / 0.70 | 0.2100 | 0.00630 | 0.01260 | 0.63¢ |
| 0.25 / 0.75 | 0.1875 | 0.00563 | 0.01125 | 0.56¢ |
| 0.20 / 0.80 | 0.1600 | 0.00480 | 0.00960 | 0.48¢ |
| 0.15 / 0.85 | 0.1275 | 0.00383 | 0.00765 | 0.38¢ |
| 0.10 / 0.90 | 0.0900 | **0.00270** | **0.00540** | 0.27¢ |
| 0.05 / 0.95 | 0.0475 | 0.00143 | 0.00285 | 0.14¢ |
| 0.02 / 0.98 | 0.0196 | 0.00059 | 0.00118 | 0.06¢ |

**读法（用问题里要的两个点）**：
- **p=0.5：单边 BE edge ≈ 0.75¢/share（0.0075），往返 ≈ 1.5¢/share（0.015）**。
- **p=0.9：单边 BE edge ≈ 0.27¢/share（0.0027），往返 ≈ 0.54¢/share（0.0054）**。
- 对称：p 和 1−p 费完全相同（buy YES@0.30 == buy YES@0.70 同费）。
- p=0.5 比 p=0.9 贵 **2.78 倍**（0.25 / 0.09）。

**含义**：同样名义、同样信息，p=0.5 附近吃边最狠，p→0/1 附近近乎免费。要 break-even，**中段需要的 edge 是边缘段的 2-3 倍**。

> ⚠️ 行业经验法则（[bettorsinsider](https://bettorsinsider.com/predictions/guides/prediction-market-fees/)）：实操 edge 应 ≥ **3× fee drag**，因为对 fair 的估计本身有误差，留 buffer。即 p=0.5 单边真正想动手的 edge 门槛 ≈ 3×0.0075 = **2.25¢/share**，不是裸 0.75¢。Sports rate 低（0.03），所以这个 3× 门槛仍很温和；若是 crypto（0.07）门槛会到 ~5¢。

---

## 2. break-even edge 对「在哪个 p 区间持仓/进出」的含义

把 §1 的曲线翻译成区间策略（参考 [coinmonks 费曲线 bot 指南](https://medium.com/coinmonks/polymarket-just-changed-its-fees-heres-what-bot-traders-need-to-know-c11132e55d5c)）：

| p 区间 | 占峰值费比例 | 策略含义 |
|---|---|---|
| **0.40–0.60（中段）** | 90–100% | 最贵。只在 edge 厚（≥3× fee）时进；中途平仓尤其贵（往返 1.5¢）。倾向「进了就拿着到结算」少折腾 |
| **0.15–0.40 / 0.60–0.85（折扣段）** | 50–90% | 大多数方向策略的甜区。费下降明显，进出更自由 |
| **<0.15 / >0.85（极端段）** | <50% | 近乎免费。tail 事件 / 近确定套利 / neg-risk 收尾的低成本区，往返 BE 只 ~0.3–0.5¢ |

**对我们的直接含义**：sharp 给方向后，**同一 edge 在中段（p≈0.5）值的钱比在边缘段少**——因为净 EV = edge − fee，中段 fee 大。若 sharp edge 在多个盘上接近，**优先把名义投到 p 偏离 0.5 的盘**（净 EV 更高），中段盘要求更厚 edge 才进。这可以做成 sizing 的一个乘子：`net_edge(p) = raw_edge − rate×p×(1−p)`，直接喂 Kelly 分数。

---

## 3. 频繁 rebalance 会不会被手续费吃掉 + 怎么防磨损

**会，而且是最大的隐性漏损。** 目标仓位连续控制若每 tick 都把仓位往目标拉，每次拉都是一次 taker 成交 = 一次 `fee_per_unit(p)`。在 p=0.5、rate=0.03 下，每次微调付 0.0075/share；一天若被噪声驱动调几十次，磨损叠加可超过整笔 edge。Kalshi 数据（[marketmath](https://marketmath.io/blog/kalshi-fees-guide-2026)）的判据可直接借用：

> **「若中途平仓的预期收益 < 2× taker fee，就持有到结算」**（settlement 免费）。

这等价于：**中途动手的收益必须能覆盖一次往返费**，否则别动。落到我们：

1. **min_rebalance 阈值（死区）**：当前仓位与目标仓位的差，只有当「这次调整的预期 EV 改善 > 该 p 下的单边 fee（或往返 fee，取决于是加仓还是反向）」才执行。死区宽度应随 p 缩放：中段 p≈0.5 死区最宽（fee 最贵，0.0075），边缘段可收窄（fee 便宜）。**死区不是常数，是 `k × rate × p × (1−p)`**。
2. **不要让噪声驱动 rebalance**：方向真值=sharp，sharp 没动就别因订单簿抖动去调目标仓位——否则纯付费给做市商。这条与系统「订单簿不判方向」硬约束天然一致。
3. **能挂 maker 就别吃 taker**：每次 rebalance 优先用 reservation 限价挂单（maker，0 费 + rebate），而不是市价吃单（taker，付全费）。这把 §1 整张表的成本**从 taker 列降到 0**（只要能成交）。

**fee 与 reservation margin 的关系**：现有 `reservation_buy = fair − fee_per_unit − margin` 已经把 `fee_per_unit(p)` 减进限价了——这正确，意味着挂出的限价已经为「假设自己是 taker」预留了费。但如果实际成交为 maker（限价被别人吃），这笔 fee 预留就变成了额外安全垫/利润，是好事。**建议保留 `fee_per_unit` 项**（它让限价随 p 自动变宽变窄：中段挂得更保守、边缘段更激进，正好匹配费曲线），`margin` 单独作为逆选/库存 buffer。

---

## 4. maker vs taker：本视角最大的结构性杠杆

两家平台都是 **maker 0 费 + rebate**（Polymarket：非 crypto 25% / crypto 20% 的 taker 费返还；Kalshi maker≈1.75% vs taker 7%）。引用 [start polymarket 做市指南](https://startpolymarket.com/strategies/market-making/)：「一个 maker 即使在 mid 平价成交、spread 上 P&L 为零，光 rebate 就净正」。

**含义**：
- §1 整张 break-even 表是 **taker 视角的成本**。只要我们用限价挂单（reservation 本就是限价），成交为 maker → **这张费表的成本全部归零**，break-even edge 降到 ~0（甚至负，因有 rebate）。
- 这把「频繁 rebalance 被吃费」的问题大幅缓解：**只要 rebalance 走挂单不走吃单，磨损≈0**。代价是成交不确定（限价可能不被吃）。
- 因此持仓管理的核心权衡不是「调不调仓」，而是「**这次调仓愿不愿意吃 taker 费换确定成交，还是挂 maker 等成交**」。紧急减仓（逆选/库存超限）才值得吃 taker；常规目标仓位收敛应尽量 maker。

---

## 5. 持有到结算 vs 中途交易 / resolution 风险（持有成本权衡）

**成本侧**：
- 持有到结算 = 只付一次进场费（且若进场是 maker，0 费），settlement 免费。**最省费路径**。
- 中途平仓 = 付两次费（往返），§1 表的「往返 BE edge」列。专业经验（[polymarkets.co.il 卖出指南](https://polymarkets.co.il/en/guide/selling-positions/)）：60–70% 规则——捕获理论最大收益的 60–70% 就走，最后 20% 不值时间+尾部风险。

**风险侧（为什么不总是持有到结算）**：
- **逆选/resolution 风险**：二元盘临近结算，决定性信息到来概率高，价格从中段「不连续地、灾难性地」跳到 0 或 1（[start polymarket 做市指南](https://startpolymarket.com/strategies/market-making/)）。临近结算的盘，一条新闻能抹掉数周 spread 收入。
- **做市铁律：临近 resolution 退场/收窄**。这与本系统已落地的「直播 final/终态→退订+释放 hub+拉黑」（见 MEMORY: subscription-lifecycle-blacklist）方向一致——临近终态降低暴露、避免拿着 stale 报价被逆选。

**权衡落点**：
- 体育盘有明确结算时点（赛果），不像永续可无限持有 → 必须主动管库存，不能指望均值回归（Kalshi 做市经验）。
- **默认偏向持有到结算**（省往返费），仅当 (a) 已捕获大部分 edge（60–70% 规则）或 (b) 逆选/库存风险触发，才吃 taker 中途平。判据用 §3 的「收益 > 2× fee 才中途平」。

---

## 6. neg-risk（互斥多结果）套利与持仓

体育有大量互斥多结果盘（谁夺冠 / 系列赛赢家 / Outright），Polymarket 有 **NegRiskAdapter** 智能合约支持（[start polymarket neg-risk](https://startpolymarket.com/learn/converting-negative-risk/)，[Polymarket/neg-risk-ctf-adapter](https://github.com/Polymarket/neg-risk-ctf-adapter)）：

- **convert**：burn 一组 NO → 释放 PUSD（或换成特定 YES）。
- **资本效率**：neg-risk 集买全部 outcome 只需 1.0 抵押（最大赔付），普通市场要 Σprice。**互斥盘的库存抵押显著降低** → 同等资金能持更大组合仓位。
- **convert 套利**：当一组 YES 价格和 < $1，买全 YES 必有一个结算 $1，锁定无风险利润（持有到结算）。这是**纯 maker/limit 即可、且天然持有到结算（省费）**的低风险持仓。
- **费上的好处**：neg-risk 收尾常发生在极端 p（某 outcome 已近 0 或近 1），正处于 §1 表的**低费区**，往返成本极小。

学术参考：套利在预测市场的系统研究 [arxiv 2508.03474](https://arxiv.org/abs/2508.03474)、Polymarket NBA 套利 [arxiv 2605.00864](https://arxiv.org/pdf/2605.00864)。

---

## 7. 给本系统的可落地建议（量化）

1. **net_edge 减费再喂 Kelly/进场门**：`net_edge(p) = raw_edge − rate×p×(1−p)`，进场门用 **≥ 3× fee_per_unit(p)** 的 buffer（误差容忍）。p=0.5 sports 门 ≈ 2.25¢/share，p=0.9 ≈ 0.81¢/share。中段要求厚 edge，边缘段放宽。
2. **rebalance 死区随 p 缩放，不是常数**：`deadband = k × rate × p × (1−p)`（k≈1 单边 / k≈2 反向往返）。仓位差小于死区不动手。落地「收益 > 2× fee 才中途交易」铁律，杜绝噪声驱动磨损。
3. **常规收敛走 maker，紧急走 taker**：目标仓位收敛优先 reservation 限价挂单（0 费+rebate，把 §1 整表成本归零）；仅逆选/库存超限/临近结算时才吃 taker 加速。这是本视角最大杠杆。
4. **保留 reservation 里的 `fee_per_unit(p)` 项**：它让限价随 p 自动变宽/变窄，正好匹配费曲线（中段更保守、边缘段更激进）；`margin` 单独留给逆选/库存 buffer，二者别合并。
5. **持仓默认偏持有到结算（settlement 免费），中途平仓只在 (60–70% edge 已捕获) 或 (逆选/库存触发)**；临近 resolution 主动降暴露/退订（已与现有终态退订逻辑一致）。互斥盘优先用 neg-risk convert 持有到结算（低费区 + 低抵押）。

---

## 来源

- [Polymarket 官方 — Trading Fees（公式+类目费率+worked example）](https://docs.polymarket.com/trading/fees)
- [Polymarket Help — Trading Fees](https://help.polymarket.com/en/articles/13364478-trading-fees)
- [Polymarket Help — Can I Sell Early?](https://help.polymarket.com/en/articles/13364247-can-i-sell-early)
- [coinmonks — 费曲线 bot 交易指南（区间/break-even/maker 优势）](https://medium.com/coinmonks/polymarket-just-changed-its-fees-heres-what-bot-traders-need-to-know-c11132e55d5c)
- [Start Polymarket — Market Making（库存管理/逆选/spread vs fee/rebate）](https://startpolymarket.com/strategies/market-making/)
- [Start Polymarket — Negative Risk & Convert](https://startpolymarket.com/learn/converting-negative-risk/)
- [Polymarket/neg-risk-ctf-adapter（合约）](https://github.com/Polymarket/neg-risk-ctf-adapter)
- [Market Math — Kalshi Fees 2026（7%×p(1−p)，2× fee 持有判据）](https://marketmath.io/blog/kalshi-fees-guide-2026)
- [Bettors Insider — Prediction Market Fees（edge ≥ 3× fee drag 经验法则）](https://bettorsinsider.com/predictions/guides/prediction-market-fees/)
- [polymarkets.co.il — Selling Positions（60–70% 规则）](https://polymarkets.co.il/en/guide/selling-positions/)
- [arxiv 2508.03474 — Arbitrage in Prediction Markets](https://arxiv.org/abs/2508.03474)
- [arxiv 2605.00864 — Arbitrage in Polymarket NBA Markets](https://arxiv.org/pdf/2605.00864)
- [News Kalshi — Market Making on Kalshi](https://news.kalshi.com/p/market-making-on-kalshi)
