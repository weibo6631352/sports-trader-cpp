# Polymarket 双边套利方案 — 协议/机制可行性参考 v1

> **owner:** 老李 (Polymarket 协议专家, A 系统工程部)
> **last_review:** 2026-06-03
> **召唤背景:** 老板 2026-06-03 战略转向 —— 不预测结算 outcome, 改为预测未来 Δt 价格走势 → 单市场 YES/NO 双边仓位管理 → 套利。本文把"双边套利在 Polymarket CTF 机制下可不可行"彻底搞清。
> **方法论:** 三档可信度区分 —— `[代码]` = 仓库代码这么实现 / `[实测]` = 团队真网验证过 / `[文档]` = 官方文档确认 / `[待核实]` = 需 GM 用 WebFetch 去官方文档核实(我无 WebFetch)。
> **配套前情:** `laolei-short-horizon-arb-master-plan-v1.md`(老雷) / `shorthorizon-locking-feasibility-v1.md`(小董) / `quant-microstructure-eventdelay-arb-feasibility-v1.md`(小袁)。本文补的是**机制/协议层**, 那三份是**数据/延迟可行性层**。

---

## §0 先把"双边套利"这个词拆清楚 —— 老板论点里其实混了两个东西

老板新论点 = "预测 Δt 价格走势 → YES/NO 双边仓位管理 → 套利"。在 Polymarket CTF 机制下,"套利"这个词对应**三种完全不同的机制**, 可行性天差地别, 必须分开论:

| 类型 | 机制 | 是否真"无风险套利" | 现实可行性 |
|---|---|---|---|
| **A. CTF 结构套利** (YES+NO≠1) | 同一 condition 内 YES_ask+NO_ask<1 买双边锁 1 元 / YES_bid+NO_bid>1 卖双边 | **是**, 数学无风险(到结算必赚价差) | `[代码]`已算出 `x_arb_free_edge`; **几乎永远不出现可盈利缺口**(做市商秒杀), 见 §1.4 |
| **B. 时间维度方向套利** (预测 Δmid) | 预测未来短窗 mid 移动方向, 单边吃单 + 短窗内平仓 | **否**, 是有风险方向性押注(裸方向暴露) | 这才是老板真实意图; 边际可行, 是**事件延迟套利**, 见前三份报告 |
| **C. 跨市场/跨平台套利** | neg_risk 互斥组内 / 跨 PM-其他平台 同事件定价差 | 介于两者间 | neg_risk 组内有真结构(§1.5); 跨平台见 `xiaocheng-w10-w1-p0-03` |

**关键判断(老李):** 老板说的"双边仓位管理 + 套利"在工程语言里**不是 A 类无风险套利**。A 类在 Polymarket 上是做市商的午餐, 我们抢不到。老板真正要的是 **B 类 —— 用"双边都看得到、双边都能持"的信息(C3 双边快照原则)做方向预测, 短窗进出**。"双边仓位管理"指的是**库存管理 / 平仓选择**(可以买对边而非卖本边来平), 不是"同时锁 YES+NO 吃无风险缺口"。

这个区分是整篇文档的地基。下面逐条把机制说死。

---

## §1 YES/NO 双边的机制真相 (CTF 二元市场)

### 1.1 基础结构: ConditionalTokens (CTF) 二元 outcome

`[文档/实测]` Polymarket 每个二元市场底层是 Gnosis **Conditional Tokens Framework (CTF)** 合约。一个 condition 对应**两个独立的 ERC-1155 positionId(token)**:
- **YES token** (outcome index 0)
- **NO token** (outcome index 1)

`[代码]` 仓库里 `clobTokenIds` 解析出 `(tok0=YES, tok1=NO)`(`market_discovery.cpp:260`, `paper_daemon.cpp:221`)。`signer_iface.hpp` 注释明确 outcome(YES/NO) → token_id(uint256 decimal)。这两个 token 在 CLOB 上是**两个完全独立的订单簿**,各自有 bid/ask/深度。

