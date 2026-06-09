# 做市 (engine B) 可行性 — 2026-06-09 重评估裁决

> owner: 老韩 (风控合规部主管 / RM 主权) | last_review: 2026-06-09
> 触发: 老板 2026-06-09「重新评估做市 (engine B) 可行性」—— 对我 2026-06-03 亲手做的砍做市裁决做今日条件复核。
> 性质: 纯调研裁决,不写代码。给 GM 综合后交老板拍板的风控裁决。
> 复核基线: `mm-risk-assessment-data-v1.md` (我 2026-06-03 原裁决) · `profit-scheme-v3-arb-only.md` (老板 2026-06-03 砍做市落点) · `laolei-pm-inplay-no-edge-decision-2026-06-04.md` (次日数据三杀)。
> 引用纪律 (CLAUDE.md §8.1): 凡引红线/实测,链原文出处 + 粘原话,不凭记忆转述。

---

## 0. 裁决先行 (三选一)

**裁决:【仍不能做】。** 一句话理由:**砍做市的真命门 = 检测延迟(看见知情流的能力),它由两块构成 —— sharp 覆盖率 + sharp 新鲜度。复核今天的实测:覆盖率不升反更糙(market 级 8/170 = 4.7%,缺口的大头是 bet365 在那些比赛根本不开盘的结构性数据缺,非单纯白名单),新鲜度的上游物理地板 GS 中继 2.3s 一秒没动(过去一周改的 149hz book 轮询 / 双流隔离全是【订单簿盯盘】新鲜度,不是【赔率源】延迟)。命门两块都没翻 → 撤单触发器依旧形同虚设 → 做市的尾部逆选依旧没有可依赖的安全带。** 6-04 又拿到一条比原裁决更真实(1s tick、真 cross_spread)的独立第二证据:实盘 mid 轨迹做市回测 38 盘净 **−$12.16**,仅 13/38 盘净正(`laolei-pm-inplay-no-edge-decision-2026-06-04.md` §2 行 C)—— 与我 6-03 的 197 万 tick 模拟结论同向。

**没有为迎合「想重启」软化:** 老板要我诚实,命门没变,我的结论就是仍不能做。但本裁决在 Q2 把一个**结构性退场、不依赖看见移动来临**的窄区间 MM 子策略说清楚了 —— 它**不是翻案**(仍不批真钱、不批裸挂),而是给出「什么变了才能翻」+ 一个最小可验证 paper spec 轮廓,供老板判断是否值得花 paper 周期去证伪。

---

## Q1 命门变了吗?(检测延迟拆两块各自复核)

我 6-03 裁决的命门原话(`mm-risk-assessment-data-v1.md` §0 行 20):
> 「命门从来不是撤单网络速度,是『我们什么时候能 SEE 到知情流』(检测延迟 = 数据新鲜度 1.9-5s + sharp 覆盖 11%),这一项 14ms 一分钱没改善。」

拆成 (a) 覆盖率 + (b) 新鲜度,逐块查今天。

### Q1(a) sharp 覆盖率 — 不升反更糙,缺口大头是结构性数据缺

**原裁决数字:** f18 (bet365 in-play sharp) 覆盖 **10.96%** 的 tick(`mm-risk-assessment-data-v1.md` §1 表行 f18 + §1 行 38)。这是 **tick 级**口径(全量 tick 里有多少带 sharp 锚)。

**今天实测(更新数据点,2026-06-04 `goalserve-dict-driven-result-market` 记忆 + `/api/v1/mapping/status`):**
> `/api/v1/mapping/status` 现报 total=170 matched=50 **matched_with_sharp=8** matched_no_sharp=42 no_match=120 —— 只 8/170 市场有 live bet365 sharp 源。

