# 盈利方案 round-2 — Polymarket 机制可行性红队批判

> **owner:** 老李 (Polymarket 协议专家, A 系统工程部)
> **last_review:** 2026-06-03
> **任务:** round-2 红队攻 `profit-scheme-DRAFT-v1.md`(双引擎草案=靶子)的机制可行性。
> **底座:** GM 已 WebFetch 核实 `polymarket-mechanics-verified-2026-06-03.md`(我 round-1 点名的 Q8/Q9/Q10 已回);本轮我再用部署节点(伦敦,白名单)打**真 gamma API 拿字段实测值**,把"文档转述"升级成"实测确认"。
> **可信度档:** `[实测]`=本轮真 API 拉到 / `[文档]`=官方文档确认 / `[代码]`=仓库实现 / `[待核实]`=仍需 GM WebFetch。
> **方法论纪律(§8.1 红线):** 引用机制约束粘原值,不转述。

---

## §0 本轮最重要的一个实测发现(先说,它改方案优先级)

我 round-1 假设"激励覆盖全体育"。**实测推翻了"全覆盖",改成"逐市场配置"。**

`[实测]` 2026-06-03 从伦敦节点打 `gamma-api.polymarket.com/markets?closed=false&active=true&limit=100`,100 个 enableOrderBook 盘口:
- **57/100 开了激励**(`rewardsMaxSpread>0`),43/100 **激励为 0**(`rewardsMaxSpread=0 && rewardsMinSize=0`)。
- 体育盘 `sports_fees_v2` 共 52 个,其中**只 31 个开激励**,**21 个体育盘 `rewardsMaxSpread=0`(根本不发激励)**。
- 实测到的"激励为 0"体育盘全是 NHL Stanley Cup futures(`will-the-X-win-the-2026-nhl-stanley-cup`);"激励开"的体育盘全是 World Cup futures(`will-X-win-the-2026-fifa-world-cup`)。**同为 futures,有的开有的不开 → 确证逐市场配置,不是按 feeType / 按运动一刀切。**

**对引擎1 的硬含义:** 引擎1 上线前**每个目标市场必须先读 gamma `rewardsMaxSpread`/`rewardsMinSize` 字段判定该盘是否在激励池内**,不能假设"挂上去就有激励"。这是一个必须进引擎1 准入逻辑的**新 gate**(草案没写)。**派序列化工程师在 market_discovery 加这两个字段的 parse(代码现在完全没读)。**

---

## §1 任务1 — 引擎1(双边 BUY YES + BUY NO)机制上跑得通吗

### 1.1 balance reserve:两个 BUY 限价单要锁多少钱

`[文档/实测推导]` Polymarket CLOB 的 BUY 限价单按 **`price × size`(USDC notional)** 锁定 collateral(USDC.e)。两个独立 token 的两个 BUY 单**各自锁各自的 notional,不抵消、不共享**(它们是两个独立 positionId 的买入,合约层没有"YES买和NO买互相对冲保证金"的概念 —— 那是 split/merge 的事,不是挂单的事)。

**算给方案组(草案问的 YES_buy@0.48 + NO_buy@0.48):**
- 设双边各挂 size=100 shares(= 体育 `rewardsMinSize=100` 的最小激励 size,实测)。
- YES_buy@0.48 × 100 = **48 USDC 锁定**
- NO_buy@0.48 × 100 = **48 USDC 锁定**
- **合计锁定 ≈ 96 USDC**,两单同时挂得通(只要钱包可用 USDC ≥ 96)。

`[机制真相,纠草案一个隐含误解]` 草案"两个 buy 共用 balance reserve"措辞会让人以为"锁的少"。**不是。两个 BUY 各锁各的,合计 ≈ (YES_px + NO_px) × size。** 当 YES_px + NO_px ≈ 1(中价对称盘),双边 100 shares 锁 ≈ 100 USDC。**这是引擎1 的真实资金占用基数,凯利/RM 必须按这个口径算 exposure。**