`[文档]` 结算时: 赢的 outcome token 每股赎回 **1 USDC**, 输的 **0 USDC**。这是 "YES+NO=1" 的来源 —— **不是订单簿恒等式, 是结算恒等式**。订单簿上 YES 价 + NO 价**可以临时 ≠ 1**(这正是 A 类套利的来源)。

### 1.2 能不能同时持 YES 和 NO? —— 能, 而且这是关键

`[代码]` 仓库明确支持且建模了双边同持:
> `quote_snapshot_hub.hpp:161`(原文粘): "二元市场 YES/NO 是两个独立 token, 做市/对冲会两边都持; 30 YES+30 NO = 30 锁定对 (风险≠净 0)。"

`pos_yes_qty` / `pos_no_qty` 分别记两边持仓, 不塌成净值。**同时持 30 YES + 30 NO 是合法状态**, 经济含义 = 锁定 30 份"无论谁赢都赎回 30 USDC"的头寸(减去买入成本)。

**这对方案的含义:**
- 持两边 = **锁定**: 若你以 YES_ask + NO_ask = 0.98 买入双边, 结算必得 1.00, 锁 0.02/对 - fee。这是 A 类套利。
- 持两边 ≠ 风险中性的"净 0": 仓库注释强调 "风险≠净 0" —— 因为两个 token 各自有市值波动, 在结算前你若想平仓, 卖出价取决于各自订单簿, **不能假设 YES市值+NO市值 恒 =1**(中间有 vig/spread)。

### 1.3 能不能"卖空"某一边? —— 不能裸卖空, 只能"卖你持有的"或"买对边等价"

`[文档/待核实]` 这是机制核心坑, 必须说死:
- Polymarket CLOB 的 SELL 单 = **卖出你已持有的 outcome token**(类似现货卖出), **不是保证金裸卖空**。你不能凭空 SELL 一个你没有的 YES token 来开空头。
- **要做空 YES 的经济等价 = 买入 NO**(因为 NO 涨 = YES 跌)。这是 CTF 二元市场的"做空"唯一干净路径。
- `[代码]` `binary_market_snapshot.hpp:62` 印证: `DecisionSide.side` 注释 "M1 恒 Buy; M2 开放 Sell (sell-to-open 空头)" —— 但**"sell-to-open 空头"这个表述在 CTF 机制下需要前置 split**(见 §1.4), 不是直接裸卖。现状 M1 桩恒 Buy, sell-to-open **尚未实现且机制上有前提**。

> **`[待核实]` 关键问题 Q1:** Polymarket CLOB 是否支持"卖出你不持有的 token"(裸卖, 靠合约从你 USDC 余额扣保证金 mint 出反向)? 还是 SELL 严格要求 inventory? py-clob-client 的 SELL 单语义。**这决定 sell-to-open 路径成不成立。**

### 1.4 merge / split: USDC ↔ YES+NO —— A 类套利的真实执行机制

`[文档]` CTF 合约提供两个原子操作(链上, 非 CLOB 订单):
- **split (mint)**: 存入 1 USDC(实际是 collateral) → 铸出 1 YES + 1 NO。
- **merge (burn)**: 销毁 1 YES + 1 NO → 赎回 1 USDC。

**这对双边套利意味着什么(老李重点):**
1. **split 是"凭空造出双边头寸"的合法路径** —— 你可以 split 出 1 YES + 1 NO, 然后在 CLOB 上**卖掉其中一边**, 等价于"开了另一边的空头 + 付了 1 USDC 本金"。这就是 §1.3 "sell-to-open 空头"在机制上的真实前提: **先 split 再 sell**。
2. **A 类无风险套利的两个方向:**
   - 若 CLOB 上 **YES_ask + NO_ask < 1**: 各买一份 → 持双边 → merge 回 1 USDC(或等结算)。成本 = YES_ask + NO_ask + fee, 收益 = 1。`lock = 1 - (YES_ask+NO_ask)` `[代码]` 已算(`paper_loop.cpp:1668`)。
   - 若 CLOB 上 **YES_bid + NO_bid > 1**: split 出 1 YES + 1 NO(花 1 USDC) → 同时卖掉两边收 (YES_bid+NO_bid)。`lock = (YES_bid+NO_bid) - 1` `[代码]` 已算(`paper_loop.cpp:1670`)。
