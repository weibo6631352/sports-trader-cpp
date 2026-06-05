# 持仓管理研究综述 + 讨论结论 v1（SSOT）

> owner: 老雷 (GM) · last_review: 2026-06-05 · 性质: 持仓管理研究综述 + 两轮专家讨论 + 架构仲裁结论
> 缘起: 老板「持仓管理是门艺术…科学规范的动态持仓管理…订单簿结构/流动趋势/赛程/比分/手续费, 不能跑不赢手续费…网上+GitHub 高 star」+「不接量化, 方向靠赔率源, 量化不可靠」+「不能图省事, 你们讨论讨论, 有更好的仓位管理更好」
> 上位框架: [`laolei-target-position-strategy-direction-v1.md`](laolei-target-position-strategy-direction-v1.md) · [`laolei-target-position-controller-spec-v1.md`](laolei-target-position-controller-spec-v1.md)
> 外部调研素材(5 路): [`posmgmt-equities-v1.md`](posmgmt-equities-v1.md) · [`posmgmt-sportsbetting-v1.md`](posmgmt-sportsbetting-v1.md) · [`posmgmt-marketmaking-v1.md`](posmgmt-marketmaking-v1.md) · [`posmgmt-github-repos-v1.md`](posmgmt-github-repos-v1.md) · [`posmgmt-predmarket-fee-v1.md`](posmgmt-predmarket-fee-v1.md)

---

## 0. TL;DR

**现状框架是对的、不推翻:** 目标仓位连续控制(target = Kelly(edge),控制器 order=target−current,被动限价不追,无 rule 止损=target 缩小的副产品,RM 账户级保命门)已建好且金融上干净。

**但 5 路调研 + 两轮讨论一致挖出 4 个结构性缺口 + 1 个范式改进**(都是"更好"且不违反「方向真值=赔率源、量化不可靠」):

| # | 缺口/改进 | 一句话 |
|---|---|---|
| 1 | **相关性集中度=结构性漏洞** | per-market 独立 Kelly + 3 单点 cap, 无组合 gross cap/无相关聚合 → 同赛事多盘 ρ0.6-0.9 系统性超注 |
| 2 | **CLV(收盘线价值)未当一等信号** | CLV≈EV 且 ~50 笔就统计显著(PnL 要几千笔); `clv_tracker` 已存在但只离线 |
| 3 | **回撤不回喂决策** | λ 死写 0.35; portfolio_metrics 算了 maxDD 却「绝不回喂决策」 |
| 4 | **订单簿/库存层只观测没接执行** | A-S 库存项 + 毒性 margin + OFI→撤/缩 全是现成信号、没接 reservation/执行 |
| 5 | **范式改进: target 不该裸喂瞬时 edge** | sharp 实测 P50 2.3s 噪声延迟, 裸 Kelly(瞬时edge) → 抖动/过度交易; 应叠 edge-生命周期乘子(收敛/velocity/regime, clamp[0,1]) |

**maker 限价单 = 0 费**(taker 才付 p(1−p) 费)是最大成本杠杆, 现 reservation 限价范式天然吃到, 应显式确立 maker-default。

---

## 1. 现有框架(已建好, 复述)

Polymarket 体育二元盘口(YES/NO outcome token, 不能持负余额, 净空头=持对边)。
- **范式:** 模型出 `target_signed_notional`(方向=赔率源 sharp 低估边, 规模=Kelly) + `reservation_buy/sell_px`(限价不追)。控制器 `order = target − current` 被动限价 rebalance。无 rule 止损/止盈(target 缩小/翻转的副产品)。
- **2026-06-05 转向:** fair 的**方向真值 = 外部赔率源 sharp**(Bet365 de-vig, 经 Goalserve), 我们 ML 不可靠**永不单独驱动方向**; 订单簿只管执行/库存/逆选; 费曲线 `fee = shares × rate × p(1−p)`, 体育 rate≈0.03。
- **风控:** RM 3 个单点 cap(per_order/outcome/condition)+ daily-loss 熔断 + 回撤 halt; 账户级保命门。
- 已落: 控制器纯函数(BR-1 回测实盘共用) / RM signed→magnitude cap / reservation 公式 / 选边翻转平旧边 / 强制穿越防抖 / `SharpFairTrack` 环(velocity/conv/vol, **仅观测**)。

