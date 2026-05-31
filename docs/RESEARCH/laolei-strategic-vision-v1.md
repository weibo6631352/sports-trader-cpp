# 战略愿景 + 执行路径 v1 — 全员评审涌现

> owner: 老雷 (GM) · last_review: 2026-05-31 · 性质: 10 主权评审收敛成北极星 + 路径
> 触发: 老板「潜力无限,发挥想象」+「金融专家、操盘手提建议」+「不要让现有接口限制发挥,为他们做数据支撑」
> 评审班底: 小袁(微结构) 小程(信号) 体育市场 小肖(算法) 老郭(架构) 小蒋(回测) 小段(Goalserve数据) 小冯(Polymarket数据) 小梁(金融) 老彭(操盘手)

---

## 0. 一句话定调 (老板纠偏的胜利)

**数据从来不是约束,是我们自己的框架把自己困住了。** 数据组实测证明:Polymarket L2 全档 / trade feed(带 aggressor)/ 收盘-结算-CLV / Goalserve 全 in-play 统计(危险进攻=xG代理/红牌/控球/射门/MLB outs+投手)—— **全都在,只是没 parse**。「我们仓位目标没有收盘」这句话**正式证伪**:`end_date_iso`+`tokens[].winner`+`closed`+`prices-history` 实测有效。

→ 方法论永久改:**先问要什么信号 → 定该提供什么接口 → 建数据支撑去喂。绝不用现有接口限制量化/算法/操盘的发挥。**

---

## 1. 北极星: CLV (全员独立收敛)

小肖(算法)、小蒋(回测)、老彭(操盘手)**三人独立**都把 **CLV(closing line value)= 入场价 vs 收盘公平价** 指为第一指标:
- **最快、最低方差的 edge 验证**(方差是 realized PnL 的 1/5~1/10,数据少也能评)。
- **操盘日常决策驱动**(老彭铁律: CLV 正才下单,没有例外;最优入场 = sharp money 涌入前 2-4h)。
- **和 reservation 限价范式天然契合**(reservation = 我愿付的最高价 = fair − 最小要求 edge;最小 edge 随流动性动态: 主流 1.5% / 冷门 4-5% / in-play 5%)。

**CLV 红线(小蒋): 只能当离线评估 label,绝对禁止进特征/实时推理**(需未来参考价 = 前视泄漏)。收盘参考价 = `SettlementRecord.close_fair`(小冯设计的一等数据产品)。

---

## 2. Alpha 地图 (小梁 + 老彭) — 钱从哪来,容量/Sharpe/衰减

| Alpha | 机制 | Sharpe | 容量 | →$5M? | 衰减 |
|---|---|---|---|---|---|
| **A in-play 延迟** | Goalserve快+LP慢,进球后30-120s没调价 | 2.0-3.0 | $0.5-1.5M | 部分 | **高(18-36月窗口)** |
| **B 做市价差** | 双边挂单赚 spread + maker rebate | 1.0-1.5 | **$2-4M** | **是(主力脊梁)** | 低 |
| **C CLV选线** | 系统挑收盘前 mispriced | 1.2-1.8 | $1-2M | 是 | 中 |
| **D 结算收敛** | 临近终场持正edge吃收敛 | 0.8-1.2 | $0.5-1M | 稳 | 低 |
| **E 跨盘口套利** | 同event ML/Spread/Totals不一致+negRisk | 2.5-4.0 | $0.3-0.8M(窄) | 否(Sharpe增强器) | 高 |
| **F 冷门错定价** | 长尾/outright厚尾 + favorite-longshot bias | 0.5-1.5 | $0.3-0.6M | 否 | 中 |

**关键裁定(GM 采纳):**
- **A 是启动 alpha 不是 $5M alpha**(小梁): Sharpe 最高但容量低、衰减快。价值 = 早期高 Sharpe 攒资本+攒数据+验执行链。**别把 $5M 押 A。**
- **战略主力迁 A→B(做市)**: B 容量大、结构性、Sharpe 1.5 正好对齐北极星。
- **但 in-play 不做市(老彭)**: in-play 做市 = 我们变成被 pick off 的慢 LP(那正是 A 在打的人)。**做市只在 pre-game/静态市场**(价稳、不是更新速度死亡竞赛)。
- **收敛(小梁×老彭): 方向(in-play)pick off 慢 LP + 做市(静态)赚价差,两条腿,不单腿站。**
- **组合后 Sharpe > 任何单源**: 6 个低相关 alpha 叠加,ρ≈0.2 → 组合 Sharpe ≈ 单源 ×1.8-2.2 → **单源 1.2 叠出 2.0+,这是超北极星 1.5 的数学路径。**

