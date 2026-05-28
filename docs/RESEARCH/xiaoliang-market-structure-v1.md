# Polymarket 体育市场结构研究 v1

- Owner: 小梁 (financial-expert)
- Last review: 2026-05-28
- 验收人: 老钱 (cpo-product-strategy)
- 关联 Ticket: S1-007 (本报告) / S1-002 (老李协议实测) / S1-012 (老彭行业分析) / S1-017 (小程信号假设)
- Status: v1 草稿, 待 Sprint-1 末与 S1-002 / S1-012 / S1-017 交叉验证

---

## 证据等级说明 (本报告通用约定)

| 等级 | 含义 | 引用方式 |
|---|---|---|
| [实测] | 我或同事直接拉到 API / WSS 数据后得出 | 给出时间窗 + 样本量 |
| [共识] | 行业内多方反复验证的事实 (老彭确认) | 给出可证伪条件 |
| [推断] | 基于金融理论 + 公开资料的合理推断 | 给出假设链 |
| [待验] | 本报告先挂账, Sprint-1 末由 owner 闭环 | 给出 owner + 截止 |

本报告 v1 受限于跨洋链路 + 主站登录墙, 主要为 [共识] + [推断], 关键数字标 [待验] 由老李 (S1-002) / 老彭 (S1-012) / 小余 (S1-D 历史数据) 后续灌实数.

---

## 1. 市场概况

### 1.1 Polymarket 体育在加密预测市场中的定位

Polymarket 是 Polygon L2 上 UMA 仲裁的二元 outcome CLOB. 体育板块自 2024 年 NFL 季后赛起爆发, 2025 全年覆盖 NBA / NFL / MLB / NHL / 欧洲五大联赛 / UFC / 网球大满贯 + 季节性 (奥运 / 世界杯 / 世俯赛). [共识]

定位 = "对冲基金 + 散户 + 套利机器人" 共处的 CLOB, 不是传统亚洲让分体系, 也不是 Pinnacle 式锐价庄家.

### 1.2 与传统体育博彩的本质差异

| 维度 | 传统庄家 (Pinnacle/Bet365) | Polymarket 体育 |
|---|---|---|
| 报价机制 | 庄家定盘口 + overround | 用户 vs 用户 CLOB |
| 收益模型 | 平衡两边 + 收 vig (3-8%) | 协议费 (当前 0%, 但未来可启用) + maker rebate |
| 限注 | 锐价户被限 | 无 KYC 限注, 但有钱包黑名单 |
| 结算 | 庄家裁定 | UMA 链上仲裁 (有挑战期) |
| 流动性 | 庄家承担库存 | 双边 LP + 散户挂单 |
| 价格发现 | 庄家信号 + sharp 喂价 | 套利 + sharp 直接成交 |

含义: Polymarket 的 "价" 不是庄家给的 fair odds, 而是市场出清价. 这一点决定了我们的 edge 模型不应直接抄 Pinnacle 那套 "no-vig fair line" 框架.

### 1.3 单笔市场结构 (老李 S1-002 详查)

- token_id 二元: YES / NO (sum = 1)
- tick_size: 0.01 大盘 / 0.001 高流动 (待老李 S1-002 列举 cents/halfcent 阈值) [待验]
- 最小订单: 5 USDC notional (Polymarket 文档口径)
- order types: GTC / GTD / FOK / FAK
- 一个体育比赛拆成多组二元 market (Moneyline / Totals / Spreads / 各 prop / Series / Outright), 每组下还可能进一步拆 (例如 NBA 系列赛 4-0 / 4-1 / 4-2 / 4-3 是四个独立 market 而不是一个 多 outcome)

注: "二元拆分" 是 Polymarket 体育市场 vs 庄家 1x2 的最大结构差异, 也是 5.1 "vig" 概念在 Polymarket 不直接适用的根因.

---

## 2. 流动性分布 (按 sport / 盘口家族)

> v1 数字均为 [推断] + [共识] 综合估算, 数量级正确, 精度待 S1-D (小余 + 小冯) 灌历史数据后修正.

### 2.1 按 sport (典型大场比赛盘前 24h 内总 book depth, USDC notional 估算)

| Sport | 顶级赛事 (Moneyline) | 普通赛事 (Moneyline) | 备注 |
|---|---|---|---|
| NFL | $500K-2M | $50K-200K | 周日大场 + Monday Night 最深; 全赛季高强度 |
| NBA | $200K-800K | $20K-100K | 季后赛 / All-star / 大牌对决最深 |
| MLB | $50K-200K | $5K-30K | 单场量级小, 但场次密度高 (162 场季) |
| Soccer (五大联赛) | $100K-500K | $10K-50K | 大场冠军杯 / 国家德比集中流动 |
| NHL | $30K-150K | $5K-20K | 散户兴趣较低 |
| Tennis (大满贯) | $50K-300K (决赛) | $2K-20K | 早轮稀薄, 仅 R16+ 可做 |
| UFC | $100K-500K (Main) | $5K-30K | Co-main 以下流动差 |

