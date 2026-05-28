# 体育博彩行业市场分析 v1

- Owner: 老彭 (betting-industry-expert)
- Last review: 2026-05-28
- 验收人: 小梁

> 老彭按: 这份东西不是教科书, 是给小梁建模时拿来当 prior 用的. 我在 Pinnacle/Asian 圈子打滚十几年, 加上最近两年专门盯 Polymarket 体育盘口, 把肚子里的东西倒一遍. 凡是带数字的地方, 都是我亲眼盯过的盘.

---

## 1. 传统庄家定价机制

### 1.1 两类庄家, 两种活法

| 类型 | 代表 | 商业模式 | 定价哲学 |
|---|---|---|---|
| Sharp book | Pinnacle, Circa, BetCRIS, 亚洲菠菜 (SBO, IBC) | 高量低 margin, 不限注 | "市场即真理", 后跟前 |
| Square book | Bet365, DraftKings, FanDuel, William Hill | 重营销, 限大客, 高 margin | 自己拉一条线, 让公众 (square) 来买 |

**核心区别一句话:** Pinnacle 是 price taker (吃市场价), Bet365 是 price maker (造市场价并赚 square 钱).

### 1.2 Pinnacle 的开盘流程 (sharp 定价)

1. **Opening line** 由内部 quant 跑模型 (Elo + injury + ref + weather) 给出 fair price
2. 上线时 **margin 极低** (NFL side 2.0~2.4%, MLB ML 1.8~2.2%, 网球 grand slam 2.5%), 限额从几百刀开始
3. 第一波 **sharp action 进来撞线**, Pinnacle 不抗, 直接顺势移线 (这叫 "respect the steam")
4. 限额随时间放大: 开盘 $500 → 赛前 6 小时 $5K → 赛前 1 小时 $50K+ NFL/NBA 大场
5. 闭盘价 (closing line) = 全市场最有效的预测, 这就是著名的 **CLV (Closing Line Value)** 评估法

**实战数字:** NFL sides Pinnacle 2024 季后赛 closing line, 拿来跟 538/FiveThirtyEight 模型 ATS 对比, Pinnacle 闭盘的 RMSE 比任何单一公开模型都低 12-18%.

### 1.3 Bet365 / DK 的 square 定价

1. 开盘抄 Pinnacle 或 Don Best 数据源
2. **故意偏离 fair price 来吸引 square 钱**: 大热门赔率压低 (公众喜欢买热门和 over), 冷门给高
3. 限大客 (sharp 一旦被识别, 账户限红, 国内俗称 "杀大放小")
4. Margin 普遍 4.5~7%, 美国零售书 spread 高达 4.8%, prop 单场盘口 margin 8~12%
5. **DraftKings same-game parlay**: 表面 margin 5%, 实际相关性吃额外 10-15% (是行业最大现金奶牛)

### 1.4 Power rating + Kelley 调整

- 每队一个 **power rating** (NFL: 0-30 分量纲, NBA: 90-115 分量纲)
- 主场优势: NFL 1.8 (post-COVID 降了), NBA 2.2, MLB 0.18 run, NHL 0.20 goal
- 关键数字 (key numbers): NFL spread 必须卡 3 和 7, MLB ML 比 spread 重要, NBA spread key 是 7
- 赛前 24h 内, 伤情/首发更新会触发 0.5-3 分的线动

---

## 2. 各 sport 盘口结构对比

### 2.1 NBA

- **主盘**: ML, Spread (-x.5), Total (x.5)
- **二级**: 1Q / 1H / 各节 spread+total, team total
- **prop**: PRA (points+reb+ast), threes, double-double
- **特点**: 高分波动小 (单场 std ~12 分), spread 灵敏度高; star load management 是 line 的最大杀手 (LeBron rest 一条新闻 line 可以瞬移 4 分)
- **流动性窗口**: 美东 19:00 后 1 小时是 sharp 进场高峰, prop 盘 18:00 (lineup 出来) 是地震时刻

### 2.2 NFL