---

## 3. 金融架构 (小梁,$5M/Sharpe1.5/DD15% 同时成立的数学)

**核心论断: 单笔 Kelly × N → 组合 Kelly,不是优化,是换生意。** 同场 ML/Totals/Spread 是同一比分轨迹的不同投影,ρ 0.6-0.9。朴素独立 Kelly 在 ρ>0 时系统性过仓 → 真实风险是以为的 2.5 倍 → 隐性 full Kelly → DD 漂到 30%。**这是北极星 DD≤15% 头号杀手,且当前架构看不见。**

**三大金融裁定(小梁主权,GM 采纳):**
1. **P2(做市上线)前必须落地组合 Kelly + 应力相关矩阵 sizing**(同event强制ρ=0.9定仓)+ 三层风险预算(全局→因子桶→condition)+ CVaR 预算(二元结算是jump process,正态VaR全错)+ DD governor(滚动回撤8%→λ0.8/12%→0.5/15%→停)。**数学必然,不是 if。**
2. **资金周转率 ν + negRisk 净额化 立为与 edge 等权的一级指标**: PnL = B×ē×ν,全公司只盯 edge。negRisk 净额化(互斥结果资金净额)= 我们唯一无破产风险的"杠杆",有效 bankroll ×2-4。看 `edge/预期持有时长`(资金时间收益率),不看绝对 edge。
3. **event-level 因子敞口 cap + 流动性调整 VaR(LVaR)**: 进球瞬间 ρ→1 同event全盘口同时重定价 = diversification breakdown;临近结算 book 空 = 退不出。cap 因子敞口(非 condition),临近结算+薄 book 自动降 size。

---

## 4. 操盘手的 edge + 3 年疯狂想象 (老彭)

**Polymarket 结构性优势(传统书没有):**
- **没有聪明 LP**: LP 是情绪流动性+散户大单 = 软钱(传统书的 square)。所有 edge 的根源。
- **无限号**: 链上匿名,赢钱不被限号(Pinnacle 最高每场几万就限)。
- **链上透明**: 所有交易可查 → **追踪 top CLV 钱包,毫秒跟单**(合法的"复制 Pinnacle 赔率变动")。
- **双边**: 买 NO = 做空任何队,可构造 delta 中性 + negRisk 套利。

**in-play 最肥瞬间(EV 密度排序): 红牌(0-65min,LP 调价最慢) > 进球后0-60s > 伤停补时 > 0:0到80min(Totals小球) > 关键换人(可fade媒体过度反应)。** 最肥盘口: 足球 ML/Totals(五大联赛 in-play)。陷阱: Prop(结算争议)/低流动性高注意度/深夜跨时区。

**3 年终极武器(发挥想象):**
- **链上 sharp 钱包复制**: 建全 Polymarket 钱包 CLV 库,找历史 CLV 正的 top20,实时跟单。
- **跨平台同引擎多出口(小梁×老彭)**: 核心资产是"比 LP 更快更准的 fair value 引擎",不是 Polymarket 账户。同信号部署 Kalshi/Betfair → 容量翻几倍(解 P3 容量瓶颈)+ 跨平台套利。
- **信息产品**: fair value/CLV 信号反过来卖 → 零容量约束、零市场冲击、Sharpe 无穷(无资本占用)。自营触顶后的第二曲线。
- **风险转移层(小梁,最远)**: 稳定领先 fair value + 充足资本 → 承接体育市场尾部风险转移(类再保险)。容量上限 = 整个体育博彩风险转移需求,远大于单平台。

---

## 5. 执行路径 (怎么弄) — 数据支撑先行,分层推进

**核心顺序倒过来(老板纠偏): 不是「现有数据能做什么」,是「要什么 → 供给什么 → 建什么」。**

### 工作流 I — 数据支撑(数据组,解锁一切的前置)
| 项 | 内容 | owner | 成本 |
|---|---|---|---|
| L2 全档 book | CLOBSubscriber 存全档数组(实测30+档,只取L1是选择) | 老周/小冯 | 极低 |
| SettlementRecord 收盘产品 | 轮询 clob/markets: close_ts+close_fair+winner+settlement → CLV 一等公民 | 小余 | 低 |
| last_trade_price handler | WSS 启用 aggressor side → Kyle λ/真OFI/sharp money 跟单 | 小冯 | 中 |
| Goalserve live_stats | 新 client 拉 livescore 危险进攻/控球/射门/红牌 | 小段/小冯 | 中 |
| Goalserve gamecast | MLB outs+bases+投手 / 网球发球 / NHL PP(已在 feed,只需 parse) | 小段 | 低 |
| Poisson λ 基线 | 盘口反推 bootstrap → 历史 ETL 校准 | 小程/小余 | 中 |