- 这是 **market 级**口径:170 个 PM live 市场里,只有 **8 个(4.7%)** 此刻有 live bet365 sharp 锚。
- 拆缺口:
  - **120 / 170 = 70% no_match** —— EventMatcher 配不上 GS 候选。真因不是名字/匹配器(`coverage-candidate-bound-not-names` 实测:门拒全 0,匹配器完美),是**候选池(GS live feed ~90 场)与 PM 库存错位**(GS 浪费在 PM 没 live 的 soccer,缺 PM 有的 cricket/esports/低级别 tennis)。
  - **42 / 170 = 25% matched_no_sharp** —— GS 有这比赛、配上了,但**那场比赛 bet365 不开盘**。原话(`why-no-trades-alpha-coverage`):「ITF 低级别网球 bet365 不开盘 → sharp_fair=-1」。这是 **Goalserve 免费 plan 的数据天花板**,不是我们的白名单没开。
  - **8 / 170 = 4.7% matched_with_sharp** —— 真正能拿到 sharp 的。

**关键复核结论 — 老板线索的回答:** 派单线索说「当时缺口是白名单+赛季,不是 odds plan;扩白名单能到 50%+」。**复核后这个乐观前提部分被实测推翻:**
- inplay.goalserve.com **确实带实时赔率**(value_eu,`inplay-goalserve-has-odds` 确认 inplay 节点 ≠ www base feed),这一条对。
- 但 6-04 的 8/170 实测把缺口拆开后,大头(42 matched_no_sharp)是**比赛级 bet365 不开盘的结构性缺**,扩白名单/订更多运动**补不了**(GS 给的就是没有)。能补的是 120 no_match 里「GS 其实有但候选池错位」那部分 —— 但那要靠扩候选源 + 按运动收窄分母(`coverage-candidate-bound-not-names` 的两个杠杆),且补上来的多半还是流动性差/无 sharp 的冷门盘。
- **甜区(NBA/MLB/NFL/大足球:覆盖 + bet365 sharp + 高流动性三齐)是少数**(`why-no-trades-alpha-coverage`:「系统真正甜区是大联赛…这些在美国白天/晚间 live 时整条链才顺」)。做市要在这少数甜区盘上,且只在它们 live 的时段。

**Q1(a) 裁决:** 覆盖率命门**没缓解,口径换算后比原裁决还糙**(market 级 4.7%)。即便理论上把 no_match 那块挤一挤、把覆盖率拉到名义 30-50%,真正同时满足「有 sharp + 真紧簿 + 够流动性」可做市的盘**仍是少数大联赛甜区**。覆盖率从 11% 扩到 50%+ 这个前提**今天的数据不支持**——缺口大头是 GS 不给的数据,不是我们没开的白名单。

### Q1(b) sharp 新鲜度 / 延迟 — 上游物理地板一秒没动

**原裁决数字:** 数据新鲜度 1.9-5s(`mm-risk-assessment-data-v1.md` §0/§4.4);叠加 6-04 实测 GS 落后 bet365 **P50 2.3s / P95 3.7s / max 4.7s**(`gs-bet365-latency-2300ms`)。

**今天复核 — 延迟链分解(`laolei-inplay-clock-alignment-v1.md` §3 表,原文):**

| 段 | 量级 | 可否动 | 今天状态 |
|---|---|---|---|
| ① bet365→GS 中继 | ~2.3s | ✗ 改不了 (上游) | **没动**。`gs-bet365-latency-2300ms`:「~2s 是数据源硬天花板,代码优化不动」 |
| ② 版本年龄 (GS 每 2.08s 出一版) | 0~2.08s | ✗ 物理下限 (GS 出版频率) | **没动** |
| ③ 轮询相位 | 0~1.0s | ✓ 对齐可消 ~0.5s 均 | 相位对齐算法**已被老板移除**(commit 2d9489c5「移除相位对齐算法」、000ca6a9 等)→ 这块改善实际没保留 |