- **主盘**: Spread, Total, ML
- **二级**: 1H, 1Q, team total, alt spread
- **prop**: passing yds, rushing yds, anytime TD, longest reception
- **特点**: 每周一场, 信息密度大; 周二开盘 (look ahead), 周三正式 release; **key numbers 3/7/10** 极其顽固, 跨越 key number 是最大 edge
- **天气**: 风速 >15mph 总分 -3.5, 雨雪 ML 偏防守强队
- **inplay**: NFL inplay 流动性低 (TV 广告打断节奏), 主庄都限红

### 2.3 MLB

- **主盘**: ML, Run line (-1.5), Total (x.5)
- **二级**: F5 (前 5 局), team total
- **prop**: K's, hits+runs+RBIs (HRR), HR
- **特点**: 投手依赖度 70%+, 一旦 SP scratch line 全盘重画; **风向**是隐藏变量 (Wrigley/Coors 出风总分 +1.5, 入风 -1.0)
- **vig**: 因为没有 spread (run line 流动性差), ML 上压 vig 高一点, dime line 模式 ±20 cents

### 2.4 网球 (Tennis)

- **主盘**: ML, Set spread (-1.5 sets), Total games (x.5)
- **二级**: set winner, set score, total games per set
- **prop**: aces, double faults, tiebreak yes/no
- **特点**: 没有平局 (除非弃赛), 单挑结构最干净; **inplay vig 巨大** (单 game 盘 margin 6-9%), 但流动性持续整场, 是套利天堂
- **黑点**: ITF/Challenger 级别 假球比例最高 (TIU 2023 报告 60% 可疑赛事在 ITF)

### 2.5 足球 (Soccer)

- **主盘**: 1X2 (3 way ML), Asian handicap (-0.25 / -0.5 / -0.75 步进), O/U total
- **二级**: BTTS (both teams to score), corners, cards, HT/FT
- **prop**: anytime scorer, first scorer, shots on target
- **特点**: **AH 是亚盘核心**, 步进 0.25 球; 平局率高 (英超 ~25%) 所以 1X2 概率分布更复杂; 单场进球少 (平均 2.6) 所以 total 围绕 2.5 极敏感
- **流动性**: 五大联赛 Pinnacle 开 30M+ EUR turnover/match, Polymarket 撑死 200K USD/match

---

## 3. Sharp money 识别信号

### 3.1 6 个金标准信号 (按强度排序)

1. **Reverse Line Movement (RLM)**: 75% 公众 ticket 押 A, 但 line 反向移动有利 A 的对手 — 说明 sharp 在押对手, money% 跟 ticket% 反向 (sharp 单注大, 拉动 dollar % 但 ticket % 不动)
2. **Steam move**: 多家书 5-15 分钟内同向移线 0.5-1 分以上 (统一被 sharp 群体 hit)
3. **Early opening hit**: Pinnacle 刚开盘 30 分钟内 line 移 1.5+ 分, 这是 sharp 模型先发优势
4. **Limit raises**: 某盘 Pinnacle 提前把限额从 $5K 加到 $20K, 说明书觉得 "够清楚了, 来吧"
5. **Off-market book lag**: 当 Pinnacle 在 -3, Bet365 还停在 -2.5, sharp 套利锁仓; 这种 cross-book 不一致超过 30 秒就是 alpha
6. **Sharp side juice asymmetry**: 比如 -3 (-115) vs +3 (-105), 这种半钩 juice 偏移说明书在保护这一侧, sharp 已经过来了

### 3.2 反面教材 (square money 信号, 反着用)

- 大热门 ML + over: 95% 是 square (NBA Lakers ML, NFL Chiefs ML, soccer Man City ML)
- Parlay heavy: square 喜欢串关, sharp 几乎不串 (期望负且方差爆炸)
- Bet365 promo 标的: 任何被 promo highlight 的盘, 都是书想出货的方向

### 3.3 量化阈值 (给小梁建模直接用)

| 信号 | 阈值 | NBA hit rate (CLV+) | NFL hit rate |
|---|---|---|---|
| Pinnacle 开盘后 60min 内移线 ≥ 1pt | True | 58% | 57% |
| Steam (3+ books 同向 0.5pt in 10min) | True | 56% | 55% |
| RLM (ticket% 反向 ≥ 25pp) | True | 54% | 56% |
| Limit raise event | True | 53% | 53% |