经验法则: **大场 Moneyline 深度 ≈ 普通场 5-10x; 高知名度运动 ≈ 小众运动 5x**.

### 2.2 按盘口家族 (相对 Moneyline 深度的比例)

| 盘口家族 | 相对深度 | 流动性特征 |
|---|---|---|
| Moneyline (主胜负) | 1.00x (基准) | 最深, 散户 + 套利共聚 |
| Totals (大小球) | 0.3-0.6x | 较深, 但 line 分散 (over/under 多档) |
| Spreads (让分) | 0.2-0.5x | 散户偏好, 但每档独立 market 流动碎片化 |
| Quarter/Half (分节) | 0.05-0.2x | 主要靠 inplay 流入 |
| Series (系列赛 4-0 / 4-1...) | 0.1-0.3x (合并所有 outcome) | 长尾, 中间 outcome 流动好 |
| Props (球员表现) | 0.01-0.1x | 极稀薄, 大场明星球员才有量 |
| Outright (冠军) | 长尾, 头部候选好 / 长尾干 | 散户 narrative 驱动, 套利空间常存 |

**含义 (与第 6 节呼应):** Moneyline 是流动性"金字塔尖", 任何流动性敏感的策略都必须从此起步.

### 2.3 跨时段流动性曲线 (单场 Moneyline)

```
notional depth (相对 peak)
  1.0 |                          ###  ←  开赛前 30 min - 开赛后 Q1
      |                       ##     
  0.6 |                  ##         #
      |              ##              #  ←  inplay 中后段
  0.3 |          ##                    #
      |    ###                          ##
  0.0 |________________________________________
      pregame 7d   24h   6h   30min   开赛   Q4   结束
```

[推断] 流动性峰值在 **开赛前 30min - 开赛后 Q1**, 这与 sharp money 的 close-to-game 注入 + 散户 last-minute + inplay 套利者同框时间窗一致.

**这条曲线决定了做市窗口**: pregame 24h 内 + inplay 上半段, 是 maker 库存周转最快的时段.

### 2.4 流动性的两个内部生态

- **Maker 池 (LP-like)**: 头部专业账户 (推测十几到几十个) 挂双边, 赚 spread + 偶发 rebate
- **Taker 池**: 散户 (narrative-driven) + 套利机器人 (cross-book vs Pinnacle/DraftKings) + sharp 资金 (直接吃市)

我们的策略 = **介于 maker 和 taker 之间的 informed 资金**, 主要赚 (a) Goalserve 信号领先 (b) maker 报价 lag.

---

## 3. 赔率分布 + 价差

### 3.1 隐含概率分布 (Moneyline, YES 价格)

[推断 + 共识] 体育 Moneyline 隐含概率分布:

```
density
       |    ##
       |  ######           ######      ##
       | ########         ########    ####
       |##########       ##########  ######
       +----------|------|---------|--------|----
                0.10   0.30      0.50    0.70   0.90
                     长尾下风       平局区/对赛   长尾热门
```

观察:
1. **双峰**: 0.45-0.55 (平衡对决, 大场常驻) + 0.65-0.85 (强弱明确, 占多数普通场)
2. **极端尾**: < 0.10 和 > 0.90 的 token 流动极差, 价差极宽 (常 5-10 cents)
3. **中段 (0.35-0.65)**: 价差最窄, 单笔深度最好

### 3.2 买卖价差 (bid-ask spread) 分布

[推断] 估算:

| 价格区间 | 典型 spread (cents) | 备注 |
|---|---|---|
| 0.45 - 0.55 | 1-2 ¢ | 顶级赛事可压到 1 tick |
| 0.30 - 0.45 / 0.55 - 0.70 | 2-4 ¢ | 主流区间 |
| 0.15 - 0.30 / 0.70 - 0.85 | 3-6 ¢ | 价差扩大 |
| 极端尾 (< 0.15 或 > 0.85) | 5-15 ¢ | 几乎无法 taker 介入 |

**含义**: 大场 Moneyline 1-2 ¢ spread = **隐含 vig 约 2-4%** (双边吃光), 已经接近 Pinnacle (业内最锐, 约 2%) 的水平; 与 Bet365 (5-8%) 比 Polymarket 反而更 sharp. 但是 Polymarket 是 "实时市场出清", 不是庄家锐价, 这个 spread 大部分时候 represent 的是 **maker 库存风险溢价 + 链上 gas 成本摊销**.

### 3.3 "vig" 在 Polymarket 的概念错位

⚠️ **重要**: Polymarket 没有传统意义上的 vig, 因为不是庄家定盘.

但实际成交价存在一个 "等效 vig":
- 体育二元 market 中, YES + NO 的 best ask 之和 ≠ 1
- 通常 best ask(YES) + best ask(NO) ≈ 1.02 - 1.05 (取决于流动)
- 这个 (sum - 1) × 100% 就是 **跨边吃光的成本** ≈ 等效 vig

[待验] 老李 S1-002 抓 24h 顶级赛事数据, 跑 (ask_yes + ask_no - 1) 分布直方图.

### 3.4 单笔可吃量 (taker 视角)

[推断] 大场顶级 Moneyline:

| 吃单大小 | 平均价格滑点 | 备注 |
|---|---|---|
| $500 | < 1 tick (~0.5 ¢) | 通常 best ask 一档吃完 |
| $2,000 | 1-2 tick | 吃穿 1-2 档 |
| $10,000 | 3-8 tick | 进入 maker 库存调整区, 价格变敏感 |
| $50,000 | 10-30 tick (5-15%) | 单笔难吃完, 需拆单 + 跨时段 |

**起步资金规模锚点 (第 7 节呼应):** 单笔 < $2K 是 "无足轻重 size", 不引起 maker 反应, 适合 MVP.

---

## 4. 做市空间评估

### 4.1 我们是否要做 maker?

**结论 (我的判断, 待老钱 + 老周表态):** Sprint-1 - M5 MVP 阶段 **不做主动 maker**.

理由:
1. Maker 真正赚钱的核心是 **库存管理 + 持续 quote 双边**, 这要求 7x24 在线 + 跨洋链路稳定 + 高频撤改单, 与我们 "跨洋高延迟 + 决策密集" 的工程定位冲突
2. 我们现有信号优势 (Goalserve 实时领先 + Polymarket lag) 是 **taker 套利型 alpha**, 不是 maker 中性 alpha
3. 维持 maker 头寸需要专门的 inventory risk model + impermanent loss-like 风险, 这块团队还没有
4. 但我们可以做 **"伪 maker" = passive limit 挂入价**: 在我们认为公允的价格上等被打到, 拿一部分 spread, 这与 taker 风格兼容

### 4.2 真要做 maker 的话, 空间多大?

[推断] 大场顶级 Moneyline 上, 单边 quote $500 size, 在 best bid - 1 tick 处挂入, 每小时被打到的概率 ~10-30% (依赖时段). 年化 spread 收益估算:
- 单笔 spread 收益: 0.5 - 1.5 ¢ on $500 = $2.5 - $7.5
- 单日单 market 可周转: 5-15 次
- 单日单 market 毛收益: $12 - $112
- 同时跑 5-10 个市场, 月毛收益: $1,800 - $33,600

但这个估算忽略:
- 被 sharp 反向吃单的 adverse selection 成本 (典型 30-50% 名义毛利)
- 跨洋撤单延迟导致 stale quote 损失 (我们结构性劣势)
- gas + 协议费

[结论] 净空间存在但被结构性延迟吃掉一半以上, **对我们不是优先**. M5 之后 (T+24 周) 可作为 v2 产品考虑.

### 4.3 "Passive limit 入场" 是兼容策略

这是 MVP 框架内可以做的: 信号触发后, 不立刻吃市, 而是挂 best bid (或更进一档) 等被动成交.

预期: 信号置信高时 taker 入场, 信号置信中等时 passive 挂单等成交 (省 spread), 信号弱时不操作.

---

## 5. 信号 α 来源候选 (≥ 10 条)

下面 12 条候选, 按我目前对置信度的估计 (老彭 + 小程合议后调整). **每条都有可证伪条件**, 这是我能给到小程做信号实验的最高约束.

### 5.1 [置信高] Goalserve-vs-Polymarket 时延套利 (核心 α 假设 #1)

**机制**: Goalserve inplay 比分 / 关键事件 (得分 / 红牌 / 受伤) 比 Polymarket maker 的报价更新更快 (典型 1-5s 领先).

**信号**: Goalserve 事件 t0 触发, Polymarket maker 在 t0+Δ 才调整价格, 我们在 [t0, t0+Δ] 窗口内 taker 入场.

**可证伪**: 若实测 Δ < 我们的 (跨洋 + 决策 + 下单确认) 时延, 则信号无价值. **跨洋是我们的硬约束**, 这条信号能否成立的关键就是 Δ 是否 > 600ms - 1s.

**实验设计**: 小程 + 小段记录 1000 个 inplay 事件, 测 (Goalserve push 时间 - Polymarket maker quote 调整时间) 分布. 若 p50 > 1s, p90 > 3s, 这条信号可做.

### 5.2 [置信高] Pregame 公允价回归 (核心 α 假设 #2)

**机制**: Pinnacle (业内最锐价庄家) 的 no-vig 价是体育领域接近真实概率的标尺. 老彭长期 confirm 这一点.

**信号**: 计算 Pinnacle no-vig fair price → 与 Polymarket mid 对比, 偏离 > 3 ¢ 且趋势向 Pinnacle 收敛 → 入场.

**可证伪**: 跑 6 个月历史数据, 看 "偏离 → 收敛" 的胜率是否显著 > 53% (KR-C-4 要求).

### 5.3 [置信中高] Inplay 比分-价格非线性失配

**机制**: 大众散户对 inplay 比分变化的反应不是线性的, maker 也来不及精算. 例: NBA Q3 落后 8 分时, 散户高估翻盘难度.

**信号**: 维护一个 "比分 → 胜率" 的历史回归模型 (按 sport / league / 时间分段), 当 Polymarket 报价偏离模型 > 2 σ, 入场.

**可证伪**: 模型残差是否有 mean-reversion 特性 (用历史成交价 vs 终局结果验证).