`[关键风险点]` 两个 BUY 单**若都成交 = 你同时持 100 YES + 100 NO = 锁定 100 USDC 赎回值,成本 96 USDC**。这本身是 A 类锁定(赚 4 USDC - fee),**但这要求两边都被吃**。现实中往往**只有一边被吃**(价格朝一个方向走,sharp 只扫被低估那边)→ 你拿到单边裸头寸 = 逆向选择。这就是引擎1 的核心风险,见 §1.4。

### 1.2 激励要求的"双边"是两 BUY 还是 buy+sell

`[文档+实测推导]` 这是草案问的命门。**结论:激励算的是订单簿两侧的挂单深度 `Q_ne`(YES侧)和 `Q_no`(NO侧),不区分这个挂单是"BUY YES"还是"SELL NO"。**

机制推导(粘 verified doc §2 原文):
> 中点 ∈[0.10,0.90] 时 `Q_min = max(min(Q_ne,Q_no), max(Q_ne/c, Q_no/c))`, c=3.0

`Q_ne` / `Q_no` 指的是**该二元市场两个 outcome token 各自订单簿上、你贴在 max_incentive_spread 内的挂单 size**。在 CTF 二元市场里:
- **SELL YES @ 0.52** 和 **BUY YES @ 0.48** 都挂在 **YES token 的订单簿**(一个 ask 侧一个 bid 侧)。
- **BUY NO @ 0.48** 挂在 **NO token 订单簿的 bid 侧**。
- 经济上 **BUY NO @ 0.48 ≡ SELL YES @ 0.52**(CTF 恒等:NO_bid = 1 − YES_ask)。

**所以"双边贴中点"在 CTF 机制下有两种等价表达:**
1. **单 token 双挂:** 在 YES token 上同时挂 BUY YES@0.48 + SELL YES@0.52 —— 但 SELL YES 需要先持有 YES inventory(CLOB SELL 非裸卖,见 §1.5),冷启动没货挂不出 SELL。
2. **★ 跨 token 双 BUY(草案路径):** BUY YES@0.48(YES簿 bid)+ BUY NO@0.48(NO簿 bid)—— **两个都是 BUY,不需任何 inventory,冷启动即可挂。**

**`Q_ne`/`Q_no` 是否认 BUY NO 为 "NO侧深度" → 这是引擎1 能不能用纯双 BUY 领激励的唯一未确认点。** 我的机制判断:**认。** 因为激励本质是"奖励在两个 token 簿上提供贴近中点的流动性",BUY NO 确实在 NO 簿上提供了 bid 流动性。但 reward 引擎用的是 `min(Q_ne, Q_no)` 还是分 bid/ask 侧计 —— **这个粒度我没有官方原文,标 `[待核实 R2-Q1]`(GM 清单)。**

**给机制层的安全姿势(不赌未确认点):** 引擎1 第一版**就用草案的双 BUY**(BUY YES + BUY NO),它绝对能挂、绝对不需 inventory、绝对绕开 split/sell-to-open。**即使最坏情况激励只认单边(拿 1/3 分,c=3),双 BUY 也比单 BUY 多覆盖一侧。** 不要在第一版赌"双 BUY 拿满分",上线后用真激励到账数据(epoch 结算)反推到底拿了 1/3 还是 3/3。

### 1.3 min_incentive_size / max_incentive_spread 实测真值(草案问的"能不能查到")

**能,且本轮已查到 `[实测]`:**

| 字段 | CLOB/gamma 字段名 | 体育盘实测值 | 单位 | 含义 |
|---|---|---|---|---|
| max_incentive_spread (`v`) | `rewardsMaxSpread` | **2.5** | **cents** | 报价距中点 ≤2.5¢ 才计分;>2.5¢ 不计分 |
| min_incentive_size | `rewardsMinSize` | **100** | **shares** | 单侧挂单 ≥100 shares 才够资格 |
| tick | `orderPriceMinTickSize` | **0.001**(futures)/ 0.01 | price | 见 §1.6 |
| 普通下单最小 | `orderMinSize` | **5** | **shares** | 与激励 min 是两回事 |