3. **`x_arb_free_edge` = max(以上两者, 0)** `[代码]` `paper_loop.cpp:1671`。这个数 >0 时**理论上存在无风险锁定空间**。

> **`[待核实]` 关键问题 Q2:** merge/split 是否要付 **gas**(Polygon, 很便宜但非 0)? 是否经 Polymarket 的某个 router 合约还是直连 Gnosis CTF? **有没有 USDC↔USDC.e 的 collateral 细节**(Polymarket 用的 collateral 是哪个 ERC20)? 这决定 A 类套利的真实成本下界。
> **`[待核实]` 关键问题 Q3:** split/merge 是否能通过 py-clob-client 或必须自己发链上 tx(我们目前**完全没有这条链路** —— 仓库只有 CLOB 下单, 没有 CTF 合约调用)?

**老李判断:** A 类套利 (`x_arb_free_edge>0`) 在体育大盘口上**几乎永远不出现可盈利缺口**。原因:
- 做市商和现成的 arb bot 用 <50ms 链路秒杀任何 YES_ask+NO_ask<0.997 的缺口。
- 我们扣掉 fee(往返 ~1.5c) + gas + 链路延迟(跨洋, §小袁报告 CLOB POST p50 60ms)后, 缺口要 >2c 才够本, 这种缺口在流动盘口寿命 <100ms。
- **结论: A 类不作为主策略, 只作为"双边健康度特征"喂模型**(老板原意, `paper_loop.cpp:1661` 注释 "喂特征让模型自主判断, 非硬规则")。这个判断和老板把它做成 feature 而非硬规则的设计**完全一致**。

### 1.5 neg_risk 市场的特殊性

`[代码/文档]` neg_risk(negative risk)= 多 outcome 互斥事件(如"谁赢冠军", 多个候选), 用一个父合约 `negRiskMarketID` 把多个二元子市场绑成互斥组:
- `[代码]` `market_discovery.cpp:449,603` 解析 `negRiskMarketID`; `binary_market_snapshot.hpp:48` 存 `neg_risk_market_id`(可空)。
- `[代码]` 签名层有**独立的 neg_risk Exchange 合约地址**: `signer_v62.hpp:204` `kCtfExchangeV2NegRisk = 0xe2222d279d744050d28e00520010520000310F59`(普通市场是 `0xE111…`)。`[实测]` 这点已在实盘签名验证(memory `polymarket-clob-v2-live-order`)。

**neg_risk 的套利特殊性:**
- 互斥组里 N 个 outcome 的 YES 价**理论和 = 1**(必有且只有一个赢)。可以做"N 边都买, 和 < 1"的多边 A 类套利, 或"卖出某个明显高估的 outcome"。
- `[文档/待核实]` neg_risk 有特殊的 **NegRiskAdapter** 合约, 支持"用 N-1 个 NO 合成 1 个 YES"之类的转换。**这是比二元更复杂的套利面, 但也更难抢。**

> **`[待核实]` 关键问题 Q4:** neg_risk 市场的 merge/split/convert 语义(NegRiskAdapter 的 `convertPositions` 等)如何工作? 体育里哪些盘口是 neg_risk(outright/futures/系列赛冠军?), 哪些是普通二元(单场 moneyline 应该是普通二元)? 这决定我们体育主战场(单场 ML/totals/spreads)是否要碰 neg_risk 复杂度 —— **我的预判: 单场盘口都是普通二元, neg_risk 只在 outright/futures, 可暂时不碰**, 但需文档确认。

---

## §2 订单 / 撮合机制

### 2.1 订单类型 (orderType)

`[实测]` 仓库实盘下单只用过 **FOK**(`polymarket-clob-v2-live-order` memory: wire body `"orderType":"FOK"`)。`[代码]` `clob_wire.cpp:87` `orderType` 是可变字段。官方 CLOB 支持的完整类型:

| 类型 | 语义 | 对双边做市的用途 |
|---|---|---|
| **GTC** (Good-Til-Cancelled) | 限价挂单, 留在簿上直到成交或撤销 | **做市必用** —— 双边挂 maker 单 |
| **GTD** (Good-Til-Date) | 带过期时间的 GTC | 短窗策略可用(到期自动撤) |
| **FOK** (Fill-Or-Kill) | 全量立即成交否则全撤 | 我们现在唯一实盘验证过的; taker 吃单 |
| **FAK** (Fill-And-Kill) | 立即成交能成的部分, 剩余撤销(= IOC) | taker 吃单, 容忍部分成交 |

`[待核实]` **关键问题 Q5:** 上面 GTC/GTD/FOK/FAK 的确切字符串值和语义, 以及**postOnly** 标志(`clob_wire.cpp:89` 我们现在硬编 `postOnly:false`)的作用 —— postOnly=true 是否保证只做 maker(撞到对手价就拒而非吃单)? 做市策略需要 postOnly 来确保拿 maker 而非 taker。

### 2.2 maker vs taker

`[文档/待核实]`
- **taker** = 吃掉簿上已有挂单(我们的 FOK/FAK 单, marketable 限价单)。
- **maker** = 挂单在簿上等别人吃(GTC postOnly)。
- 撮合规则: 价格优先 + 时间优先(标准 CLOB)。
- **maker/taker 区分直接决定 fee**(见 §3)—— 这是双边做市可行性的命门。

### 2.3 tick size (价格最小变动)

`[代码/实测]` 默认 tick = **0.01**(1 美分), `paper_loop.cpp:633/1296/1341`, `fill_rate_model.cpp:185` 注释 "Polymarket tick = 0.01"。
`[代码]` 但 tick **是 per-market 动态的**, 通过 `/tick-size` 端点 + WSS `tick_size_change` 事件获取(`polymarket_clob_subscriber.cpp:426`)。`[文档]` 已知 Polymarket 对**极端价位**(接近 0 或 1)用更细的 tick(0.001), 中间价位用 0.01。

> **`[待核实]` 关键问题 Q6:** tick size 的确切分档规则 —— 哪些价格区间用 0.01, 哪些用 0.001? 是否有 0.0001? **这对极端价位策略(小袁报告认定的甜区 mid<0.15/>0.85)极其重要** —— 极端价位若 tick=0.001, 则 1.7c 的 BE 在 0.001 tick 下有 17 个 tick 的腾挪空间; 若 tick=0.01 则只有 1-2 tick, 挂单空间被压死。

### 2.4 最小订单额

`[实测]` 可成交(marketable/FOK) BUY 硬下限 **$1 USDC notional**(makerAmount ≥ 1e6 micro), <$0.1 做不出真实 fill(memory `polymarket-clob-v2-live-order`)。
`[待核实]` **关键问题 Q7:** maker(GTC 挂单)的最小订单额是否也是 $1? 是否有最小 share 数(而非 notional)限制? 极端价位($0.02 的 token)挂 1 share 才 $0.02, 远低于 $1 —— 极端价位的最小单约束是按 notional 还是 share?

### 2.5 双边挂单做市可行吗?

**老李判断(机制层):** 机制上**可行**(GTC + postOnly 双边挂), 但有三个硬约束:
1. **fee 结构**(§3)—— 若 maker 也收费且无 rebate, 双边做市赚的 spread 被 fee 吃掉。
2. **tick 粒度**(§2.3)—— near_half 盘口 spread 中位 5c(小董报告), 有腾挪空间; 极端价位 spread 1c = 1 tick, 几乎没有挂单空间。
3. **逆向选择**(adverse selection)—— 我们挂的 maker 单被"知道进球了"的人吃掉, 正是我们想做的事件延迟套利的反面。做市 = 被延迟套利者吃。

---

## §3 费用结构 (决定套利可不可行 —— 最关键一节)