**★ 必须澄清的混淆(派单点名要分清):** 过去一周(6-05~6-09)git log 里大量「新鲜度」改动 —— 149hz 主动 book 轮询、双流隔离(`sse-crossocean-firehose-onchange`)、BBR/fq 拥塞控制、gzip 压缩、SSE 看门狗 —— **改的全是【订单簿 / 盯盘 UI】新鲜度,不是【赔率源】延迟。** 证据:
- `sse-crossocean-firehose-onchange` 记忆原话:根治的是「盯盘新鲜度 >10s = 跨洋单 TCP 队头阻塞」,对象是 **book/quote 推送**。
- `active-book-poller-149hz` 记忆:149hz 是「热链 GET /book 流动性加权」,对象是 **订单簿**。
- 赔率源延迟链(①②③)在这一周**没有任何针对性改动**;唯一碰过的相位对齐(削 ③)还被老板移除了。

**Q1(b) 裁决:** 新鲜度命门的**核心部分(①+② = ~2.3s 中继 + 出版周期)物理地板完全没变**。我们看到一个 sharp 跳变时,它平均已经是 **2.3s 前**的 bet365 真值(`gs-bet365-latency-2300ms`:「那笔亏损单买 0.15 时 bet365 显示 0.186 —— 那是 2.3s 前的快照,下单时 bet365 早走到 0.10」)。这对做市的含义:**做市的撤单触发器需要『在知情流扫穿我的挂单之前看见它来』,而我们结构性地晚 2.3s 看见。**

### Q1 合并结论:即便 sharp 覆盖扩到 100%,我们能不能在知情流扫穿前撤/退?

**不能。** 这是两块独立证据的合取:
1. **覆盖率拉满也救不了延迟。** 假设魔法般把覆盖扩到 100%,GS 2.3s 中继还在 → 我们仍是晚 2.3s 看见 sharp 跳。覆盖是「有没有信号」,延迟是「信号多旧」,两个正交,扩覆盖不动延迟。
2. **实测直接量化了「撤单看不见」:** `mm-risk-assessment-data-v1.md` §4.4 原文 —— 8,777 次大幅 realized 移动(>5c)中,只有 **8.0%** 在前一 tick 有 sharp 预警,**92% 是无前兆突袭**;模拟加撤单触发器对 PnL 改善 **<0.4%**。这个数字今天没有任何新数据去推翻它,而它依赖的两个底层条件(5s tick 节奏 + sharp 覆盖)今天都没改善。
3. **14ms warm 撤单速度依旧无关。** 命门从来不是「撤得快不快」,是「看得见看不见」。撤单网络速度(14ms)对 2.3s 检测延迟是 1/164 量级,杯水车薪。

**→ 命门两块都没翻。做市的『撤单退场』安全带,今天依旧形同虚设。**

---

## Q2 窄区间做市净正吗?(说清「不依赖看见移动来临」的子策略)

我原裁决说净正只存在于「极窄区间 + 极小 size + 不靠撤单防逆选,靠『极价地板 + 紧簿 + 退场而非撤单』」(`mm-risk-assessment-data-v1.md` §0 行 20 / §8.1 行 162)。把这个子策略具体化。

### Q2.1 子策略定义(结构性退场,不靠 sharp 预警撤单)

核心特征:**报价资格 100% 由【可本地、零延迟观测的状态】决定(mid 价位 / 簿 spread / 簿深度 / 已持时长),完全不查 sharp、不等「移动来临」的信号。** 退场是结构性的(状态越界自动停报 / 到期 taker 平),不是「看见知情流→撤」。这样设计的理由正是 Q1 的结论:既然我们结构性地晚 2.3s 看见知情流,就**根本不把『看见』放进安全带的依赖链**。