`[实测]` 非体育对照:culture/politics 盘 `rewardsMaxSpread=3.5, rewardsMinSize=20`(门更松)。体育是 **2.5¢ / 100 shares**(spread 门更紧、size 门更高)。

**对引擎1 的硬约束(草案未量化,我补上):**
1. **2.5¢ spread 门很紧。** 你的 BUY 单价距 size-cutoff-adjusted 中点必须 ≤2.5¢ 才计分。near_half 盘簿口 spread 中位 5¢(小董) → **你得贴进对手价 2.5¢ 内挂,逆向选择风险陡增**(贴得越近越容易被 sharp 扫)。
2. **100 shares min size。** 极端价位($0.05 token)100 shares = $5 notional,够 `rewardsMinSize` 但小;中价($0.50)100 shares = $50 notional。**双边双 100 shares 中价盘 ≈ 锁 $100/市场。** 这是引擎1 单市场最小资金粒度。
3. **2.5¢ 是 size-cutoff-adjusted 中点的距离,不是簿中点。** size-cutoff 怎么算 `[待核实 R2-Q2]`(影响"贴多紧才计分"的精确边界)。

---

## §2 任务2 — 激励资格现实(KYC / 白名单 / 反女巫 / pro-rata 稀释)

### 2.1 程序化做市能不能领激励

`[文档/待核实]` 机制层判断:**能,但有三道门要确认。**
1. **paper 虚拟盘绝对领不到** —— 激励按真实链上挂单 sample(每分钟一 sample,epoch=1周),paper 没真挂单,零激励。这点草案已写对。
2. **KYC/地域:** Polymarket 对美国等地区有地域限制(ToS)。我们 `.env` 钱包能不能领激励 = 能不能正常交易的同一道门。**CLAUDE.md §8.1 已决议"地域/法律/监管暂不纠缠,未来迁合规地区一次性处理"** → 机制层不在这里卡,但**真钱开闸前(老韩+小白会签)必须确认钱包地域资格**,否则挂了单领不到激励 = 白送逆选成本。标 `[待核实 R2-Q3]`。
3. **反女巫/白名单:** 激励是**按钱包地址 pro-rata 自动结算**(`你的 Q_epoch / 全体 Q_epoch × 池`),不是申请制白名单。**没有"先批准才能领"** —— 任何合规钱包挂够 size/spread 就计分。**但** 多钱包刷量(sybil)拿激励是明确 ToS 违规(见 §4)。我们单钱包合规即可,不碰 sybil。

### 2.2 pro-rata 稀释 — 小 size 能领多少(机制层量化)

`[文档+实测]` 池子机制:`$5M+/月` 全平台 → **按 game 分池 → 池内 pre/live 再分 → 池内 pro-rata**。

机制层估算(给量化组 small-size 判断,非精确):
- 月 $5M / 全平台所有 rewarded 盘。`[实测]` 单次快照就有 57 个 rewarded 盘,跨整月数千个 game-池。
- **单个体育 game-池的日激励 = 总池 / 当日 rewarded game 数**,粗估**单 game 单日池 O($10²~10³)**(没有官方 per-game 池额,标 `[待核实 R2-Q4]`)。
- 你的份额 = 你贴在 2.5¢ 内的双边 size×时长 / 该池所有人的同口径和。**鲸鱼挂 $10k×全程双边,你挂 $100×双边 → 你份额可能 <1%。** 单 game 单日你拿 O($0.1~$1) 量级。
- **结论(机制层判断):** **激励对小 size 是"聊胜于无"的薄收入,不是主利润线。** 草案把引擎1 当"结构性主利润"是**乐观了**。激励的真实价值是**补偿做市的逆选成本 + maker 零费让 spread 收入留存**,而非激励本身。**主利润应来自 maker spread(零费留存)+ taker rebate(0.25 实测),激励是 cherry on top。**