### 3.1 fee 公式与来源

`[实测/文档]` 权威来源 = gamma market 对象的 `feeSchedule`(memory `polymarket-fee-source`, 官方 docs 2026-03-31):
```
feeSchedule = {exponent, rate, takerOnly, rebateRate}
fee_per_share(p) = rate × p × (1 − p)        # 峰值在 p=0.5
往返(双边/进出) fee = 2 × rate × p × (1−p)    # 代码 paper_loop.cpp:1943
```
`[代码]` `ExtractFeeRateCoef`(`market_discovery.cpp:135`)解析 `feeSchedule.rate`, 钳 [0, 0.10]; `feesEnabled:false` → fee=0(老市场免费)。
`[实测]` 体育 rate ≈ **0.03**(NHL 实测 `feeType:sports_fees_v2, rate:0.03`), 加密 0.072, general 0.05。

**fee 的量级感(老李算给方案组):** rate=0.03, p=0.5(near_half):
- 单边 fee = 0.03 × 0.5 × 0.5 = **0.0075 = 0.75c/share**
- 往返 fee = **1.5c/share**
- 极端价位 p=0.1: 单边 fee = 0.03 × 0.1 × 0.9 = 0.0027 = **0.27c**, 往返 **0.54c**(p(1-p) 抑制让极端价位 fee 天然低 —— 对极端价位甜区是利好)

### 3.2 maker 收不收费 / rebate / 流动性奖励 —— 三个未决命门

`[代码]` `feeSchedule` 里有 `takerOnly` 和 `rebateRate` 两个字段, **但我们代码只读了 `rate`, 完全没解析 `takerOnly` / `rebateRate`**。这是当前认知最大空白。

> **`[待核实]` 关键问题 Q8(最高优先级):** `feeSchedule.takerOnly` 的语义 —— **是否 true 表示"只对 taker 收费, maker 免费"**? 若是, 则双边做市(全 maker 单)可能 **fee=0**, 这彻底改变 B 类策略经济性。
> **`[待核实]` 关键问题 Q9:** `feeSchedule.rebateRate` 的语义 —— 是否是 **maker rebate**(挂单成交反而拿钱)? 数值多少? 这是做市策略的命根。
> **`[待核实]` 关键问题 Q10:** Polymarket 是否有独立的 **Liquidity Rewards Program**(流动性奖励, 按挂单贴近 mid 的程度和时长发奖励)? 规则、是否覆盖体育盘口、如何申领? 这是很多 Polymarket 做市商的真实利润来源, 可能比 spread 本身更大。

### 3.3 双边往返真实总成本(在 Q8-Q10 未核实前的保守口径)

`[代码/小董报告]` 当前 BE(盈亏平衡)口径:
```
BE = 往返 fee + spread(ask−bid) + slip_est       # arb_signal.hpp:70
```
- near_half(p≈0.5): 往返 fee 1.5c + spread 中位 5c = **BE 6.45c**(小董报告, 杀手级高)
- 极端价位(p≈0.1): 往返 fee 0.54c + spread 中位 1c = **BE ~1.7c**(小袁报告甜区)

**这个 BE 假设我们是 taker(吃 spread)。若 Q8 证实 maker 免费 + Q9/Q10 有 rebate, 则做市口径下 BE 可能降到接近 0 甚至负(拿 rebate)** —— 这会让 near_half 盘口也重新可行。**所以 Q8-Q10 是整个方案经济性的分水岭, 必须最高优先核实。**

---

## §4 撮合 / 结算 / 状态机

### 4.1 市场生命周期

`[代码/文档]` gamma market 状态字段: `active` / `closed` / `archived` + CLOB 侧 `accepting_orders`。
`[代码]` `market_discovery.cpp:509,584`(原文): "2026-06-02 老板「和官方对齐」: 不再发现层剔除 completed/死盘 —— 全部 active 盘都发现"。发现查询用 `?closed=false&active=true`(`market_discovery.cpp:644`)。