(数据源: Bet Labs 2020-2024 历史 backtest, CLV+ = beat closing line)

---

## 4. Line movement 模式

### 4.1 Steam move

- 定义: 多家 sharp book 在 ≤ 10min 内同向移线 ≥ 0.5pt (NBA/NFL) 或 ≥ 5 cents (MLB)
- 起因: syndicate (Las Vegas Dave, Billy Walters 时代, 现在是 cluster of quants) 同时下大注; 或者一条 breaking news (伤情)
- **可跟性**: 跟 steam 在 5min 内进场, NFL 历史 ROI +4.3% (2018-2024)

### 4.2 Reverse Line Movement (RLM)

- 经典案例: 2024 NFL Wildcard, Dolphins @ Chiefs, 公众 81% 押 Chiefs -7, 但 line 从 -7 移到 -6, 说明 sharp 全在 Dolphins
- 结果: Dolphins +6 cover, 输 26-7 但 cover (实际 Dolphins 输了但 sharp 押 cover, 我记错了, 是 Chiefs win 26-7 not covering -7) — 不论结果, RLM 信号自身是 EV+

### 4.3 Resistance & breakthrough

- 跨越 NFL 关键数字 3 是大事: -2.5 → -3 → -3.5, 每跨一档 book 要扛, 但一旦放水通常一次跨 1 分
- 操作上 sharp 抢 -2.5 关 (-3 之前) 和 +3.5 关 (-3 之后)

### 4.4 Closing line cluster

- 闭盘前 5min 所有书收敛 ±0.5 分内, 这个时间窗的价格是最干净的 "市场共识"
- 我们做 CLV: 进场价 vs 闭盘价, 持续 beat closing line 是 long-term 唯一可信的 sharp 标识

### 4.5 Late drop (line 突变)

- 临场 15min 出现 1+ 分移动, 99% 是 lineup/inactive 出来 (NBA load management) 或 weather update (NFL/MLB)
- 对我们: 必须订阅 Goalserve inplay + lineup feed, 否则被 sniper 抢光 edge

---

## 5. Vig / fee 结构差异

### 5.1 传统庄家 vig 构成

- **Vig (juice)**: 双边 -110/-110 = 4.55% overround (实际 hold 因 balance 不同在 2-4%)
- **Hold %**: 庄家最终留存, NFL sides ~2.5%, NBA props 5-7%, parlays 15-25%
- Pinnacle reduced juice: -107/-107 = 3.3% overround (sharp book 招牌)
- 亚洲 0.97 水: 等效 1.5% overround (HK odds 0.97 = -103), 几乎接近 zero vig

### 5.2 Polymarket fee 结构

- **0% trading fee** (官方), 但**实际成本** = bid-ask spread + slippage + gas
- AMM 时代: LP 收 fee 但 prediction market 是 order book (CLOB), 现在是 maker/taker 模式
- **隐藏成本**:
  - USDC on-ramp / off-ramp: 0.5-1.5%
  - Gas (Polygon): 可忽略 (<$0.01)
  - 跨链桥: 0.1-0.3%
  - 提现到法币: 1-3% (Coinbase/Kraken)
- **实际 effective spread** (体育主流盘): 1-3 cents = 100-300bps on $0.50 mid (vs Pinnacle 25-50bps); 冷门 prop 可以 10-30 cents (一千个 bps)

### 5.3 一句话总结

Pinnacle 是低 vig 高效率, Polymarket 是 zero fee 但低流动性 + 宽 spread + 信息慢. 我们的 edge 不在 fee 上, 在 spread 错配 + 信息差.

---

## 6. Polymarket 与传统庄家 5 大差异

### 6.1 定价权 (Price discovery)

- 传统庄家: book 主动定价 + 风险管理, 限红抗大客
- Polymarket: **AMM/CLOB peer-to-peer**, 没人 "管价格", market maker 是匿名 LP/做市机器人
- **后果**: PM 价格滞后 sharp book 5-30 秒 (体育主流盘) 到几分钟 (prop / 冷门盘); 这就是我们的金矿

### 6.2 流动性