`[实测纠草案]` 草案 §round-1盲点 写"激励把做市从成本方变成结构性利润方"。**部分对(maker 零费 + rebate 是真的结构性利好),但"激励池份额"本身被 pro-rata 稀释到小 size 几乎拿不到 → 不能把激励池当主收入。** 主收入是 maker 零费下的 spread 留存。

---

## §3 任务3 — in-play 暂停(证伪 P0)

### 3.1 草案反推"有连续 tick → 市场开放"认同吗

**部分认同,但不足以证伪暂停假设。我的判断:这个反推有逻辑漏洞,不能当 in-play 全程可交易的证据。**

漏洞:
1. **连续 in-play tick 来自 Goalserve(比分/赔率数据源),不是 Polymarket 撮合状态。** 我们 5s/次的 tick 证明的是"Goalserve 在推数据",**不证明"Polymarket CLOB 那一刻在撮合"**。两个是独立系统。
2. **`acceptingOrders` 字段才是 PM 撮合状态的真信号** `[实测字段存在]`。in-play 暂停(若有)会反映在 `acceptingOrders=false` 或 WSS 状态事件,**不会反映在 Goalserve tick 上**。草案拿 Goalserve tick 反推 PM 撮合状态 = **跨系统误推**。
3. **正确的反推应该是:观测 PM 自己的 `acceptingOrders` + CLOB 订单簿在进球前后是否连续有 quote 更新 + 试探性挂单是否被接受。** 这要真 in-play 数据,不是 Goalserve tick。

### 3.2 进球瞬间微暂停对引擎2 的杀伤

`[机制层判断]` 即使存在进球后几秒微暂停,对引擎2 的杀伤**取决于暂停发生在收敛窗口的哪一段**:
- 若暂停在**进球瞬间~恢复**(比如 2-5s),恰好覆盖"sharp 已跳、PM 散户未动"的最肥窗口 → **直接吃掉引擎2 的入场点**,杀伤致命。
- 若暂停只是极短(<1s)撮合 freeze 然后恢复,PM 重定价仍滞后(verified 的散户 ~13s 滞后),则窗口只是右移几秒,引擎2 仍有 ~8-10s 残余窗口 → 可活。
- **关键变量 = 暂停时长 vs 13s 散户滞后。** 暂停 <5s → 引擎2 残存;暂停覆盖整个收敛过程 → 引擎2 死。

### 3.3 还需怎么实测确证(给 GM + 测试组)

**这是 P0,不能靠文档,必须真盘实测。三步:**
1. **被动观测:** in-play 盘开着时,采集 PM CLOB WSS 的 `acceptingOrders` / 订单簿快照,标注 Goalserve 进球事件 ts,看进球后 0-15s 内 PM 订单簿是否有"撮合停滞"特征(quote 不更新 / acceptingOrders 翻 false)。**纯观测,零下单风险。** 测试组小宋/小颖可做。
2. **GM WebFetch:** docs.polymarket.com 搜 "suspend" / "in-play" / "live trading" / "market pause" / "game in progress" 的撮合状态文档。标 `[待核实 R2-Q5,P0]`。
3. **paper 试探挂单:** paper 模式在 in-play 进球后立即挂一个远离价(不会成交)的 GTC 单,看是否被接受/拒(`acceptingOrders` gate)。**paper 不花钱,可做。**

**机制层红线:不核实 §3.3 第1步(被动观测进球前后 PM 订单簿连续性)之前,不写引擎2 任何生产代码。** 这是我 round-1 Q12 的升级,现在有了 `acceptingOrders` 实测字段做观测抓手。