典型生命周期: **active(交易中) → closed(停止交易, 等结果) → resolved(结算, oracle 报告 outcome) → 可 redeem**。

> **`[待核实]` 关键问题 Q11:** `active` / `closed` / `acceptingOrders` / `enableOrderBook` 的确切组合语义。一个市场"in-play 交易中"对应哪几个 flag? "暂停"对应什么? 我们需要精确知道"何时还能下单"。

### 4.2 in-play 期间是否一直可交易 / 暂停机制

`[待核实]` **关键问题 Q12(对事件延迟套利极关键):** Polymarket 体育市场在 in-play 期间:
- 是否**全程开放交易**, 还是在某些时刻(进球后、关键判罚)**自动暂停撮合**(suspend)? 很多体育博彩平台在比分变化瞬间会 freeze 几秒 —— **如果 Polymarket 也在进球后暂停, 那我们的"事件延迟套利"窗口可能根本下不进单**, 这会直接证伪整个 B 类方案。
- 暂停时挂单怎么处理(保留还是撤销)? 恢复后价格怎么开?

**老李判断:** 这是 B 类方案**最危险的未知**。小袁报告算了延迟预算(~3.1s vs PM 重定价 13.2s 有 10s 窗口), 但**那个分析隐含假设了"进球后市场一直开放可下单"**。如果 PM 在进球后 suspend 撮合, 整个延迟预算无意义。**Q12 必须在写任何生产代码前用真实 in-play 数据 + 官方文档双重核实。**

### 4.3 结算时持仓怎么处理

`[代码]` `paper_loop.hpp:735` 注释: "比赛 Ended → 按终态比分把 YES/NO 持仓 realize 到结算值 (winner 1 / loser 0)"。`[文档]` 链上: resolved 后持赢方 token 调 `redeemPositions` 换 1 USDC/share, 输方 token 归 0。
`[待核实]` **关键问题 Q13:** 结算到可 redeem 的延迟(oracle/UMA 报告 + 争议期)? 体育结算用 UMA optimistic oracle 还是 Polymarket 自动结算? 争议期内资金锁定多久? 这影响"持仓到结算"的资金占用成本(但 B 类短窗策略本来就不持到结算, 影响小)。

---

## §5 代码库认知 vs 官方文档 —— 对齐表 + 差异标注

| 协议点 | 代码/实测认知 | 可信度 | 与官方文档差异/空白 |
|---|---|---|---|
| CTF 二元 = 2 独立 token | `clobTokenIds(YES,NO)` 解析 | `[实测]`高 | 一致 |
| YES+NO=1 是结算恒等(非簿恒等) | `x_arb_free_edge` 算缺口 | `[代码]`高 | 一致, 缺口实际寿命待核 |
| 同持双边合法 | `pos_yes_qty/pos_no_qty` 分记 | `[代码]`高 | 一致 |
| 卖空 = 买对边 / split 后卖 | M2 "sell-to-open" 桩 | `[代码]`**低** | **未实现, 机制前提(split)未接** → Q1/Q3 |
| merge/split (USDC↔YES+NO) | **代码完全没有** | — | **整条 CTF 合约调用链路缺失** → Q2/Q3 |
| neg_risk 独立 Exchange 合约 | `0xe2222…` | `[实测]`高 | 地址实盘验证; convert 语义未知 → Q4 |
| fee = rate×p×(1−p) | `ExtractFeeRateCoef` | `[实测]`高 | 一致 |
| maker fee / rebate | **只读 rate, 没读 takerOnly/rebateRate** | — | **最大空白** → Q8/Q9/Q10 |
| 流动性奖励计划 | **代码完全没有** | — | **未知是否存在** → Q10 |
| tick=0.01(动态 per-market) | `/tick-size` + WSS event | `[代码]`高 | 分档规则未知 → Q6 |
| min order $1 (taker buy) | makerAmount≥1e6 | `[实测]`高 | maker/极端价位 min 未知 → Q7 |
| 订单类型 GTC/GTD/FOK/FAK | 只实盘验证过 FOK | `[实测]`部分 | postOnly 语义未核 → Q5 |
| in-play 是否暂停撮合 | **代码假设一直开放** | — | **危险假设** → Q12 |
| 生命周期 active/closed/resolved | `?closed=false&active=true` | `[代码]`中 | flag 组合精确语义 → Q11 |
| EIP-712 V2 签名 / Exchange V2 | 11 字段 / `0xE111…` / version"2" | `[实测]`高 | 实盘成交验证, 权威 = py-clob-client-v2 |