---

## 2. 五路外部调研要点(详见各 lens 文档)

- **股票/期货:** 分数 Kelly(λ0.25-0.5, 全 Kelly 会 >60% 回撤); 并发/相关缩放(多注各全 Kelly 会超 bankroll); vol-target 当**风控乘子不当 alpha**; 回撤触阈去险。**不可搬:** pyramiding 规则/rule 止损/亏损加仓教条/追价。
- **体育博彩(最近):** **CLV≈EV 近 1:1**(2万笔实测 3.4% vs 理论 4.0%), **~50 笔即显著**=低方差领先指标; 分数 Kelly 由"下行风险/防超注"而非"概率不确定"驱动; 组合 exposure cap ≤20-30% + 相关注合并; sharp velocity=edge 衰减率; Betfair green-up=连续 rebalance。**不可搬:** 持有到结算心智。
- **做市/微观结构:** A-S `reservation = ref − q·γ·σ²·(T−t)`(库存项纯加性偏移, 可锚 sharp fair); spread=波动项+簿深项; GM 逆选费; VPIN/|OFI| 毒性→撤/缩/扩(**只用幅度不用符号**); q=0→skew=0 不变量。
- **GitHub 高 star:** rodlaf/kalshimarketmaker(211★, 二元 A-S+库存 skew+双层 cap)、guberm/polymarket-bot(二元 Kelly f*=(b·p−q)/b + 6 层风控)、warproxxx/poly-maker(1.3k★ 最成熟, 链上 YES/NO merge 省 gas/释放保证金)、hummingbot A-S、nikhilnd 线性库存→spread。**缺口: 所有 A-S 仓库以 mid 为中心, 我们要锚 sharp fair, 无现成代码。**
- **预测市场/费用:** **maker=0 费**(taker 才付); break-even edge **p=0.5≈0.75¢单边/1.5¢往返, p=0.9≈0.27¢**(p=0.5 比 p=0.9 贵 2.78×); 实操 edge 应 ≥3×fee; **rebalance 死区随 p(1−p) 缩放防费磨损**; 默认持有到结算(免费)+neg-risk convert。

---

## 3. 两轮讨论结论