子策略报价资格(全部满足才挂,任一破立即停报新单 + 已持仓走 deadline 退场):
- **价位带:** mid ∈ [0.15, 0.85] 的**非极价区**(原裁决 §6 实测崩盘尾部 p999=49.5c 全压在极价区 mid>0.85;极价区 carry 最肥但尾部最毒,窄区间策略**主动避开极价**,反 round-2 的「极价甜区」直觉)。
- **簿质量:** 真双边紧簿 best_ask−best_bid ≤ 4c(比原 RR-MM-6 的 8c 更紧)且 top3 深度 ≥ 500 USDC + **非幻影簿**(best_bid ≥ 0.06 且 best_ask ≤ 0.94,挡 `mm-risk-assessment-data-v1.md` §2 的 45% mid 区幻影簿,贴 mid 挂 = 送免费期权)。
- **运动:** 仅 tennis(`mm-risk-assessment-data-v1.md` §7:逆选强度最低,move_mean 0.053c / bigmove 0.16%)。**baseball 禁**(bigmove 0.89%,5.5× tennis)。
- **size:** 极小,受 RR-MM-1/2 三重 VaR 约束(单边库存 VaR ≤ 0.3% bankroll、逆选预算 ≤ 0.2% bankroll/场)。
- **退场(结构性,不依赖撤单):** ① 持仓超 T 秒(建议 T=15-30s,数量级 = 几个 GS tick)强制 taker 平,**不等 sharp 说该走**;② 簿一变薄(深度跌破 500 或 spread 拉宽 >4c)立即停报新单;③ 临近极价(mid 漂出 [0.15,0.85])立即停报。

### Q2.2 这样的窄区间 MM,尾部逆选还咬不咬?carry 还剩多少?

**咬,但形态变了 —— 从『崩盘 gap-through』降级为『身体小逆选』,代价是 carry 也被削薄。**

- **尾部毒性大幅下降(主动避开极价 + tennis only):** 原裁决最毒的尾部是极价区崩盘(§4.3 高价票 p999=49.5c)。窄区间策略**结构上不在极价区挂单** → 吃不到那个 50c gap-through。tennis 非极价区的单 tick |Δmid| p99 只 1.5c(§7 表),量级可控。**这是窄区间设计真正的价值:它不是『防住了逆选』,是『主动不站在逆选最毒的地方』。**
- **但身体逆选仍在,且 deadline taker 退场要付费:** 退场用 taker 平,p≈0.5 处 taker 费 = rate·p(1−p) ≈ 0.03×0.25 = 0.75c 单边(`posmgmt-predmarket-fee-v1`:p=0.5 往返 1.5c)。每次到期未自然平的仓位,退场就吃这 0.75c。
- **carry 还剩多少(诚实下界):** 原裁决 §3 实测半 spread carry,非极价紧簿 = **1.0c/fill**。窄区间(spread ≤4c)半 spread ≈ 2c → carry ≈ 1c/fill。扣掉:① deadline taker 退场费(若触发)0.75c;② 身体逆选(tennis 非极价 move_mean 0.053c,小);③ p99 尾部 1.5c 偶发。**净 carry 在『勉强正』量级 —— 这恰是 6-04 在真实 1s tick 上回测拿到的画面:**
  > `laolei-pm-inplay-no-edge-decision-2026-06-04.md` §2 行 C:「做市 carry 真实 mid 轨迹回测(乐观满成交+零费):38 盘净 **−$12.16**,仅 13/38 净正,−0.007/笔」。
  - 注意这是**乐观满成交 + 零费**的回测都净负 −$12.16 —— 加上真实 deadline 退场的 taker 费,会更糟。这是比我 6-03 的 5s tick 模拟**更新、更真实(1s tick、真 cross_spread)**的第二条独立证据,**同向**(做市 carry 扛不住逆选)。

**Q2.2 裁决:** 窄区间 MM 把尾部从「致命崩盘」削到「可控身体逆选」,**但同时把 carry 削到『勉强正甚至净负』。** 它解决了「不会被一次崩盘掏空本金」(好事,纪律意义),但**没有解决「净期望是否为正」**——6-04 真实 1s tick 回测给的是净负。窄区间是「降低破产风险」而非「创造正期望」的策略。