---

## §6 双边套利方案的机制约束 / 红线 / 坑

### 6.1 哪里走不通(机制硬墙)

1. **A 类无风险套利抢不到** —— `x_arb_free_edge>0` 的缺口被 <50ms bot 秒杀, 我们跨洋 60ms 链路 + fee + gas 必然落后。**A 类只能做特征, 不能做策略**(与老板设计一致)。
2. **sell-to-open 空头有 split 前提** —— "做空 YES"要么"买 NO"(干净), 要么"split 后卖 YES"(需 CTF 合约链路, 我们没有)。**M2 的 sell-to-open 在接 split 链路前不可行**, 建议**只用"买对边"做方向**(买 NO = 看空 YES), 绕开 split。
3. **极端价位的 tick/min-order 可能压死挂单空间**(Q6/Q7 未核实)。
4. **in-play 暂停撮合(若存在)直接证伪事件延迟套利**(Q12, 最危险)。

### 6.2 fee 吃掉 edge(已量化)

- near_half taker 往返 BE 6.45c, 事件移动中位远小于此 → **near_half taker 方向套利负期望**(小董/小袁一致结论)。
- 极端价位 BE 1.7c, 是唯一甜区, 但深度薄、min-order 约束、接近结算 position risk 不对称。
- **若 Q8 证实 maker 免费 / Q10 有流动性奖励, near_half 做市口径重新打开** —— 这是唯一能让方案从"补充策略($5-50/天)"升级到"主策略"的路径。**强烈建议 Q8/Q9/Q10 优先核实。**

### 6.3 ToS / 反操纵红线(机制合规)

> **`[待核实]` 关键问题 Q14:** Polymarket ToS 对**自动化交易 / 做市 / 高频**的条款 —— 是否允许 bot? 是否有 self-trading(自己吃自己挂单, wash trading)禁令? 双边挂单若被判 wash trade(刷量骗流动性奖励)会被封。我们做市时**YES 挂单和 NO 挂单不能构成自成交**。
> **`[待核实]` 关键问题 Q15:** **速率限制** —— CLOB REST/WSS 的 rate limit(下单频率、撤单频率、查询频率)。事件延迟套利 + 做市会高频撤改单, 撞 rate limit 会被限流甚至封。当前 CLAUDE.md 红线已列"违反 ToS 速率立即回滚"。
> `[内部已知]` Goalserve 侧速率已守(min_fetch_interval 1s), 但 **Polymarket CLOB 下单/撤单速率限制未知**。

### 6.4 与现有红线的关系

- `[内部]` 所有下单必经 RiskManager(CLAUDE.md 红线, 不可绕)。
- `[内部]` R-12: WSS event loop 禁同步 IO —— 事件触发路径(score notify → 下单)必须异步, 不能在 WSS loop 里阻塞下单。
- `[内部]` 真钱开闸(`LiveOrderGate.Arm()`)需老韩 RM + 小白安全会签; paper advisory 阶段不触会签门。
- `[内部]` paper 不污真账本(R-11)。

---

## §7 给 GM 的"待 WebFetch 官方文档核实清单"(按优先级)

> 我无 WebFetch。以下按"对方案生死的影响"排序, GM 去 docs.polymarket.com / CLOB API 文档 / py-clob-client-v2 README 核实。

### P0 — 决定方案生死(不核实不该写生产代码)