---

## §4 任务4 — ToS 红线(程序化双边快挂/撤 = spoofing/wash?)

`[待核实但机制层有判断]`

### 4.1 spoofing(虚假挂单)
- **spoofing = 挂无意成交的单制造假深度然后撤。** 引擎1 的双边 BUY 单是**真愿意成交的单**(被吃了就持仓,正是激励要的"提供真流动性"),**不是 spoofing**。
- **风险点在"快速撤改"。** 若引擎1 因 bet365 跳价而在毫秒级反复撤改双边单(避逆选),高频撤单率可能触发 PM 的反操纵监控。**机制层建议:撤改有节流(min quote lifetime,比如挂单后至少 hold N 秒再撤),既降 spoofing 嫌疑又匹配激励 sample(每分钟一次)的节奏 —— 挂单活够一个 sample 周期才有激励意义,频繁撤改反而拿不到激励。** 这是 spoofing 风险与激励效率**天然对齐**的好消息。

### 4.2 wash trading(自成交)
- **wash = 自己的买单吃自己的卖单刷量。** 引擎1 双边是 **BUY YES + BUY NO(两个不同 token 的两个买单)**,**永不自成交**(买 YES 不会吃到买 NO,它们在不同订单簿)。**双 BUY 路径天然免疫 wash trading 嫌疑** —— 这是双 BUY 相对"YES买+YES卖"路径的又一个合规优势。
- **真 wash 风险在多钱包 sybil**(一个钱包挂、另一个吃,刷激励)。**我们单钱包,不碰。** 这条要写进引擎1 的 RM 红线:**禁止任何形式的自我钱包间成交。**

### 4.3 给 GM 核实清单
- `[待核实 R2-Q6]` Polymarket ToS 原文对 automated trading / market making / order cancellation rate 的明确条款(查 Terms of Service + docs "trading rules")。
- `[待核实 R2-Q7]` CLOB 下单/撤单 **rate limit 具体数值**(REST + WSS)。引擎1 双边 + 引擎2 高频会撞,需真值定节流参数。CLAUDE.md 红线已列"违反 ToS 速率立即回滚",但**具体阈值未知**。

**机制层结论:引擎1 用双 BUY + 撤改节流(hold ≥ 一个 sample 周期),合规面是干净的**(非 spoofing、非 wash)。引擎2 的高频撤改才是 rate-limit/spoofing 主要暴露面,需 R2-Q6/Q7 真值。

---

## §5 任务5 — 攻草案 + 收敛

### 5.1 草案机制上走不通 / 有坑的点

| # | 草案论点 | 机制层裁定 | 修正 |
|---|---|---|---|
| 1 | "激励覆盖全体育电竞" | **错** `[实测]` 逐市场配置,21/52 体育盘激励为 0 | 引擎1 加 `rewardsMaxSpread>0` 准入 gate |
| 2 | "两 buy 共用 balance reserve(锁得少)" | **误导** 两 BUY 各锁各的,合计≈(YES+NO)px×size≈$100/中价市场 | RM/凯利按全额锁定算 exposure |
| 3 | "激励池份额是结构性主利润" | **乐观** pro-rata 稀释后小 size 拿 O($0.1~1)/game/日 | 主利润=maker 零费 spread 留存+rebate,激励是补偿逆选的 cherry |
| 4 | "有连续 in-play tick → 市场游戏中开放" | **跨系统误推** Goalserve tick≠PM 撮合状态 | 看 PM `acceptingOrders`+订单簿连续性,不是 Goalserve tick |
| 5 | "双 BUY 拿满分(双边)" | **未确认** Q_ne/Q_no 是否认 BUY NO 为 NO 侧深度未定 | 第一版别赌满分,用 epoch 到账反推实际倍率 |
| 6 | 引擎1 "P1 立即可上不需模型" | **半对** 机制能挂,但 2.5¢ spread 门内挂单=高逆选,无避逆选规则会被扫 | P1 需最低限度的"bet365 跳价→拉单"规则(引擎3 规则化基线),纯静态双挂会被扫爆 |