### 工作流 II — 特征(量化/算法,老郭分批 + 小蒋验证门)
- **批0(现在)**: 改名 + taxonomy 立规(g_/b_/x_ 血缘单轴;`time_to_resolution→g_time_to_expiry`/`resolution_status→g_market_state` 避雷 WSS 同名)。成本全局最低点。
- **批1**: ring 扩 best_ask/ask_size + book 微结构(b_ofi/b_amihud/b_dislocation/x_log_odds/多尺度动量)。
- **批2**: de-vig 变体(g_fld_signal,power差值当特征不换主路径)+ CLV 离线 label。
- **批3**: sports in-play(Poisson/g_time_x_lead/比分动态/pin_risk)。
- **批4(滚动)**: 候选→MlFeature enum 小步晋升 + bump spec + retrain(唯一动列序锁)。
- **验证门(小蒋)**: 逻辑三问淘汰30→15 + Bonferroni(N=30→p<0.0017)+ IC>0.02/IR>0.3 + walk-forward 6fold + de-vig 用 Brier 对3b结算真值 + CLV 前视隔离红线。

### 工作流 III — 金融架构(小梁,P2 前必落)
组合 Kelly + 应力相关 + CVaR 预算 + DD governor + negRisk 净额化 + event-cap/LVaR。**晚于做市上线落地 = DD 必破 15%。**

### 工作流 IV — 操盘策略(老彭,渐进)
P1 方向(in-play 延迟)→ P2 +开盘错误+fade-public+链上钱包追踪+静态做市 → P3 跨市场delta中性+跨平台。

---

## 6. 阶段路线 (小梁财务 × 老彭操盘 合并)

| 阶段 | 时间 | bankroll | 主力 | Sharpe | DD | PnL | 瓶颈 |
|---|---|---|---|---|---|---|---|
| P0 MVP | 0-6月 | $50-100k | A in-play | 验证 | <10% | 正期望 | **执行链+延迟** |
| P1 单盘规模 | 6-12月 | $200-500k | A+D | 1.5 | 12% | $0.3-0.5M | 容量+风控自动化 |
| P2 组合化 | 12-24月 | $1-2M | +B做市+组合Kelly | 1.8 | 12% | $1.5-2.5M | **相关性建模+周转ν** |
| P3 全盘口全运动 | 24-36月 | $3-5M | B主力+C+E/F | **2.0+** | 11% | **$5M+** | 容量(靠盘口广度扩)+跨平台 |

**全盘口覆盖不是产品需求,是容量需求**(小梁): $5M 做市时我们自己就是市场一大部分,冲击成本上升,只能靠盘口广度扩容量。

---

## 7. GM 裁定 (本轮拍板)

1. **数据支撑先行**: 工作流 I 立项,数据组解锁 L2/SettlementRecord/trade feed/Goalserve stats —— 这是一切的前置,不让接口限制发挥。
2. **CLV 立为北极星指标**: SettlementRecord 做成一等数据产品;CLV 严守「离线 label 非特征」红线。
3. **战略主力 A→B,但 in-play 只方向、做市只静态**(小梁×老彭收敛)。
4. **组合 Kelly 列为 P2 阻塞前置**(小梁主权 enforce): 做市上线前必落,否则 DD 必破。立 P2 金融架构 ADR(月度策略评审,小梁出公式+老韩联签 cap+老周接架构)。
5. **特征走老郭分批 + 小蒋验证门**: 批0 改名现在做,捕获区放开加,enum 晋升克制。
6. **疯狂想象入 backlog**: 链上 sharp 钱包复制 / 跨平台同引擎 / 信息产品 / 风险转移层 —— 记入长期 backlog,不阻塞 MVP,但定方向。

**一句话**: 我们不是「在 Polymarket 交易的一个账户」,是「比 LP 更快更准的体育 fair value 引擎」——CLV 是它的体温计,组合 Kelly 是它的骨架,数据支撑是它的血,全盘口+跨平台是它的容量,链上透明是它独有的武器。