- **Q12 [in-play 暂停]**: Polymarket 体育市场在进球/比分变化后是否会自动 suspend 撮合? 暂停时挂单与恢复价格怎么处理? **若暂停 → B 类事件延迟套利可能整体证伪。** (查: docs trading mechanics / market resolution / 实盘 in-play 观察)
- **Q8 [maker fee]**: `feeSchedule.takerOnly=true` 是否表示 maker 免费? (查: docs fees + py-clob-client 的 fee 字段)
- **Q10 [流动性奖励]**: 是否有 Liquidity Rewards Program? 规则/覆盖体育/申领? (查: docs.polymarket.com rewards / liquidity-rewards 页)

### P1 — 决定策略形态(做市 vs 吃单)

- **Q9 [rebate]**: `feeSchedule.rebateRate` 是 maker rebate? 数值? (查: docs fees)
- **Q5 [订单类型 + postOnly]**: GTC/GTD/FOK/FAK 确切字符串与语义; postOnly=true 是否保证只做 maker? (查: CLOB API order types)
- **Q1 [卖空语义]**: SELL 单是否要求 inventory? 能否裸卖(合约自动 mint 反向)? (查: py-clob-client SELL + docs)
- **Q6 [tick 分档]**: tick size 在不同价格区间的确切分档(0.01 / 0.001 / 更细?), 极端价位用哪档? (查: docs tick size / `/tick-size` 端点文档)

### P2 — 决定 A 类套利 + neg_risk 是否要碰

- **Q2 [split/merge 成本]**: gas? collateral 是哪个 ERC20(USDC/USDC.e)? 经哪个合约? (查: CTF docs / contract addresses)
- **Q3 [split/merge 链路]**: 能否经 py-clob-client 调, 还是必须自己发链上 tx? (查: py-clob-client / CTF 文档)
- **Q4 [neg_risk]**: NegRiskAdapter convert 语义; 体育里哪些盘口是 neg_risk(预判: 仅 outright/futures, 单场二元不是)? (查: neg-risk docs)
- **Q7 [min order maker/极端价位]**: maker 挂单最小额? 极端价位 min 按 notional 还是 share? (查: docs order constraints)

### P3 — 合规 / 运营约束

- **Q14 [ToS 反操纵]**: 自动化/做市/self-trading 条款; wash trading 禁令对双边挂单的影响。 (查: Polymarket Terms of Service)
- **Q15 [速率限制]**: CLOB REST/WSS 下单/撤单/查询的 rate limit 具体数值。 (查: docs rate limits / API reference)
- **Q11 [生命周期 flag]**: active/closed/acceptingOrders/enableOrderBook 组合语义。 (查: gamma/CLOB market schema)
- **Q13 [结算延迟]**: 体育结算用 UMA 还是自动? 争议期/redeem 延迟? (查: resolution / UMA oracle docs)

---

## §8 老李一句话结论(给方案设计会)

1. **老板的"YES/NO 双边套利"在机制上不是无风险套利(A 类抢不到), 是"双边信息驱动的方向性短窗交易(B 类)"** —— 这点不澄清, 全会会在"无风险锁定"上空转。
2. **"双边仓位管理"应理解为库存/平仓灵活性(买对边代替卖本边), 不是同时锁双边吃缺口。** 建议**只用"买 YES / 买 NO"两个 BUY 动作**表达方向, 绕开 split/sell-to-open 的合约复杂度(我们目前没那条链路)。
3. **方案经济性的分水岭是 fee 结构(Q8/Q9/Q10)** —— maker 是否免费 + 有无 rebate/奖励, 决定它是"$5-50/天补充策略"还是"可做主策略"。这三个问题不核实, 任何 PnL 预测都是空中楼阁。
4. **最危险的机制未知是 in-play 是否暂停撮合(Q12)** —— 不核实它就写事件延迟套利生产代码, 风险是整条路下不进单。
5. **A 类无风险套利(`x_arb_free_edge`)保持现状当特征** —— 老板把它做 feature 不做硬规则的决策是对的, 别改。

---

*本文聚焦协议/机制/费用/合规事实。wire 实现派网络工程师, JSON parse 派序列化工程师 —— 老李只定字段语义与机制契约。*