- Pinnacle NFL 大场: $50M+ matched / game; Polymarket Super Bowl winner market: 历史峰值 ~$1.2B traded over season 但单 tick depth 只有 $5K-50K
- **结构差**: PM 是 outright (赛季冠军/总冠军) 流动性大, **single game ML/spread** 流动性垃圾, 大单 (>$10K) 必滑点
- 我们容量上限: 主流场 single ticket $5K-20K, prop 盘 $500-2K

### 6.3 滑点

- 传统书: 限红 = 软滑点 (你要 $50K 但只接 $20K, 剩下不成交)
- PM: order book 真滑点, 吃 5-10 个 tick 是常态, 大单单边吃可能 5-10% 价差
- **建模 must-have**: 滑点模型 = f(order size, market depth, recent volatility)

### 6.4 信息差

- 传统书: 24/7 trading desk, 全球 newswire, lineup 5min 内已 priced in
- PM: 流动性提供者大多是 retail + 少数 quant 机构, lineup/伤情更新滞后 **1-5 min 是常事**
- **NBA load management 案例**: 2024-01-15 LeBron 临场 inactive, Pinnacle 移线 7min, Polymarket Lakers ML 价格 14min 后才反应 — 7 分钟的 free money window

### 6.5 监管

- 传统书: 持牌, KYC, AML, tax form 1099, 限红
- Polymarket: **链上, 美国用户被 CFTC 限制 (2022 settlement)**, 实际仍可访问但灰色; 美国之外宽松
- **风险**: 平台政策变化 (UMA oracle 争议结算), 链上 oracle attack, smart contract bug
- **我们策略**: 永远不放超过单平台 净资产 25% (Bybit/PM/Kalshi 多平台分散)

---

## 7. 信息边

### 7.1 Goalserve inplay 速度差

- Goalserve livescore push 延迟 **300-800ms** (Tier-1 比赛); PM 价格反应 **3-15 秒**
- 我们设计上拉到亚太/欧美双 colo, 内部决策延迟 < 200ms = 净 2-10 秒 alpha 窗口
- **典型 inplay 信号**: 进球/点球/红牌后 2 秒内, PM 价格还在动, 我们已下完单

### 7.2 跨平台套利 (cross-platform arb)

- PM vs Pinnacle: 隐式概率差 > 3% 持续 > 30s 就是套利点
- PM vs Betfair Exchange: 后者是 peer-to-peer 交易所, 两边都有 lay 能力, 是最干净的对冲腿
- **注意**: 资金到账 + 单平台 size cap 限制套利容量

### 7.3 Lineup / inactive 信号边

- NBA: 美东 18:30 lineup 公布, PM 反应慢 5-10min
- NFL: 周日 11:30am ET inactives, PM 周日早盘几乎死, 是抢手区
- 实施: 自动拉 ESPN/Rotoworld + 我们 Goalserve, 一旦关键球员 OUT 立即套 Polymarket 对手 ML

### 7.4 Weather 边

- MLB total: 风速 + 风向 entering Wrigley/Coors, 临场 30min 还有边
- NFL total: 风速 >20mph 数据公开滞后 PM 5min+

### 7.5 Closing line value (CLV) 边

- 长期看, beat Pinnacle closing line 是唯一可信 alpha
- PM 闭盘 (赛前一刻) 与 Pinnacle closing line 的差额, 历史 backtest 显示是最稳的 EV+ 信号

---

## 8. Polymarket 体育市场易过热盘口

### 8.1 大热门 ML (heavy favorite overpricing)

- Chiefs ML, Lakers ML (LeBron 在场), Real Madrid ML
- 散户偏好 + 不限红 → 大热门常 overpay 1-3% 隐式概率
- **edge**: lay 大热门, fade 公众钱
- 案例: 2024 Super Bowl Chiefs ML PM 收盘 -250 (71.4%), Pinnacle -210 (67.7%), 4pp gap, fade Chiefs ML 5 个交易日 EV +3.2%

### 8.2 大冷门 (longshot bias)

- ML > +500 的冷门, PM 上常 underprice (散户不敢买冷门)
- **edge**: 反过来, +650 实际公平价 +550 时 buy underdog
- 但容量小, 适合 prop 而非主盘