### 5.2 我认同的机制可行形态

**引擎1(可先上,机制干净):**
- 路径:**双 BUY(BUY YES + BUY NO)GTC + postOnly** `[待核实 R2-Q8:postOnly 语义]`,贴中点 ≤2.5¢,单侧 ≥100 shares。
- 准入:**仅 `rewardsMaxSpread>0` 的盘**(读 gamma 字段)。
- 节流:**挂单 hold ≥ 一个 sample 周期(~60s)再撤改**(合规 + 激励效率对齐)。
- 资金口径:**单市场锁 ≈ (YES_px+NO_px)×size,中价盘 ≈$100**,RM 按全额算。
- 收入排序:**① maker spread 零费留存(主)② taker rebate 0.25(次)③ 激励 pro-rata(补偿逆选,薄)**。
- **不赌激励满分,不碰 split/sell,不碰 sybil。**

**引擎2(P0 证伪未过,不写代码):**
- **`acceptingOrders` 进球前后连续性被动观测通过 + R2-Q5 文档确认前,冻结引擎2 生产代码。**
- 通过后:tennis/soccer in-play,事件触发管道,窗口 = 13s 滞后 − 暂停时长。

**引擎3(规则化基线先行):**
- 引擎1 的 P1 就**必须带最低避逆选规则**(bet365 跳价信号→拉单/偏报价),否则 §5.1#6 被扫爆。序列模型后置。

### 5.3 还需 GM 再 WebFetch 的清单(我去查不了 ToS/撮合内部规则)

| 编号 | 问题 | 优先级 | 查处 |
|---|---|---|---|
| **R2-Q5** | in-play 进球后 PM 是否 suspend 撮合?暂停时长?挂单/恢复价怎么处理? | **P0(证伪)** | docs "live trading"/"suspend"/"market pause" + §3.3 被动观测 |
| **R2-Q1** | 激励 `Q_ne`/`Q_no` 是否认 "BUY NO" 为 NO 侧深度?分 bid/ask 计还是合计? | **P0(引擎1 能否纯双 BUY 领激励)** | docs liquidity rewards 计分细则 |
| **R2-Q8** | `postOnly=true` 是否保证只做 maker(撞对手价拒单而非吃单)? | **P1** | CLOB API order types / py-clob-client |
| **R2-Q2** | size-cutoff-adjusted midpoint 精确算法(2.5¢ 距哪个中点)? | P1 | docs rewards scoring |
| **R2-Q6** | ToS 对 automated trading / 撤单频率 / spoofing 的明确条款 | P1(合规) | Terms of Service |
| **R2-Q7** | CLOB 下单/撤单 REST+WSS rate limit 具体数值 | P1(定节流) | docs rate limits / API ref |
| **R2-Q3** | 我们 `.env` 钱包地域是否有领激励资格(真钱开闸前) | P2(真钱前) | help.polymarket 地域 + 钱包测 |
| **R2-Q4** | per-game 激励池额度量级(估稀释) | P2(收入预测) | docs rewards / Discord 公告 |

---

## §6 本轮实测字段表(给序列化工程师 + RM,粘真值)

`[实测 2026-06-03 伦敦节点 gamma]`,体育盘 `feeType=sports_fees_v2`:

| gamma 字段 | 实测值(体育) | 语义 | 下游用途 |
|---|---|---|---|
| `feeSchedule.rate` | **0.03** | taker 费率系数,fee=rate×p×(1−p) | RM/sizing(已接) |
| `feeSchedule.takerOnly` | **true** | 只 taker 付费,**maker 免费** | 做市经济性命门(确认) |
| `feeSchedule.rebateRate` | **0.25** | taker 费的 25% 返 maker | 引擎1 次收入线 |
| `rewardsMaxSpread` | **2.5**(cents)/ **0** | 激励计分 spread 上限;**0=该盘无激励** | 引擎1 准入 gate(代码现无) |
| `rewardsMinSize` | **100**(shares)/ **0** | 激励单侧最小 size | 引擎1 size 下限 |
| `orderMinSize` | **5**(shares) | 普通下单最小 share 数(≠激励 min) | 下单校验 |
| `orderPriceMinTickSize` | **0.001** / 0.01 | per-market tick | 价格对齐(已接) |
| `holdingRewardsEnabled` | **true(76%)** | **另一条线:持仓激励**(非挂单激励) | 标记,未研究,见 §7 |
| `acceptingOrders` | **true/false** | **PM 撮合开关(in-play 暂停的真信号)** | 引擎2 P0 观测抓手 |
| `makerBaseFee`/`takerBaseFee` | **1000**/1000 | 占位基准(实际费走 feeSchedule) | 别误用为真费率 |

**派序列化工程师:** market_discovery 加 parse `rewardsMaxSpread` / `rewardsMinSize` / `acceptingOrders`(字段语义我已定,见上表)。我不写 parse(边界:JSON parse 派序列化工程师)。

---

## §7 本轮新发现需立项的点

1. **`holdingRewardsEnabled`(76% 盘为 true)是独立于挂单激励的"持仓激励"线** —— 我 round-1 没提到,本轮实测发现。它奖励的是**持有 outcome token 的时长**(不是挂单),机制完全不同。**这可能是另一条结构性利润线**(持仓本来就有的方向性头寸顺带拿持仓激励),但规则未知。建议 GM WebFetch 列为 R2-Q9(P2),量化组评估。
2. **tick=0.001 在 futures/激励盘普遍**(94/100 是 0.001,只 6 个 0.01)。这比我 round-1 预判的"中价 0.01"更细 → **2.5¢ 激励 spread 在 0.001 tick 下有 25 个 tick 腾挪空间**,挂单粒度充足(纠正 round-1 §2.3 对极端价位"挂单空间被压死"的担忧 —— 实际 tick 比想象细)。

---

## §8 老李一句话结论(给方案设计会 round-2)

1. **引擎1 机制上跑得通,且双 BUY 路径合规面干净**(非 spoofing/wash,免 inventory,免 split)—— 这是草案最扎实的部分。**但准入必须加 `rewardsMaxSpread>0` gate(实测 21/52 体育盘没激励),资金按全额锁定算。**
2. **激励池本身被 pro-rata 稀释到小 size 几乎拿不到,不是主利润。主利润是 maker 零费下的 spread 留存 + rebate 0.25。** 草案把激励当主利润是乐观了 —— 但 maker 零费 + rebate 这两个**确认的**结构性利好仍然成立,引擎1 经济基础没塌。
3. **引擎2 的 P0 证伪点(in-play 暂停)被草案用"Goalserve 连续 tick"错误绕过 —— 那是跨系统误推。** 真抓手是 PM 自己的 `acceptingOrders` + 订单簿连续性,**必须被动观测进球前后 PM 订单簿后才能写引擎2 代码。**
4. **第一版别赌"双 BUY 拿激励满分"** —— Q_ne/Q_no 是否认 BUY NO 为 NO 侧深度未确认(R2-Q1),用 epoch 到账数据反推真实倍率。
5. **GM 再查 8 个点**(R2-Q1/Q5 是 P0),清单见 §5.3。本轮我已把 round-1 的 Q8/Q9/Q10 用真 API 实测钉死(takerOnly=true / rebateRate=0.25 / rewards 字段真名真值)。

---

*本文聚焦协议/机制/费用/合规事实,区分官方确认 vs 待核实。wire 实现派网络工程师,JSON parse 派序列化工程师 —— 老李只定字段语义与机制契约。*