### Q2.3 它和已建好的「目标仓位控制(单边跟随 sharp)」是冲突还是可叠加?

**结论:结构性冲突,不可在同一 condition 同时跑;但在『系统范式』层面,目标仓位控制已经吃掉了窄区间 MM 想要的大部分好处,做市的增量价值很小。**

- **目标仓位控制现状(已建好,`laolei-position-management-synthesis-v1.md` §1/§4.1):** 方向 = 赔率源 sharp 低估边、规模 = Kelly、**reservation 限价不追(maker 0 费)**、控制器 order = target−current 被动 rebalance。已落地相关性折扣 Kelly(commit cb72b80c)、毒性冻结加仓硬档(cb72b80c)、p(1−p) 死区(b245e266)、DD→target 乘子、CLV sizing。
- **冲突点 1 — 方向语义打架:** 目标仓位控制是**单边方向性**(只在 sharp 说低估的那一边建仓,有观点)。双边做市是**无方向 carry**(两边都挂,赚价差,无观点)。同一 condition 上,一个说「我看好 YES 所以买 YES」,另一个说「我两边都挂等成交」——`mm-risk-assessment-data-v1.md` §8.2 RR-FUND-2「同 condition 引擎互斥」本就禁止两引擎同盘。
- **冲突点 2 — maker reservation 已经在『结构性吃 carry』:** 目标仓位控制的 reservation 限价单本身就是 maker(0 费),被动成交时**天然吃到半 spread carry**(`laolei-position-management-synthesis-v1.md` §0 行 24:「现 reservation 限价范式天然吃到」)。也就是说,**窄区间 MM 想要的 carry,单边跟随 sharp 的限价单已经在顺手赚了**,只是它赚的是「在我看好的方向上用 maker 价进场,省了 taker 费 + 偶尔吃到对手的价差」,不是「两边挂做市」。
- **可叠加的唯一窄缝:** 理论上,在**没有 sharp 信号的盘**(42 个 matched_no_sharp + 部分 no_match)上,目标仓位控制因为「方向真值缺失(2026-06-05 老板定调:无 sharp 不许靠 ML 单独定方向)」而**不出手**,这些盘上双边 carry MM 不与单边控制冲突(井水不犯河水)。**但这恰是数据最差的盘**(无 sharp 锚 → 无法估 fair → 无法判断挂的价是不是在送期权 → 退回 Q1(a) 的覆盖命门)。在无 sharp 盘做市 = 蒙眼挂单,正是原裁决 §2/RR-MM-7 必禁的「送免费期权」。

**Q2.3 裁决:** 窄区间 MM 与单边跟随 sharp **在有 sharp 的甜区盘上结构性冲突(互斥,且后者已顺手吃了 carry)**;在无 sharp 的盘上不冲突但**那里做市就是蒙眼送期权(必禁)**。**→ 做市相对现有目标仓位控制的增量价值很小,且增量主要落在数据最差、风险最高的地方。**

---

## Q3 更新裁决

### Q3.1 明确裁决:【仍不能做】

复核今天条件,做市从「有条件可做、当前不满足前置条件」(6-03 原裁决)**进一步弱化为「仍不能做」**,理由比 6-03 更强:

1. **命门两块都没翻(Q1):** 覆盖率换 market 级口径后更糙(4.7%),且缺口大头是 GS 不给的结构性数据缺(扩白名单补不了);新鲜度上游 2.3s 物理地板一秒没动(过去一周改的全是订单簿盯盘新鲜度,与赔率源延迟无关)。撤单触发器依旧只能预警 8% 的突袭。
2. **多一条更真实的反证(Q2.2):** 6-04 在 1s tick + 真 cross_spread 上跑做市回测,乐观满成交 + 零费仍净 **−$12.16**(38 盘 13 正),与 6-03 的 197 万 tick 模拟同向。
3. **增量价值被现有范式吃掉(Q2.3):** 单边跟随 sharp 的 maker reservation 已顺手吃 carry;做市的增量只落在无 sharp 的数据最差盘 = 蒙眼送期权。