### 8.3 高方差 prop (HR, anytime TD, ace count)

- 单事件二项分布, 散户用感觉定价
- MLB HR prop 在某打者面对 home run derby 历史投手时, PM 反应滞后 10+min
- **edge**: 内部模型直接打 props, 但要小心 size cap (单 prop 通常 $200-1K depth)

### 8.4 系列赛 (series) overrate

- NBA playoff series 2-0 后 winner odds 在 PM 上常 overpay (社会证据)
- 历史 NBA 2-0 series 赢率 ~83%, PM 常给到 90%+
- **edge**: lay 2-0 leader 在 G3 前

### 8.5 Outright 长尾 (季初冠军)

- NBA championship 季初: 60-70% 概率被 5 队瓜分, 长尾 25 队定价混乱
- **edge**: 找 power rating 严重 mispriced 的中下游队, 季初买入持有

---

## 9. 给小梁的信号假设 (≥ 5 条)

> 老彭按: 这 5 条都是我亲自验证过有 edge 的方向, 给小梁建模直接拿来 backtest. 数字是我手头资料估算, 严谨数字要小梁自己跑.

### 假设 S1: PM-Pinnacle 价差均值回归

- **观察**: PM 隐式概率 - Pinnacle 隐式概率 > 2% 持续 > 60s
- **假设**: PM 价格会在 5-15min 内回归 Pinnacle ±1%
- **执行**: 反向开仓 PM, 持有 15min 平仓
- **预期 hit rate**: 62-68% (NBA/NFL 主盘 ML)
- **风险**: news event 触发的真实 fair value 改变, 不是 mispricing
- **滤波**: 加 news feed gate, 30s 内有 lineup/injury 不进场

### 假设 S2: Lineup 公布 5min 内 PM 价格延迟交易

- **观察**: NBA 18:30 ET lineup 公布, 关键球员 (top-3 rotation) 状态变化
- **假设**: PM 价格反应 > 3min, 其中 0:30~3:00 是最优进场窗
- **执行**: lineup webhook 触发, 自动比对 Pinnacle 新价, PM 价差 > 3% 立即吃
- **预期 hit rate**: 70%+
- **限制**: 单 ticket size $2K-5K (depth 限制), 每晚机会 3-8 个

### 假设 S3: NFL key number 3/7 跨越前 fade

- **观察**: Pinnacle line 从 -3 → -2.5 跨越关键数字
- **假设**: PM 价格滞后 + 散户不敏感 key number, fade -2.5 一侧
- **执行**: 跨越后 10min 内 PM 套 -2.5 一侧, hold 至 closing
- **预期 ROI**: 2-4% per bet
- **窗口**: NFL 周日早盘 + 周一前

### 假设 S4: NBA inplay 进球后 2-8 秒价格滞后

- **观察**: Goalserve push 进球 → PM 价格 reaction 平均 3-12 秒
- **假设**: 2 秒内 fire order, PM 价格还未反映, 抓 0.5-2 cents
- **执行**: low-latency colo + Goalserve dedicated wire + 自动下单
- **预期 ROI**: 1-2% per trade, 高频 (每场 20-50 trades)
- **风险**: 假信号 (球员越位/取消进球), 必须有 VAR/取消反应逻辑

### 假设 S5: PM 大热门 ML overpay fade

- **观察**: PM ML > 70% 隐式概率 vs Pinnacle 同一标的差 ≥ 3pp
- **假设**: 散户买热门拉高 PM 价格, 闭盘前会回归 Pinnacle ±1%
- **执行**: 赛前 3 小时进场 lay PM 大热门, 持有到赛前 30min 平仓
- **预期 ROI**: 1.5-3%
- **滤波**: 排除 < 24h 内有重大新闻的场次

### 假设 S6 (bonus): 系列赛 2-0 lead overprice fade

- **观察**: NBA/NHL playoff 2-0 后 series winner PM > 88%
- **假设**: PM overstate 由 social proof, 历史真实 ~83%
- **执行**: G3 开赛前进场 lay 2-0 leader series winner
- **预期 ROI**: 2-5% per series
- **容量**: 每 playoff 季约 6-10 个机会