### 5.4 [置信中] 关键事件后 over-reaction

**机制**: 红牌 / 受伤 / 关键球员犯规 第六罚 时, 市场 over-react, 价格瞬间过冲.

**信号**: 检测 Polymarket 价格在事件后 30s 内单边跳 > 5 ¢, 在 1-3 min 内回归 2-3 ¢, 反向 fade. 与 5.1 互补 (5.1 顺势, 5.4 逆势 fade 过冲)

**可证伪**: 事件类型分类 + 5 min 价格曲线, 看 fade 是否系统性盈利.

### 5.5 [置信中] Maker 撤单 cascade

**机制**: 大额 maker 突然撤单时, 该方向流动性骤减, 市场转向另一方. 这是经典微观结构信号.

**信号**: WSS book channel 监测 best bid/ask 一档 size 在 < 1s 内消失 > 50%, 且 spread 扩大 > 2 tick, 触发反向预判.

**可证伪**: 撤单后 30s - 5 min 价格走向, 与 "正常波动" 对照.

### 5.6 [置信中] 跨 market 套利 (Polymarket 内部)

**机制**: 同一比赛的 Moneyline + Spreads + Totals 之间存在数学约束 (例: 主胜 + 让分 + 大小球之间隐含分差与得分分布相关), 偶尔违反.

**信号**: 维护跨市场一致性检查, 当三向同时偏离, 入场建立无风险 / 低风险组合.

**可证伪**: 跨市套利机会的频率 + 单笔可获利金额是否覆盖 gas 成本.

### 5.7 [置信中低] Narrative 散户 fade

**机制**: 大牌球星比赛 / 全国电视直播场 (NBA prime-time, NFL Sunday Night) 散户偏向主队 + 偏向大众选手, 价格被 narrative push.

**信号**: 识别 narrative game, 在散户 push 方向反向 fade.

**可证伪**: 需要 narrative 分类标签 (老彭提供), 跨 3 季历史数据看 fade 是否盈利.

### 5.8 [置信中低] Sharp money 跟随 (steam move)

**机制**: 大金额 sharp 资金进场, 价格瞬间跳跃, 此后 5-15 min 内有滞后跟随者继续推动同方向.

**信号**: 检测 Polymarket 单笔 > $5K 成交 + 价格单边跳 > 2 ¢, 跟随顺势.

**可证伪**: 大额成交后 15 min 内的价格 drift 方向 + 幅度.

### 5.9 [置信中低] 长尾低概率 token 错价

**机制**: < 0.10 或 > 0.90 的 token 流动差, 偶有 mispricing.

**信号**: 与 Pinnacle / DraftKings 对比, 偏离 > 1 ¢ (在低概率区, 这是显著的) 时入场.

**可证伪**: 但单笔可吃量极小, 即使胜率高也可能不够规模化. 适合小规模 v2 阶段.

### 5.10 [置信中低] 结算前后的不效价残留

**机制**: UMA 仲裁有挑战期 (~2h), 在比赛结束 → 结算的窗口期价格可能不收敛到 100% / 0%.

**信号**: 比赛结束后, 若价格仍未到端点, 且结果毫无悬念, 入场拿 risk-free yield.

**可证伪**: 历史数据看 "终局明确但价格未收敛" 的频率和持续时间.

### 5.11 [置信低] WSS / REST 数据流不一致

**机制**: Polymarket REST snapshot 和 WSS push 偶有不同步, 套利者来不及 reconcile.

**信号**: 实时监测两源差异, 发现错价快速 taker.

**可证伪**: 这个是工程信号, 可能存在但稳定性差, 主要作为补充.

### 5.12 [置信低] Funding round / news catalyst (outright)

**机制**: 长周期 outright (冠军盘) 在新闻事件 (球员转会 / 主帅下课 / 受伤复出) 后价格调整不充分.

**信号**: news feed → outright market 价格反应度量, 残差套利.

**可证伪**: 需要 news pipeline, 在 MVP 阶段暂不考虑.

### 5.13 总体框架

| # | 候选 | 类型 | 周期 | 置信 | 工程依赖 |
|---|---|---|---|---|---|
| 5.1 | Goalserve-Poly 时延 | 顺势 | inplay 秒级 | 高 | 跨洋延迟测试 |
| 5.2 | Pinnacle 回归 | 价值 | pregame 小时 | 高 | 历史回测 |
| 5.3 | 比分-价格失配 | 价值 | inplay 分钟 | 中高 | 比分回归模型 |
| 5.4 | 事件后 fade | 逆势 | inplay 秒-分 | 中 | 事件分类 |
| 5.5 | Maker 撤单 cascade | 微观结构 | 秒 | 中 | book WSS |
| 5.6 | 跨市场套利 | 套利 | 任意 | 中 | 多市场同步 |
| 5.7 | Narrative fade | 逆势 | pregame | 中低 | 标签数据 |
| 5.8 | Sharp 跟随 | 顺势 | 分钟 | 中低 | 成交 size 监测 |
| 5.9 | 长尾错价 | 套利 | 任意 | 中低 | 小 size |
| 5.10 | 结算窗口 | 套利 | 比赛后 | 中低 | 风险低 |
| 5.11 | 数据流不一致 | 工程 | 秒 | 低 | reconcile |
| 5.12 | News catalyst | 价值 | 天 | 低 | 后期 |