### Q3.2 什么变了才能翻(可证伪的翻案条件)

我把翻案门画死,任一缺失即不翻。**这些是『检测延迟命门』的解,不是绕过:**

| 翻案条件 | 当前状态 | 怎么算达标 |
|---|---|---|
| **T1. 赔率源延迟降到亚秒** | 否(GS 中继 2.3s 物理地板) | 换一个 ≤1s 端到端的 sharp 源(直采 bet365 / Pinnacle / 多博彩商共识),且**不违 ToS、不灰色**。注:bet365 直采已被 Cloudflare 硬封 + 实测真窗口仅 ~0.3s(`no-bet365-pm-convergence-edge`),此路基本死。 |
| **T2. sharp 覆盖到甜区盘 ≥80%** | 否(market 级 4.7%) | 在**目标做市的那些 condition 上**(不是全盘平均)sharp 覆盖 ≥80% 的 tick + book 新鲜度 ≤2s(原 RR-MM-5b)。注意缺口大头是 GS 不开盘,需换源。 |
| **T3. 逐笔成交 tape(taker 主动方)** | 否 | 拿到我方价位的逐笔成交方向/size,才能无偏估「良性 fill 占比」→ 才能算真 carry 净期望(`mm-risk-assessment-data-v1.md` §9 行 202)。当前所有回测的 fill 模型都有偏(只选中逆选 fill)。 |
| **T4. paper 实测净正 + CLV** | 否 | 窄区间 MM paper 跑出净正 + maxDD ≤15% + CLV 验证,且 baseline = 同期单边跟随 sharp(证明 MM 有增量,不是重复吃同一份 carry)。 |

**T1+T2 是命门门(不达标连试都不该试)。T3+T4 是放真钱门。** 当前 T1-T4 **全否**。

### Q3.3 最小可验证窄区间 MM spec 轮廓(供 paper 验证,不是上线)

**前提声明:这不是批准开做市。** 这是给老板一个「如果想花 paper 周期去证伪/证实窄区间 MM 是否净正」的最小 spec。它在 PAPER_MODE 下跑,**不碰真账本**(R-11),**不触 LiveOrderGate.Arm()**(R-11/§8.1 真钱开闸需老韩 RM + 小白安全会签),纯观测产出 PnL/maxDD/换手率/逆选分解。

```
准入(全满足才挂,任一破停报新单):
  sport == tennis
  mid ∈ [0.15, 0.85]                       # 避开极价崩盘尾部
  best_ask − best_bid ≤ 4c                  # 真紧簿
  top3_depth ≥ 500 USDC                     # 非薄簿
  best_bid ≥ 0.06 且 best_ask ≤ 0.94        # 非幻影簿(挡送期权)
  该 condition 当下无单边跟随 sharp 持仓     # 引擎互斥(RR-FUND-2)

报价:
  双边 maker 挂 bid = best_bid+1tick / ask = best_ask−1tick(改善价但不穿越)
  size 受 RR-MM-1(库存 VaR ≤0.3% bankroll)+ RR-MM-2(逆选预算 ≤0.2%/场)夹死

退场(结构性,零延迟本地状态触发,不查 sharp):
  持仓时长 > T(15-30s)→ 强制 taker 平(吃 0.75c 费,认了)
  簿变薄(depth<500 或 spread>4c)→ 停报新单
  mid 漂出 [0.15,0.85] → 停报新单 + deadline 平已有仓
  事件窗内(进球/破发 info.state 跳变)→ 立即停报(RR-MM-3)

度量(paper 产出,判净正用):
  净 PnL / 笔 + maxDD + 换手率 + carry/逆选/退场费三段分解
  baseline 对照:同期同盘单边跟随 sharp 的 PnL(证明 MM 有增量)
  验收门:净正 + maxDD≤15% + 增量 > 0(否则证伪,做市继续砍)
```