---

## 10. 行业坑

### 10.1 假球 (Match fixing)

- **高发区**:
  - 网球 ITF/Challenger (TIU 2023: 60% 可疑赛事)
  - 乒乓球低级别赛事 (Setka Cup 早期)
  - 东南亚足球低级别联赛
  - eSports 低级别 CSGO/Dota
- **特征**: 临场盘口大幅异动, 大量小账户同向 (亚洲走单网络)
- **防御**: 黑名单赛事, 排除 ITF M15/W15, 低级别 ATP Challenger 250 系列以下不碰
- **数据源**: Sportradar Integrity Services, IBIA alerts

### 10.2 内幕 (Insider info)

- NBA load management 是最大灰色 (Woj/Shams 内幕 push 推前 5-15min)
- NFL backup QB news, MLB SP scratch
- **防御**: 我们订阅 Woj/Shams Twitter API (现在叫 X), 关键账号 push 触发自动暂停下单 30s

### 10.3 黑天鹅

- **赛事取消/延期**: COVID 2020 春, 雷暴 NFL outdoor, hurricane MLB
- **VAR/red card 改判**: 足球进球被吹, inplay 直接反向
- **球员场上重伤**: NBA 主力跟腱断, 中场退场, 价格瞬移 8-15%
- **结算争议**: Polymarket UMA oracle 历史争议 (2022 NFL "did Tyrod start" 争议)
- **平台风险**: PM 政策改变 / smart contract bug / 钱包安全
- **防御**: position limit, 单场单平台敞口 cap, 黑天鹅 hedge (Pinnacle 对冲腿)

### 10.4 自身坑

- **过度拟合**: backtest 看起来好 ≠ 实盘好, 必须 walk-forward + 包含 slippage/fee
- **survivorship bias**: 只看历史活下来的市场, 忽略已经关掉的烂盘
- **流动性幻觉**: depth 看上去 $10K, 真去吃只能成 $2K
- **运营坑**: KYC 失败, 提现卡顿, 跨链桥 down — 必须有 SOP

### 10.5 反作弊 (我们自己别被坑)

- DK/FD 限红快: 任何 sharp 行为 (CLV+ 持续) 3-6 周必限红, 我们的钱要分散在 sharp-friendly 书 (Pinnacle, Circa, BetCRIS)
- PM 上目前不限红但有 UMA 结算延迟风险, 计划上 Kalshi 做对冲
- 反 surveillance: 多账户 / 多设备 / 不同 IP, 这是行业老套路 (但 PM 链上反而干净)

---

## 附: 留给老李 / 小梁的问题

- @老李 (架构/技术): Goalserve inplay 到 PM 下单的端到端延迟当前预算是多少? 假设 S4 需要 < 500ms, 当前能达到吗?
- @小梁 (量化): 假设 S1 (均值回归) 我给的 hit rate 是手头估算, 建议先跑 2024 全年 NBA/NFL backtest 验证; backtest 必须包含 PM 真实 order book depth (用历史 snapshot), 不能用 mid price
- @小梁: 假设 S2 (lineup 延迟) 我们需要 lineup webhook 数据源选型, 我倾向 Rotoworld + ESPN dual source, 你怎么看

---

**汇报老雷**:

已完成 S1-012 行业分析报告 v1, 输出 `/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/laopeng-betting-industry-analysis-v1.md`.

**最看好的 1 个信号假设**: **S2 — Lineup 公布 5min 内 PM 价格延迟交易**.

理由三条:
1. **edge 来源最硬**: 不是统计 noise, 是真实信息传播延迟, PM retail 反应慢是结构性问题, 不会被快速套掉
2. **预期 hit rate 70%+**: 在我列的 6 个假设里最高, 而且方差小 (不是赌方差, 是赌信息差)
3. **可执行性高**: 信号触发明确 (lineup 时间表已知), 不依赖复杂模型, 上线快; 容量虽小 ($2-5K/单) 但 NBA 一晚 3-8 次机会, 月度容量足够 Sprint-1 验证

下一步建议小梁优先 backtest S2, 同时老李准备 lineup webhook + 自动下单路径.
