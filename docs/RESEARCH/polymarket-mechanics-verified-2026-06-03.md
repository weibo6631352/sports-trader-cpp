# Polymarket 机制核实 (官方文档, 2026-06-03)

> owner: 老雷 (GM) | last_review: 2026-06-03
> 来源: docs.polymarket.com + help.polymarket.com (WebFetch 实查). 给盈利方案设计会做事实底座。
> 用途: 双边仓位管理 + 套利 盈利方案的机制可行性 + 成本/激励真值。

## 1. 费用结构 (决定套利净期望)

- **公式: `fee = C × feeRate × p × (1−p)`** (C=shares, p=成交价)。对称于 0.50。
- **费率 (taker)**: Sports **0.03(3%)** / 电竞同体育类? (待确认电竞归类) / Crypto 0.07 / Finance·Politics·Tech 0.04 / 其余 0.05 / Geopolitics 0(免费)。
- **峰值有效费 @ p=0.50**: Sports **$0.75 / 100 shares = 0.75%**(= 0.03×0.25)。30¢ 和 70¢ 同额。
- **★ Maker 永不收费,只有 taker 付费。** Maker rebates 由 taker 费的 20–25% 资助。
- 2026-03-30 起新建体育市场用此更新费率结构 (峰值 0.75%)。

**对方案的含义**: 双边【挂单做市(maker)】= **零交易费**;只有【穿价吃单(taker)】才付 0.75%(峰值)。→ 套利成本极度依赖 maker/taker 选择。纯 maker 双边策略交易成本 ≈ 0。

## 2. 流动性奖励 (★ 全新独立利润线, $5M+/月砸体育电竞)

- **2026-04 起每月 $5M+ 流动性激励**,**按 赛前(Pre)/盘中(Live) 分池,per game,池内 pro-rata**。
- **打分 = 二次函数**: `S(v,s) = ((v−s)/v)² × b`
  - `v` = 最大允许价差(cents, = max_incentive_spread)
  - `s` = 实际报价距(size-cutoff 调整后中点的)价差
  - `b` = in-game 乘数
  - → **报价越贴近中点(s 小)分越高(二次)**;越宽越不值钱;超过 v 不计分。
- **★ 双边深度加成**: 中点 ∈[0.10,0.90] 时 `Q_min = max(min(Q_ne,Q_no), max(Q_ne/c, Q_no/c))`, **c=3.0**;中点 <0.10 或 >0.90 时 **强制双边** `Q_min=min(Q_ne,Q_no)`。→ **只挂单边只拿 1/3 分;双边对称报价拿满分。**
- 资格门: 报价在 `max_incentive_spread` 内 + size ≥ `min_incentive_size`。
- 结算: 每分钟一 sample,epoch=1 周=10080 samples;你的奖励 = 你的 Q_epoch / 全体 Q_epoch × 池。$1 USDC 起付。

**对方案的含义**: **双边、贴中点、够大 size 的做市报价,本身就赚激励(且 maker 零费)** —— 这是不依赖预测精度的**结构性利润线**。预测模型的作用变成:① 决定贴多紧/往哪边偏(避免被 sharp 跳动扫单=逆向选择);② 何时从 maker 转 taker 吃收敛套利。**奖励规则天然奖励我们要做的双边管理。**

## 3. 订单 / 撮合机制

- **订单类型**: GTC(挂到成交/撤)/ GTD(到期)/ FOK(全成或撤)/ FAK(部分成余撤)。做市用 GTC/GTD。
- **可同时挂 YES + NO 限价单**(GTC/GTD),但**共用该市场的同一 balance reserve**(余额约束:挂单总和 ≤ 可用余额)。
- **tick size**: 0.1 / 0.01 / 0.001 / 0.0001,按市场 `minimum_tick_size`(getTickSize)。下单价必须对齐否则拒。
- **心跳**: 每 10s 必须心跳,否则订单被自动撤(已知, 见 memory clob-wss-heartbeat)。
- **撮合**: offchain 撮合 + onchain(Polygon)结算;EIP-712 签名;原子结算。
- **negRisk**: 多结果事件传 `negRisk:true`(Neg Risk CTF Exchange)。

## 4. 待进一步核实清单 (给后续 WebFetch / 协议专家)