**这个 spec 的诚实预期(我的先验):** 大概率证伪(6-04 真实回测已经净负)。但它便宜(paper、不碰真钱、复用已有 reservation/sizing/RM 脚手架),且能把「做市净正吗」从「靠模拟争论」变成「靠 paper 实测一锤定音」。**值不值得花这个 paper 周期,是老板的 PnL 优先级判断,不是我的风控判断。我的风控判断只到这里:paper 可跑(fail-closed、不碰真钱),真钱绝不开,直到 T1-T4 全绿 + 会签。**

---

## 附:本次复核的数据出处一览(可追溯)

| 论断 | 出处 |
|---|---|
| sharp 覆盖 tick 级 11% | `mm-risk-assessment-data-v1.md` §1 表 f18 行 / §1 行 38 |
| sharp 覆盖 market 级 8/170=4.7% + 42 no_sharp(bet365 不开盘)| `goalserve-dict-driven-result-market` 记忆 / `/api/v1/mapping/status` 2026-06-04 |
| 缺口大头是结构性数据缺非白名单 | `why-no-trades-alpha-coverage`(ITF bet365 不开盘 → sharp=-1)|
| GS 中继延迟 P50 2.3s/P95 3.7s 物理地板 | `gs-bet365-latency-2300ms` / `laolei-inplay-clock-alignment-v1.md` §3 表 ① |
| 延迟链 ①②不可动、③相位已被移除 | `laolei-inplay-clock-alignment-v1.md` §3 / git commit 2d9489c5 |
| 一周改的是订单簿盯盘新鲜度非赔率源延迟 | `sse-crossocean-firehose-onchange` / `active-book-poller-149hz` 记忆 |
| 撤单触发器只预警 8%、92% 无前兆、改善<0.4% | `mm-risk-assessment-data-v1.md` §4.4 行 100-104 |
| 1s tick 真实做市回测 38 盘净 −$12.16 | `laolei-pm-inplay-no-edge-decision-2026-06-04.md` §2 行 C |
| 极价区崩盘 p999=49.5c / tennis 最优 baseball 最毒 | `mm-risk-assessment-data-v1.md` §6 / §7 表 |
| 幻影簿 45% 送期权必禁 | `mm-risk-assessment-data-v1.md` §2 / RR-MM-7 |
| 单边跟随 sharp 已建好 + reservation 天然吃 carry | `laolei-position-management-synthesis-v1.md` §0/§1/§4.1 |
| 方向真值=赔率源 sharp、ML 不可靠永不单独定方向 | `odds-source-is-truth-quant-unreliable` 记忆(老板 2026-06-05 定调)|
| PM 真实滞后 bet365 仅 ~0.3s、收敛套利 edge 结构不存在 | `no-bet365-pm-convergence-edge` 记忆 |

---

**一句话给老板:** 6-03 我砍做市的命门是「看不见知情流(覆盖 + 延迟)」,今天复核两块都没翻 —— 覆盖换 market 级口径更糙(4.7%,且缺口是 GS 不给的数据不是白名单)、延迟上游 2.3s 一秒没动(这周改的全是盯盘订单簿新鲜度,不是赔率源)。6-04 又拿到一条更真实的反证(1s tick 做市回测乐观满成交零费仍净 −$12.16)。**裁决:仍不能做。** 翻案要 T1 亚秒 sharp 源 + T2 甜区覆盖 80% + T3 逐笔 tape + T4 paper 净正,当前全否。若老板想一锤定音「窄区间 MM 到底净不净正」,我给了一个 paper-only、不碰真钱、复用现有脚手架的最小 spec 去证伪 —— 我的先验是它会证伪,但跑它便宜,要不要跑是 PnL 优先级判断。**真钱绝不开,直到四门全绿 + 会签。纪律 > PnL。**