**MVP 首批 (与小程的接力 第 9 节):** 5.1 / 5.2 / 5.3

---

## 6. 为什么 MVP 选 Moneyline (论证)

### 6.1 五点论证

**(1) 流动性最厚** (第 2 节)
- 同一比赛中 Moneyline depth 是其他盘口的 2-10x
- 单笔 $2K 吃单滑点 < 2 tick, 是唯一可规模化的盘口
- 信号成立但流动性不够 = 不成立

**(2) 市场最 sharp, 价差最窄** (第 3 节)
- 1-2 ¢ spread, 等效 vig 2-4%
- 这意味着 α 阈值最低 ~ 2 ¢ 就可能盈利
- 其他盘口价差 4-15 ¢, 需要 α 远超我们当前模型水平

**(3) 二元 outcome 最干净, 数学最简洁**
- YES + NO sum = 1, fair price 一维数轴, 不像 spread/totals 有 line 选择问题
- Kelly sizing 直接套二元公式 f* = (bp - q) / b, 不需要多 outcome 修正
- 风控阈值 (VaR / drawdown / 头寸上限) 推导最直接

**(4) Goalserve 数据对齐最准**
- Goalserve livescore 给主胜负 / 比分 直接对应 Moneyline
- 不需要做 line 转换 (spreads 要算 cover / no-cover, totals 要算 over / under, 都有歧义)

**(5) 跨庄家可比性最强**
- Pinnacle / Bet365 / DraftKings 都有 Moneyline, 我们做 "Polymarket vs Pinnacle no-vig" 对比, 信号 5.2 直接落地
- 其他盘口的 line 在各庄家间不一定一致, 比较麻烦

### 6.2 拒绝清单 (与老钱 S1-008 协同)

- ❌ Spreads: line 不统一, 散户偏好, 散户行为占主导, 信号不稳
- ❌ Totals: 同 spreads, 且 over/under 的 lazy money 多
- ❌ Props: 流动太薄, 单笔 < $100 size
- ❌ Outright: 周期长, 资金占用大, MVP 阶段不划算
- ❌ Series: 流动碎片化, M5 后再说
- ❌ Quarter/Half: 流动太薄

### 6.3 单 sport 还是多 sport?

我的建议: **MVP 阶段先 NBA + NFL 两个 sport**.

理由:
- NBA / NFL Moneyline 流动最厚
- NBA inplay 节奏快 (信号 5.1 / 5.3 / 5.4 验证机会多)
- NFL pregame sharp 资金重 (信号 5.2 / 5.8 验证机会多)
- 两个 sport 节奏差异大, 也能提早暴露 portfolio 协同问题

(MLB / soccer 留给 v2; 五大联赛 inplay 时区跟我们跨洋时区可能不友好)

---

## 7. 资金规模建议

> 这一节是 "金融视角下的容量分析", **不是风控参数初值**. 风控参数 (单笔上限 / 日 drawdown / VaR 阈值) 是老韩 S1-004 的活, 我会在 T+16 周给老韩签字时再独立定数.

### 7.1 起步资金 (M1-M4 纸面 + 小额实盘): $20K - $50K

**论证**:
- 单笔 ticket size 上限 $500-1000 (滑点可忽略)
- 同时持仓 5-10 个 market, 单 market 暴露上限 $2-3K
- 总暴露上限 $20-30K, 剩余作为 ops 储备
- 资金小到 maker 完全不会注意到, 不引入 adverse selection
- 即使初期信号全错, 单月最大亏损 < $5K, 不致命

**对应工程指标 (小程 / 老周参考):**
- 信号触发频率: 每日 20-50 次 (按 5.1 + 5.2 + 5.3 三信号合计)
- 平均持仓时长: pregame 信号 ~ 数小时, inplay 信号 ~ 数分钟
- 单笔 Sharpe 期望: 0.05 (单笔), 但累积可达 > 1.0

### 7.2 6 个月: $200K - $500K

**触发条件**: M5 实盘已稳定 4 周, Sharpe > 1.0 落地, 风控零失效, 老雷签字加仓.

**论证**:
- 我们已经知道哪些信号 work, 可以放大
- 单笔上限到 $2-5K (仍在低滑点区)
- 同时跑 20-30 个 market, 总暴露 $150-300K
- 此时开始考虑信号间的 portfolio 相关性 + 资金占用率

**关键转折**: 6 个月节点是 "做不做 maker" 的决策点 (4.1 节). 若决定做, 需要额外 $200-500K 的库存资本.

### 7.3 18 个月: $1M - $3M

**触发条件**: 6 个月已稳定盈利, 年化净收益率 > 15%, 团队成熟到能管理多策略.