- [ ] 电竞(esports)是否单列费率,还是归 Sports/Other?(影响电竞盘成本)
- [ ] `max_incentive_spread` / `min_incentive_size` 各市场具体数值(每 market 不同?CLOB API 哪个字段)。
- [ ] size-cutoff-adjusted midpoint 的精确算法(s 的计算)。
- [ ] in-game 乘数 `b` 的取值规则(赛前 vs 盘中?哪些时段?)。
- [ ] 持仓双边(YES+NO)是否 = 锁定 1.00 → 可 merge 回 USDC?merge/split 费用?
- [ ] 盘中 suspended 期间挂单/激励如何处理。
- [ ] ToS 反操纵条款对双边做市/快速撤挂的限制(避免 wash/spoofing 嫌疑)。

## 4b. round-2 实测补充 (老李从伦敦节点打真 API + 老雷实测延迟, 2026-06-03)

**延迟 (决定做市可行性, 推翻"跨洋"):**
- 伦敦节点 → clob.polymarket.com **TCP 连接(网络 RTT)= 3–6ms**(5 次实测 2.8-5.8ms)。首字节 28-35ms = 含 TLS 握手 + 服务端处理, **非**网络延迟。
- **结论: 我们网络 RTT <10ms, 不跨洋。** CLAUDE.md §13 "CLOB ~31ms" + 老姜 latency doc "RTT 200ms" 是口径错(测了全程含 TLS/处理)。→ **撤单可 <10ms 跑赢逆选(同地做市商体质), 做市逆选损失 ≈0, 非 200ms 的 $40-100/场。**

**激励 (逐市场配置, 非全覆盖):**
- 实测 100 个 enableOrderBook 盘: **57 开激励 / 43 `rewardsMaxSpread=0` 无激励**;体育 52 个里 **21 个不发**。NHL Stanley Cup 不开 / World Cup 开 → **逐市场配,引擎1 准入必须 gate `rewardsMaxSpread>0`**。
- 体育激励参数实测: **`rewardsMaxSpread`=2.5¢ / `rewardsMinSize`=100 shares**(culture/politics 更松 3.5¢/20)。
- pro-rata 稀释: 小 size 单 game 单日 **O($0.1-1)**(鲸鱼挂 $2万 我们挂 $200)→ **激励是 cherry 不是主利润**。单场池实例: NBA $7,700(2150 pre+5550 live)/ EPL $10,000。
- 领取: 按钱包地址 pro-rata 自动, 每日 UTC 午夜, $1 起, 无 KYC/白名单, bot 可领(多钱包 sybil = ToS 违规不碰)。

**费用利好实测:** `rebateRate=0.25`(taker 费 25% 返 maker)。**真利好 = maker 零费 spread 留存 + rebate 0.25**(套利出场腿免费 → BE 砍到 ~1.4%)。

**机制实测:**
- 双 BUY 路径: YES_buy@0.48×100 + NO_buy@0.48×100 各锁各 notional(≈96 USDC, 不抵消, RM 按全额算 exposure)。reward 算簿深度 Q_ne/Q_no **不分 BUY/SELL** → BUY-NO 在 NO 簿提供 bid 流动性可领激励(**P0 待确认 reward 是否认 BUY-NO 为 NO 侧满额深度**)。免 split/免 inventory/免 wash。
- **PM 有 `acceptingOrders` 字段**(实测存在)→ in-play 进球是否 suspend 撮合可被动观测(Goalserve 连续 tick ≠ PM 在撮合, 两系统独立)。**引擎2 生死命门, 写代码前必须观测进球前后 PM 订单簿连续性 + acceptingOrders。**
- **`holdingRewardsEnabled` 76% 盘=true**: 独立于挂单激励的【持仓激励线】, 规则未知, 待量化评估。
- API 字段: `rewards.{min_size, max_spread, rates:[{asset_address, rewards_daily_rate}]}`(get-markets/rewards 端点); market 加 parse `rewardsMaxSpread/rewardsMinSize/acceptingOrders`(代码现无)。

**ToS:** 引擎1 双 BUY + 撤改节流(hold ≥ 一个 ~60s sample 再撤)合规干净(非 spoofing/非 wash)。引擎2 高频撤改是 ToS 暴露面(需 rate-limit 真值)。

## 5. 一句话结论

**盈利方案应是「双边做市赚流动性激励(零费 maker, 二次奖励奖励贴中点双边)」+「预测驱动避逆选 + 抓 sharp 收敛套利」的复合体**,而非单纯"预测结算/价格再下注"。激励池($5M/月)+ maker 零费,把双边仓位管理从"成本中心"变成"结构性利润线"。