### Round 1(5 专家, 各自代码核对 + 提更好方案)
强共识(财务/风控独立都挖到 #1 相关性洞; 博彩/财务/风控都要 #2 CLV; 财务/风控都要 #3 DD 乘子; 微观结构给 #4 执行层全套; 量化质疑 #5 范式)。详见各 agent 记录与下文设计。

### Round 2(首席架构 老郭 对抗压测 + 仲裁)
- **修事实漂移:** `sizing_calculator.cpp` Step6 注释写"λ=0.25 quarter"但 `kLambdaBase=0.35` —— **陈旧注释先修**(否则按注释推 maxDD 会差半档)。
- **挖出真红线冲突:** 控制器 `predictive_unwind=true`(生产 daemon 已置)**直接 best_bid 平仓、绕开 reservation_sell** → A-S 库存项只在加仓侧生效、减仓侧被短路 → **库存 skew 只能压不能泄**。A-S 与 predictive_unwind 谁优先须先仲裁。
- **相关性折扣按计数 N 会自激震荡**(连续 rebalance 令 N 抖→f_adj 抖→target 抖); 改**按 gross 加权**, ρ 静态分桶热路径只查表。
- **DD 乘子 m 与 daily-5% 熔断口径打架**(maxDD 峰谷 vs daily realized), 且直接缩 target 会在低流动性 DD 区**逼 taker 平仓=把浮亏锤成实亏** → 需 hysteresis + 只缩"新增上界"不强平。
- **CLV 实时放大 λ: REJECT**(CLV 含 sharp 2s 滞后噪声, 实时反喂规模=滞后噪声驱动仓位, 违背"统计不可靠永不单独驱动"精神, 规模放大同样能爆 maxDD)。CLV 只做**离线 edge 验证 + 50 笔衰减报警**。

---

## 4. 推荐的"更好"持仓管理设计（综述定稿）

### 4.1 分层(方向 / 规模 / 执行 / 风控 四层正交)

```
方向层(谁说了算): 赔率源 sharp 低估边 = target 符号。CLV(市场价 vs sharp fair)= edge 此刻在不在。
   ── 永不由我们 ML/订单簿决定方向 ──
规模层(下多大): |target| = Kelly(net_edge) × Π 乘子:
   · 相关性折扣  (按 gross 加权, ρ 静态分桶)         ← 消同赛事超注
   · DD 全局乘子 m∈[0,1] (hysteresis, 只缩新增上界)   ← 回撤自适应
   · vol 乘子   min(1, σ_target/σ_realized)           ← 盘乱缩量(风控非alpha)
   · 生命周期乘子∈[0,1] (收敛剩余/velocity/regime)    ← 消瞬时edge抖动; 描述统计不反号
   net_edge = raw_edge − rate·p(1−p); 进场门 edge ≥ 3×fee
执行层(怎么打): maker 默认(0 费); reservation = fair − fee(p) − dynamic_margin − A-S库存项;
   dynamic_margin = floor + k_vol·σ²·τ + k_tox·|OFI|/depth (只用幅度);
   OFI/BidAbsence 超阈 → 暂停新单/缩 cap/冻结加仓; taker 仅防御(毒性+库存超限/临近结算/发散);
   rebalance 死区随 p(1−p) 缩放(防费磨损)。
风控层(别爆本金): RM 全单经 evaluate()+audit; 新增事件组相关 exposure cap + gross cap;
   账户级保命门(daily-5% 熔断/maxDD≤15% 硬地板)不动; CLV 滚动 50 笔<0 → STRATEGY_DECAYED 报警。
```

### 4.2 合法 / 越界界线(架构仲裁, 对"sharp 时序处理"画死)

- **合法(对 sharp 自己的时序做无预测描述统计):** velocity=sharp_fair 一阶差分; convergence=收敛剩余距离; regime=已观测序列状态分类。只回答"edge 此刻还在不在/收敛多少", **方向真值仍 100% 来自 sharp**, 乘子 clamp[0,1]·Kelly。
- **越界(滑向不可靠量化驱动方向, 红线):** ① 任何乘子能让 target **反号**(velocity 外推预测 fair 翻转→提前反向); ② 用 ML/回归**预测未来 fair 路径**定规模; ③ 乘子 >1 放大到 Kelly 上界之上。

---

## 5. 路线图(P0 / P1 / P2)

**P0 — 纯加性/不碰方向/热路径查表(§8.1 carve-out 内, 可立即建):**
- 事件组相关 exposure cap + `EXCEED_EVENT_EXPOSURE` 拒单码(ρ 加权**同向**聚合, fail-closed 取保守上界)。
- 修 `sizing_calculator.cpp` Step6 陈旧注释 0.25→0.35。
- **观测先行不驱动交易**(全上 portfolio_metrics 面板, 维持"绝不回喂决策"): portfolio gross_exposure、ρ 加权事件敞口、|OFI|、**CLV 滚动 50 笔均值**。

**P1 — 需 paper 验证/评审(动交易行为, 回测=实盘同逻辑 BR-1 + maxDD≤15% 实测背书):**
- 相关性折扣 Kelly(gross 加权版, 非计数 N)。
- A-S 库存 skew 项(**先解 predictive_unwind 优先级冲突 = 决策点 2**)。
- 毒性/OFI 接执行三档(cfg 默认关, paper 背书后开; OFI_30s 预算好传入守 R-12)。
- 生命周期衰减乘子 clamp[0,1](描述统计, paper 验抖动/换手率下降)。

**P2 — 需老板拍板/结构件:**
- DD→target 乘子 m(与 daily 熔断口径合并 + hysteresis + 区分"不加"vs"砍现仓" = 决策点 1)。
- 反向/开空(M2, 二元市场结构性 N/A, 路线外)。

---

## 6. 给老板的 3 个决策点(架构推荐选项在前)

1. **回撤出场机制:** 浮亏扩大时系统该"只停止加仓"还是"主动减仓(可能把浮亏锤成实亏)"? **架构推荐: 只停加仓 + hysteresis, 不主动砍现仓**(避免低流动性 DD 区被迫 taker 锤亏)。
2. **生命周期乘子能否加速平仓:** sharp edge 快速收敛时, 允不允许乘子让已有仓更快 unwind(触碰你定的 `predictive_unwind` 语义)? **架构推荐: 允许加速平, 绝不允许反向开。**
3. **CLV 能否实时放大仓位:** CLV 好就加大下注? **架构推荐: 否** —— CLV 含 sharp 2s 滞后噪声, 只做离线 edge 验证 + 50 笔衰减报警, 不实时放大。

### 老板裁决(2026-06-05, 已拍板)

1. **回撤出场 = 只停加仓 + hysteresis**(同架构推荐): 全局乘子 m 只压"新增 target 上界", 不主动砍现仓; 档位 hysteresis(进 10% 砍/退 8% 恢复)。**不在低流动性 DD 区被迫 taker 锤实浮亏。**
2. **加速平仓 + 反向开仓 = 都允许**(老板 2026-06-05 补充「反向有机会完全可以」, 修正架构"禁反向开"):
   - **反向开仓完全允许 —— 但触发权归赔率源 sharp**: sharp fair 翻转到看好对边 → target 符号翻 → 控制器**平旧边 + 建新边**。这正是已建好的 **M2-a「选边翻转平旧边」**, 用 YES/NO 两条 long-only 腿表达(二元市场不持负余额、不触 C1、老韩 H-1/H-2 已覆盖)。**反向现在就能做。**
   - **生命周期乘子加速平**: velocity 高/收敛快时乘子让已有仓更快 unwind。
   - **唯一仍禁**: 用我们不可靠的量化/乘子**"预测" sharp 会翻转、抢跑开反向**。乘子本身仍 clamp[0,1] 只调规模; **翻转判断权 100% 在 sharp**(跟随确认, 不抢跑预测)。
3. **CLV = 实时放大 target**(⚠ 老板**推翻**架构"仅离线"建议)。**实施护栏(GM 定, 化解架构指出的 2s 滞后噪声风险):**
   - 放大由**滚动 CLV 均值**(~50 笔统计显著的稳健量)驱动, **不用单笔瞬时 CLV**(那个才带 sharp 2s 滞后噪声)。
   - 放大乘子 **clamp 上限**(e.g. ≤1.5×), **Kelly 仍是规模天花板**, **maxDD≤15% 硬地板 + DD 乘子 m 仍管**(放大不得越过保命门)。
   - 正 CLV 桶放大 / 负 CLV 桶收缩 + 触发 STRATEGY_DECAYED 报警(双向)。
   - 仍 paper 实测 maxDD + 换手率背书后才真钱开闸(P1)。

> 决策已落定 → 进 Stage 2(`task #5`): 按裁决把通过项分层接进真实加/减/止盈/止损决策, 锚定赔率源, 回测 + 人确认再上。P0(事件组 cap + 注释修 + 观测面板)可立即建。