**论证**:
- 我们的 alpha 容量上限是多少? 这是关键问题
- 估计: 大场 Moneyline 单 market 最大可容纳信号资金 ~ $10-30K (再多就显著移动价格, 自吃 alpha)
- 全季 NBA + NFL 大场 ~ 200-300 场, 每场可上 $20K → 单月容量 $1-2M, 但要扣 holding time
- 同时跑的有效资金 ~ $1-3M 是合理上限
- 超过这个规模, 必须扩到其他 sport / maker 业务才能继续吃下

### 7.4 容量极限的警告

[推断, 但置信高] Polymarket 体育全平台年化净 alpha 容量估计 **$5M - $20M**. 这是结构性上限, 我们触及到 50% 容量 (~$2-10M) 时 alpha 衰减就会显著. 老雷需要提前知道这个 ceiling, 不要规划 $100M AUM 路径.

### 7.5 资金阶段表

| 阶段 | 时间 | 总资金 | 单笔上限 | 同时市场 | 触发条件 |
|---|---|---|---|---|---|
| 纸面 | M1-M3 | $0 | n/a | n/a | replay 通过 |
| 小额实盘 | M4-M5 | $20-50K | $500-1K | 5-10 | 风控验收 |
| 扩张 | M6-M8 | $200-500K | $2-5K | 20-30 | Sharpe > 1.0 持续 4 周 |
| 成熟 | M9-M18 | $1-3M | $10-30K | 50+ | 多策略 + 跨 sport |
| 容量极限 | M18+ | $2-10M | depends | depends | 触及 alpha 衰减边界 |

---

## 8. 风险源

### 8.1 流动性枯竭 (最重要, 我列第一)

**场景**:
- 大新闻事件 (受伤 / 暴雨 / 暂停) 时, maker 集体撤单, spread 跳到 20 ¢, 我们持仓无法 exit
- 跨洋链路抖动时, 我们看到 stale book, 实际市场已经走开
- 比赛非常规事件 (推迟 / 取消 / 仲裁) 触发, UMA 仲裁结果不确定, 价格冻在中间

**缓解**:
- 风控阈值: 持仓总 notional / 24h 平均 depth < 5% (我建议老韩用这个)
- inplay 信号触发持仓不超过 30 min, 强制 close (alpha decay 也要求)
- 跨洋链路 stale > 1s 自动暂停下单

### 8.2 单边 (信号集中导致组合风险)

**场景**:
- 5.1 / 5.2 / 5.3 三个核心信号同时指向 NBA 主胜方向 (理论上不应该, 但偶尔)
- 单日所有持仓都是 "主胜 / over / 强队", 大盘当晚集体爆冷, 全军覆没

**缓解**:
- 投资组合层面计算 beta 暴露 (按 sport / 按时段 / 按胜率档位 / 按 narrative 类别)
- 单日单方向暴露上限: 总资金 30% (这是金融视角默认, 老韩定具体值)
- 信号融合时强制做 decorrelation check (小程负责)

### 8.3 操纵 (Polymarket 特有)

**场景**:
- 小流动 market 上有人故意拉高 / 砸低价格, 触发我们的 momentum 信号 (5.5 / 5.8), 我们跟进后他反向 dump
- Series / outright 长尾 market 操纵成本低, 收益高
- UMA 仲裁博弈期 (~2h) 有人尝试影响仲裁结果, 进而影响 token 价

**缓解**:
- 只在 24h 平均 depth > $50K 的 market 上交易 (排除可被操纵规模)
- 信号 5.5 / 5.8 必须额外验证 (例如要求 follow-through, 不仅看单笔)
- UMA 挑战期内不开新仓

### 8.4 跨洋延迟 + WSS 断流 (工程风险, 但金融后果)

**场景**:
- 跨洋链路抖动, WSS 断流 5s, 我们重连后看到的价格已经跳了 5 ¢
- 信号 5.1 / 5.4 / 5.5 (秒级时效) 在断流期间全部失效, 但风控可能没注意到

**缓解**:
- WSS 断流 > 2s 进入 frozen 状态, 不下新单, 只 close 旧单
- 与老吴 (跨洋部署) + 老韩 (风控) 联合制定 frozen 协议

### 8.5 监管 / 协议变更

**场景**:
- Polymarket 启用协议费 (当前 0%, 但 ToS 保留权利)
- UMA 仲裁规则变更
- 美国 / EU 监管行动 (老黄 S1-006 跟进)

**缓解**:
- 协议费敏感性测试: 假设 fee 涨到 1%, 看哪些信号还盈利 (我估计 5.5 / 5.11 / 5.9 直接死掉, 5.1 / 5.2 / 5.3 仍可)
- 老黄合规红线清单已经在 Sprint-1

### 8.6 模型风险 (我自己的责任)

**场景**:
- 历史回测 Sharpe > 1.0, 实盘 < 0.5 (out-of-sample 衰减)
- 信号 5.2 的 Pinnacle no-vig 公式有 bug, 实际 fair price 算错
- Kelly 公式应用错误 (用了 historical 胜率, 未考虑置信区间收缩)

**缓解**:
- 所有信号必须有 out-of-sample 验证窗 ≥ 100 场
- Kelly 用 fractional (1/4 Kelly 起步, 这是金融实践共识)
- 信号生产化前必须经老钱 + 老韩双签

### 8.7 风险源汇总

| # | 风险源 | 概率 | 影响 | 主要 owner |
|---|---|---|---|---|
| 8.1 | 流动性枯竭 | 中 | 高 | 老韩 (风控) + 小梁 (阈值) |
| 8.2 | 单边集中 | 中 | 高 | 小梁 (portfolio 模型) |
| 8.3 | 操纵 | 中低 | 中 | 小程 (信号) + 老韩 (风控) |
| 8.4 | 跨洋延迟 | 高 | 中高 | 老吴 + 老韩 |
| 8.5 | 监管 / 协议 | 低 | 致命 | 老黄 + 老雷 |
| 8.6 | 模型风险 | 高 | 中 | 小梁 + 老钱 |

---

## 9. 与小程的信号接力 (≥ 3 个首批可落地的)

**约定**: 我给假设 + 可证伪条件, 小程负责设计实验 + 数据获取 + 回测 + 出 IS / OOS 报告, 接 S1-017 信号假设清单. 然后我审定阈值 + Kelly 系数.

### 9.1 首批信号 #1: Goalserve-Polymarket 时延套利 (5.1)

**给小程的输入**:
- 假设: Δ = (Polymarket 调整时间 - Goalserve push 时间) 在 inplay 关键事件 (得分 / 红牌 / 受伤 / 暂停) 时 p50 > 1s, 提供可套利窗口
- 关键参数: Δ 的分布 (含 p50/p90/p99) + 我们端到端决策延迟 D 的分布
- 可执行条件: P(Δ > D + safety_margin) > 50%, safety_margin = 200ms
- 数据需求: Goalserve inplay event log + Polymarket book WSS, 同步时钟, 1000+ 事件样本
- Owner 分工: 小程做实验, 小段提供数据同步, 我审定 Kelly 系数

**预期 KPI**:
- 单笔 win rate > 55%
- 期望 edge per trade: 1-2 ¢
- 频率: 每场 inplay 比赛 2-5 次触发

### 9.2 首批信号 #2: Pinnacle no-vig 回归 (5.2)

**给小程的输入**:
- 假设: Polymarket Moneyline mid 偏离 Pinnacle no-vig fair price > 3 ¢ 时, 后续 6h 内有 > 60% 概率收敛 50% 偏离
- no-vig 公式: fair_yes = (1/odds_yes) / (1/odds_yes + 1/odds_no), 这是行业标准, 我可以提供完整推导
- 数据需求: 6 个月 Pinnacle 历史赔率 + Polymarket mid, 配对样本 ≥ 500 场
- 可证伪: 跑回归 P(收敛 | 偏离 > 3 ¢) 是否显著高于 baseline P(收敛)
- Owner 分工: 小程做信号, 小余 (S1-D) 提供数据仓库

**预期 KPI**:
- 单笔 win rate > 54%
- 期望 edge per trade: 2-3 ¢
- 频率: pregame 阶段每场 0-3 次, 大场更多

### 9.3 首批信号 #3: 比分-价格失配 (5.3)

**给小程的输入**:
- 假设: 实时 比分 + 剩余时间 状态对应一个真实胜率 P_true, 维护 P_true 的回归模型. 当 |P_polymarket - P_true| > 2σ_model, 入场 fade.
- 模型设计: 按 (sport, league, time_remaining_bucket) 分段回归, 输入 (score_diff, possession_indicator), 输出 P_true
- 数据需求: 2 年历史 inplay 比分轨迹 + 终局结果 + Polymarket 历史成交价
- 可证伪: out-of-sample 上 fade 收益 > 0 且 Sharpe > 0.8
- Owner 分工: 小程做模型, 小宋 (test-replay) 提供 backtest 框架, 我审定 σ 阈值

**预期 KPI**:
- 单笔 win rate > 53%
- 期望 edge per trade: 1-2 ¢
- 频率: NBA 每场 5-10 次, NFL 每场 3-5 次

### 9.4 接力时间线

| 节点 | 责任人 | 交付 |
|---|---|---|
| Sprint-1 末 (6/12) | 小程 | S1-017 信号假设清单, 含本 v1 报告 5.1-5.12 的展开 |
| T+8 周 (M2 前) | 小程 + 小余 | 信号 5.1 / 5.2 / 5.3 的 IS 回测报告 |
| T+10 周 (M2) | 小程 + 我 | 信号 v1 (这三个) Sharpe > 1.0, OOS 验证通过 |
| T+12 周 | 我 + 老韩 | Kelly 系数 + drawdown 阈值定稿 |
| T+18 周 (M4) | 全员 | 纸面交易跑通这三个信号 |

---

## 10. 开放问题

### 10.1 待 [实测] 数字 (Sprint-1 内闭环)

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| Q1 | tick_size 在不同盘口的实际分布 | 老李 (S1-002) | 6/12 |
| Q2 | (ask_yes + ask_no - 1) 的等效 vig 分布 | 老李 (S1-002) | 6/12 |
| Q3 | 大场 Moneyline 24h depth 数量级实测 | 老李 + 小余 | 6/12 |
| Q4 | 跨洋决策端到端延迟 D 的 p50/p90/p99 | 老姜 + 老吴 (S1-011 + S1-010) | 6/12 |
| Q5 | Goalserve inplay 事件 push 延迟 | 小段 (S1-003) | 6/12 |
| Q6 | Polymarket maker 报价调整延迟 (Δ in 5.1) | 小程 + 小段 联合 | 6/19 |

### 10.2 待 [咨询] 问题

| # | 问题 | 咨询对象 |
|---|---|---|
| Q7 | Pinnacle no-vig 在体育不同 sport 的偏差 (NBA / NFL / soccer 是否一致 sharp) | 老彭 |
| Q8 | sharp money 在 Polymarket 上的可识别特征 (size / 时段 / 钱包行为) | 老彭 |
| Q9 | 哪些 sport / league 的 maker 行为最有可识别 lag (有助于 5.1 + 5.5) | 老彭 |
| Q10 | UMA 仲裁 historical 是否有 controversy case 影响过我们 token | 老李 |
| Q11 | neg_risk + redeemable market 与普通 market 的策略差异 | 老李 |

### 10.3 待 [战略] 决策

| # | 问题 | 决策人 |
|---|---|---|
| Q12 | 是否做 maker (4.1 节结论是 MVP 不做, 但 6 个月后要重审) | 老雷 + 老钱 + 我 |
| Q13 | 单 sport 起步 (NBA only) 还是双 sport (NBA + NFL) | 老钱 (S1-008 MVP scope) |
| Q14 | 6 个月加仓节点的 Sharpe + drawdown 阈值如何定 (硬阈值 vs 软判断) | 老雷 + 老韩 + 我 |
| Q15 | 容量极限是否要做实测压测 (例: 在低流动 market 故意吃市看冲击) | 老钱 + 老韩 |

### 10.4 已知未知 (我无法回答, 但知道存在)

- Polymarket 内部 maker 是谁? 用什么策略? (推测有 LP 机构和 prop firm, 但不可证实)
- 大额 sharp 钱包是否有可识别签名? (链上数据可能有 hint, 让小李 / 老叶 onchain-advisor 后续看)
- Polymarket 官方未来是否会推出体育 specific 产品 (例如 sport-only LP pool / 让分盘原生支持)
- 跨庄家 sharp 资金的 cross-book 行为 (DraftKings → Polymarket → Pinnacle 的资金流向)

---

## 附录 A: 我的关键金融假设清单 (供老钱 + 老韩复核)

| 假设 | 用在哪里 | 我的依据 | 反例条件 |
|---|---|---|---|
| Polymarket 二元 token 服从 ~ uniform 偏 logit 分布 | 第 3 节 | 行业共识 + 体育胜率分布 | 实测分布如果是 bimodal 极端则需修正 |
| Maker 库存调整延迟 > 我们决策延迟 | 5.1 | 跨洋是我们劣势, 但 Polymarket maker 也在做 cross-region | 实测 Δ < D 则信号 5.1 死掉 |
| Pinnacle no-vig 是体育 fair price 的最佳代理 | 5.2 | 行业 30+ 年共识 (老彭 confirm) | 体育细分类别可能有 Pinnacle 也不锐的盘 |
| 二元 Kelly f* = (bp - q) / b 在 Polymarket 直接适用 | 第 7 节 | 标准凯利公式 | fee + spread 需折算到 b |
| Sharpe > 1.0 是可达目标 | OKR KR-C-4 | 体育套利历史 Sharpe 普遍 1-2 | 跨洋延迟可能压制到 < 0.8 |
| α 容量上限 $5-20M | 7.4 | 推断 (Polymarket 体育全年 GMV + maker 反应) | 待 M5 后实测压测 |

## 附录 B: 与其他 Sprint-1 文档的依赖关系

```
S1-007 (本报告, 小梁)
  ├── 输入需求 →
  │     ├── S1-002 (老李, CLOB 协议实测): 第 1.3 / 3 / 10 节
  │     ├── S1-012 (老彭, 行业分析): 第 1.2 / 5 / 10 节
  │     └── S1-003 (小段+小余, Goalserve): 第 5.1 / 10 节
  │
  ├── 直接产出 →
  │     ├── S1-017 (小程, 信号假设清单): 第 5 节 + 第 9 节
  │     ├── S1-004 (老韩, RiskManager 设计): 第 7 / 8 节阈值参考
  │     └── S1-008 (老钱, MVP scope 拒绝清单): 第 6.2 节
  │
  └── 等候反馈 →
        ├── 老钱验收本报告
        └── 老雷 (战略 Q12-Q15)
```

---

**v1 收尾.** 本报告完成 Sprint-1 KR-C-1 (市场结构研究 → 第 4 周) 主体. 下一版 v2 在 T+8 周 (8/6 前) 发布, 灌入 S1-002 / S1-003 / S1-012 的实测数据 + 信号 5.1 / 5.2 / 5.3 的 IS 回测结果.

— 小梁, 2026-05-28
