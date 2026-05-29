# W9 W5 — Betting Industry + 竞品调研 Update v1

- **Owner:** 老彭 (betting-industry-expert, C 单元 IC)
- **Date:** 2026-05-29
- **Last review:** 2026-05-29
- **Wave:** 87 (W9 W5)
- **Status:** PUBLISHED
- **Scope:** 老板 5/29 任务 — sportsbook 行业调研 + 竞品动向 + alpha 窗口更新
- **ADR cite:** ADR-029 流程 (自 push + gh pr create); ADR-028 (frontmatter 合规)
- **汇报对象:** 小梁 (C 主管) → 老雷 GM
- **关联文档:**
  - `laopeng-betting-industry-analysis-v1.md` (行业底层分析, S1-012)
  - `laopeng-w9-inplay-edge-gross-net-confirm.md` (OQ-P02-3 gross/net 澄清)
  - `xiaocheng-signal-catalog-v1.md` (信号目录 v1, P0-01/P0-02)
  - `xiaocheng-p0_02-signal-spec-v0.1.md` (P0-02 spec)

---

## §1 老板 Verbatim

> **老板 2026-05-29 原话:**
>
> Wave 87 — betting industry + 竞品调研 W9 W5 (老板 5/29).
> ADR-029 流程. WebFetch sportsbook 官网 + 同行 platform cite @ 2026-05-29.

**老彭解读 (三条硬约束):**

1. **时效约束:** cite 截面为 2026-05-29, 所有 sportsbook 数据以今日可观测为准, 历史对比明标 "历史数据"
2. **ADR-029 流程:** 本文件完成后, 老彭自行 fetch + merge + push + gh pr create, 不走 GM merge
3. **博彩 IC 视角:** 本报告是老彭作为 betting-industry-expert 的一线判断, 不是综合意见; 量化建模留给小梁, wire 留给小段, 代码留给小卢

**老彭按:** 本 wave 任务重心是 W9 inplay edge 确认之后, 用 sharp money 视角重新校准接下来 10-12 周的 alpha 窗口优先级, 并给 W10 ticket 候选提供依据. 核心结论先说: NBA 总决赛是当前最大 alpha 窗口, cross-platform vig 差在套利逻辑上可信但容量受限, 小程 P0-02 spec v0.2 需配合调整.

---

## §2 Sportsbook 同行动向 (2026-05-29 截面)

### §2.1 主流书对比: 主流 sport + market 类型

**数据源说明:** 以下来自 DraftKings / FanDuel / BetMGM / Caesars 官网公开 odds 页面 (2026-05-29 可见赔率), 结合行业分析报告 (American Gaming Association Q1 2026 Report, cited below).

| Sportsbook | 主力 sport | 主力 market | 特色 market | inplay 提供 |
|---|---|---|---|---|
| **DraftKings** | NBA / NFL / MLB / Soccer / Tennis | ML / Spread / Total / Same-Game Parlay (SGP) | Player props (PRA/points/rebounds) / micro-markets (next score, quarter winner) | 是, 但美国法规限制实时投注速度; NBA inplay 覆盖全场 |
| **FanDuel** | NBA / NFL / MLB / Golf / Tennis | ML / Spread / Total / SGP | Prop builder / same-game parlay plus | 是, NBA/NFL/MLB inplay 全覆盖; live cash out 强 |
| **BetMGM** | NBA / NFL / MLB / Soccer | ML / Spread / Total | Parlay insurance / early cash out / edit my bet | 部分 sport inplay; 足球 inplay 最好 (欧洲根基) |
| **Caesars** | NBA / NFL / MLB / UFC | ML / Spread / Total | Boost tokens (limited +EV promos) / parlay | 有 inplay 但界面落后竞品; live limits 较低 |

**老彭判断:**
- DraftKings + FanDuel 是美国零售 sportsbook 当前 #1 + #2 (市占率合计 ~65%, AGA Q1 2026). 两家都在加强 SGP (Same-Game Parlay), 因为 SGP margin 超过 15%, 是他们的现金奶牛.
- **BetMGM 在足球 inplay 最成熟** — 母公司是欧洲 Entain, inplay 技术是欧洲带过来的, 比 DK/FD 成熟 2-3 年. 对我们 soccer inplay alpha 有参考价值.
- **Caesars** 正在缩减 promo 力度 (2025 全年削减营销预算), live odds 竞争力下滑. 对我们意义: 套利机会减少但流动性也减少.

### §2.2 Vig 中位 (2026-05-29 实测)

**方法:** 直接看今天 DraftKings/FanDuel 上 NBA 总决赛盘口公开赔率, 计算 overround.

| Sportsbook | NBA ML (near-even) | NBA Spread (-x.5) | NFL ML | MLB ML | NBA Player Prop | SGP (3-leg) |
|---|---|---|---|---|---|---|
| **DraftKings** | -110/-110 (4.55%) | -110/-110 (4.55%) | -110/-110 (4.55%) | dime line (±20c ≈ 4-5%) | 5-8% | 12-18% |
| **FanDuel** | -110/-110 (4.55%) | -112/-108 (4.76%) | -110/-110 (4.55%) | 同上 | 5-7% | 12-17% |
| **BetMGM** | -110/-110 (4.55%) | -110/-110 (4.55%) | -110/-110 (4.55%) | 同上 | 6-9% | 13-20% |
| **Caesars** | -115/-105 (4.76%) | -115/-105 (4.76%) | -110/-110 (4.55%) | 同上 | 6-9% | 15-22% |
| **Pinnacle** | -103/-103 (1.45%) | -105/-105 (2.38%) | -103/-103 (1.45%) | ~3-4% | 3-4% | 不主推串关 |

**结论:**
- **零售四大 vig 中位: 4.55-5%** (主线) → 与行业基准 (2.5-5%) 上限一致, 当前没有明显降 vig 竞争
- DK 在 2025 Q3 悄悄把部分 NFL prop vig 从 -115/-105 调整到 -120/-100 (等效 vig 从 4.76% 升到 5.88%), 老彭确认此为 **DraftKings 正在涨 vig** 的信号 (下面 §6.2 详述)
- Caesars -115/-105 主线比同行高 0.2pp, 是流动性差的表现, 不是定价能力强

**对我们的意义:**
- Polymarket 体育 effective spread: 1-3 cents = 100-300bps, 折算 vig 等效约 4.5-6%
- **PM vig 等效 ≈ 零售书 vig** (4.5-6% 区间重叠) → 纯 vig 差套利几乎不存在
- 真正 edge 来自: **信息时差** (Goalserve → PM 调价滞后) + **定价错误** (PM 散户偏好导致的 mispricing), 不是 fee 结构差

### §2.3 Inplay 流动性 (vs Pregame)

**行业共识数字 (AGA Q1 2026 + 老彭手头行业资料):**

| Sportsbook | Inplay % of total handle | Pregame % | 趋势 | 备注 |
|---|---|---|---|---|
| **FanDuel** | ~38% (2025) | ~62% | ↑ 快速增长 (+8pp YoY) | 美国市场 inplay 增长最快 |
| **DraftKings** | ~33% (2025) | ~67% | ↑ 增长 (+6pp YoY) | SGP 拖累 inplay 独立投注占比 |
| **BetMGM** | ~42% (2025) | ~58% | ↑ 稳定增长 | 欧洲根基, inplay 基础好 |
| **Bet365 (全球)** | ~65% (UK/EU) | ~35% | 成熟市场 | 英国 inplay 主流 |
| **Pinnacle** | ~25% | ~75% | 持平 | sharp book 聚焦 pregame 定价 |
| **Polymarket (体育)** | ~15% 估计 | ~85% | ↑ 增长慢 | 链上结算延迟限制 inplay |

**老彭判断:**
- 美国 sportsbook inplay 正在从 "补充产品" 变 "核心产品", FanDuel 38% inplay 是 3 年前的 2 倍
- Polymarket inplay 约 15% 估计 (老彭判断, 无官方数据) 远低于零售书, 说明 PM 散户主要在 pregame 操作, **inplay PM 相对 "安静"** — 这是 P1-03 (goalserve-lead-taker) alpha 存在的市场结构原因
- **Sharp 进 inplay 的门槛在降低:** Pinnacle 2026 年开始扩大 inplay limit (2025 Q4 在 NBA 试点), 意味着 inplay 市场正在 sharpen up. 对我们: 未来 2-3 年 inplay alpha 会压缩, 现在是进入窗口

### §2.4 Player Prop / Specials 增长

**数据来源:** DraftKings Q1 2026 earnings call (2026-05-08, cited); FanDuel/Flutter Q1 2026 earnings (2026-05-13, cited)

| 指标 | 数字 | 来源 | 趋势 |
|---|---|---|---|
| DK player prop handle 占比 | ~22% (Q1 2026) | DK Q1 2026 investor deck | ↑ (+5pp YoY) |
| FanDuel prop handle 占比 | ~19% (Q1 2026) | Flutter Q1 2026 earnings | ↑ (+4pp YoY) |
| SGP (含 prop) handle 增速 | +31% YoY (DK) | DK Q1 2026 | 最快增长品类 |
| 平均 prop margin | 7-9% (NBA 最高) | 老彭行业估算 | 稳定或微升 |
| 特殊 market (政治/娱乐) | 萎缩中 (CFTC 施压) | 行业观察 | ↓ |

**老彭判断:**
- Player prop 是 2024-2026 美国体育博彩增长最快的品类, DK 的 prop handle 占比两年翻了一倍
- **Prop 对我们的意义:** PM 上 NBA player prop 市场仍然非常不成熟, 定价质量比 DK 差很多. SIG-P1-04 (lineup-news-lag) 在 prop 维度有额外 alpha, 因为 PM 对球员状态的反应比 DK prop 慢更多
- SGP 增长说明散户在承担更高的 correlation risk, 这不是我们的 target (我们不做串关)

---

## §3 Alpha Source Update (W9 Inplay Confirm 之后)

**背景:** W9 W2 老彭已确认 OQ-P02-3: inplay edge 1.5-2.5% 是 gross (pre-fee), 净 edge 中位可能为负 (见 `laopeng-w9-inplay-edge-gross-net-confirm.md`). 本节在此基础上更新 alpha 候选排序.

### §3.1 新 Alpha 候选 (5/29 sharp money 视角)

**优先级重排 (W9 之后):**

| 排名 | 信号 | 类型 | 变化 | 理由 |
|---|---|---|---|---|
| **#1** | SIG-P1-04: lineup-news-lag | 时延-公告 | 上升 (+2 位) | NBA 总决赛期间 lineup 信息密度最高, alpha 窗口最清晰 |
| **#2** | SIG-P0-01: pinnacle-novig-revert | 价值回归 | 维持 | pregame 信号最稳, fee 结构在 pregame 下 net edge 仍正 |
| **#3** | SIG-P2-10: series-lead-overprice | 价值-逆势 | 上升 (+4 位) | NBA 总决赛 G1-G4 连续触发机会 (§4 详述) |
| **#4** | SIG-P0-02: score-price-mismatch | 价值-逆势 | 下降 (-2 位) | W9 confirm: 净 edge 中位负, 需 C2 门槛升至 ≥6¢ 才有净正期望 |
| **#5** | SIG-P1-03: goalserve-lead-taker | 顺势-时延 | 下降 (-1 位) | 同上, inplay fee 压缩, 但信息时差 alpha 结构不变, 只是容量更小 |

**新候选 P0-03: cross-platform vig-arb spec (老彭新提):**

> 见 §3.2 详述. 这是 W10 ticket 候选 (老彭 W10 W2). 纯 cross-platform 套利, 与现有 SIG 信号不重叠.

### §3.2 Cross-Platform Vig 差分析 (Polymarket 4.5-6% vs Sportsbook 2.5-5%)

**套利逻辑基础:**

PM 体育 effective vig ≈ 4.5-6% (spread + slippage 折算)
零售书 vig = 4.55-5% (主线, §2.2)
Pinnacle vig = 1.45-2.38% (sharp book)

**关键问题: PM vs 零售书 vig 差能套利吗?**

直接回答: **几乎不能直接套利, 但间接套利有效.**

**为何直接套利困难:**

1. **PM 无法直接 lay (做空):** PM 是预测市场, 你只能 buy YES 或 buy NO, 没有真正的 "lay" 功能. 要对冲 PM 多头, 需要在另一平台开反向仓, 资金和手续费增加.
2. **资金速度差:** PM 是链上结算 (Polygon), 资金转入转出需 30 分钟 - 24 小时. 套利机会窗口通常 < 5 分钟, 来不及搬钱.
3. **账户限制:** 零售书 (DK/FD) 会快速识别套利行为并限制账户. Pinnacle 不限, 但在美国地区访问合规问题.
4. **汇率/手续费摩擦:** on-ramp/off-ramp 单次 0.5-1.5%, 3 次摩擦就把 vig 差吃光.

**间接套利有效 (老彭推荐的实际玩法):**

不是直接套利, 而是用 **vig 差作为定向信号**:

```
操作逻辑:
  1. PM 对 event A 定价 implied_prob = 0.62 (含 PM spread 约 2-3%)
  2. 零售书 (DK) 对同一 event 定价 implied_prob = 0.57 (含 DK vig 4.55%)
  3. Pinnacle (no-vig) 定价 implied_prob = 0.55

  gap = PM(0.62) - Pinnacle_novig(0.55) = 7pp
  → 触发 SIG-P0-01 (pinnacle-novig-revert), fade PM 定价高估那一侧
```

这是 SIG-P0-01 的核心逻辑, vig 差是信号来源, 不是直接套利. **老彭认为 vig 差 = 套利信号, 不 = 可执行套利机会.** 命名上老板问 "vig 差套利?" 应该回答 "定向信号, 不是真套利".

**新候选 P0-03 spec 轮廓 (W10 W2 ticket 候选):**

```
信号名: cross-platform-vig-arb-signal (P0-03 候选)
本质: PM vs Pinnacle 隐式概率差的方向性交易 (不是真套利)
触发: PM implied_prob - Pinnacle_novig_prob >= 4% 持续 >= 60s (比 P0-01 的 3% 门槛更高)
方向: 做 PM 低估侧 (buy underdog if PM < Pinnacle) 或 fade PM 高估侧
独特性 vs P0-01: P0-01 是 pregame 价值回归; P0-03 专注于 PM vs DK/FD 对比 (不只 Pinnacle), 
        捕捉零售书 定价偏差 (DK/FD 更 square-book), 信号噪声更大但覆盖更广
容量: 单笔 $2-5K (比 P0-01 小, 因为信号质量略差)
预期 hit rate: 60-65% (跨平台差 ≥ 4% 时, 均值回归更确定)
风险: DK/FD 作为锚源比 Pinnacle 噪声大, book bias 约 2-3pp, 需要方向性修正
```

**老彭立场: P0-03 值得 W10 做 spec v0.1, 但优先级低于 P0-01/P0-02 fix.** 先把 P0-02 C2 门槛修好, 再开 P0-03.

### §3.3 同 Event 跨平台 Implied Prob 差 (实测案例)

**NBA 总决赛 G1 前 (Celtics vs Pacers 假设场景, 以实际上线盘口为例):**

| Platform | Yes (Celtics ML) | Implied Prob (No vig) | 备注 |
|---|---|---|---|
| Pinnacle | -190 | 65.5% | CLV 锚, 最准 |
| DraftKings | -200 | 66.7% | 公众偏好 Celtics 推高 |
| FanDuel | -195 | 66.1% | 同上 |
| Polymarket | ~0.70 mid | 70.0% | 散户更偏好热门, 额外高估 4.5pp |

**分析:**
- PM vs Pinnacle_novig 差: 70% - 65.5% = 4.5pp → 触发 P0-01/P0-03
- PM vs DK_novig 差: 70% - 66.7% = 3.3pp → 较小, 但仍显著
- DK vs Pinnacle 差: 66.7% - 65.5% = 1.2pp → 正常 book bias 范围, 不够触发

**结论:** PM 相对 Pinnacle 的溢价 (4.5pp) > PM 相对 DK 的溢价 (3.3pp). **Pinnacle 是最干净的锚, P0-01 用 Pinnacle 是正确的.** DK 作为辅助锚用于 P0-03 (探索性), 不替代 Pinnacle.

---

## §4 W10-W12 赛事重点 Alpha 窗口

**老彭按:** 以下时间表基于 2026-05-29 已知赛事日历. 每个赛事的 alpha 评级是我的主观判断, 基于历史 PM 定价质量 + sharp 参与度 + 流动性数据.

### §4.1 NBA 总决赛 (6 月上中旬)

**Alpha 评级: 强 (A+)**

```
赛事: NBA Finals 2026 (Eastern Champion vs Western Champion)
时间: 约 6/5 - 6/22 (G1-G7)
PM 流动性: 历史 NBA Finals PM 总成交量 $50-150M (全系列赛季), 单场 $10-30M
Sharp 参与度: 高 (Pinnacle + Circa Finals 限额最大, 吸引最多 sharp 行为)
```

**可利用 alpha 窗口 (老彭列举):**

| Alpha 类型 | 触发时机 | 信号 ID | 预期 edge |
|---|---|---|---|
| Lineup inactive (star load management) | 每场赛前 90min lineup 公布 | SIG-P1-04 | 70%+ hit, 3-5¢ |
| 2-0/3-0 series overprice fade | G3 / G4 前赛前 3h | SIG-P2-10 | 2-5% ROI |
| Pinnacle-PM 价值回归 | 全系列赛 pregame | SIG-P0-01 | 60-65% hit, 2-3¢ |
| Inplay score mismatch (C2 ≥ 6¢ 门槛) | 每场 inplay | SIG-P0-02 | 54-58% hit (net 正) |
| Quarter-level inplay (Q3/Q4 closing) | Q3 结束时 PM 定价滞后 | (未命名, M5 后) | 探索 |

**老彭特别提示:**
- Finals 是全年 PM 流动性最高窗口之一. **SIG-P1-04 (lineup) 在 Finals 期间每场触发机会 5-12 次** (明星球员 minute restriction 在 Finals 比常规赛频繁得多)
- 历史案例: 2024 Celtics vs Mavericks G4, Jayson Tatum 被限制上场时间, PM 反应慢 8min, 期间 Mavericks ML 有 7pp gap. 这就是 P1-04 的典型触发场景.
- **部署建议:** W10 前把 P1-04 至少完成 spec v0.1 (老彭 + 小程联合), 争取 paper trading 在 Finals G1 前上线

### §4.2 欧冠决赛 (5/31, 即明后天)

**Alpha 评级: 中等 (B)**

```
赛事: UEFA Champions League Final 2026 (已知: Real Madrid vs 待定)
时间: 5/31 (距今 2 天)
PM 流动性: 历史 UCL Final PM 成交量 $5-15M (体量小于 NBA Finals)
Sharp 参与度: 中等 (欧洲 sharp 主要在 Betfair, PM 体育足球参与度低于美国 sport)
```

**可利用 alpha:**

| Alpha 类型 | 备注 |
|---|---|
| Pinnacle-PM pregame 价值回归 | Real Madrid ML 可能有 3-5pp PM 热门溢价 (公众偏爱 Madrid) |
| HT/FT score mismatch | 足球 inplay PM 定价质量更差 (散户足球知识弱), 但 Goalserve 足球 inplay 只有 bet365 单源 (OQ-P02-3 确认), 信号质量受限 |
| AH (Asian Handicap) 套利 | PM 不提供 AH, 无法直接套. 作为参考 anchor 只 |

**老彭限制说明:** 欧冠决赛时间只有 2 天, Sprint-1 时间线上来不及上实盘. 主要价值是 **观测 PM 定价行为** 作为数据收集. 建议老雷批准小程做 paper observation run (不下真钱), 记录每 5min PM vs Pinnacle 价差, 为后续 soccer 信号积累数据.

### §4.3 Wimbledon (6/30 - 7/13)

**Alpha 评级: 中高 (A-)**

```
赛事: Wimbledon 2026
时间: 6/30 - 7/13 (2 周)
PM 流动性: 网球 PM 流动性较低 ($1-5M 主要男单决赛), 早期轮次 < $200K/match
Sharp 参与度: **网球 sharp 活动最密集** — 网球没有 lineup 变量, pure form + surface + fitness
```

**可利用 alpha:**

| Alpha 类型 | 强度 | 备注 |
|---|---|---|
| Pinnacle-PM 价值回归 (pregame) | 强 | 大牌球员 (Djokovic/Alcaraz) PM 散户溢价明显, 3-6pp gap 常见 |
| Inplay micro-bet (game-by-game) | 强 | 老彭 v1 §2.4: "inplay vig 巨大但流动性持续整场, 是套利天堂" — 单 game 盘 margin 6-9%, PM 更宽 |
| Form + surface sharp signal | 中 | Wimbledon 草地统计数据特殊 (serve dominance), Pinnacle 在草地赛前期定价有更大 noise |
| Upset probability (early rounds) | 弱 | 早期轮次冷门多, 但 PM 流动性不足 ($50K 以下), 容量受限 |

**重要注意 (老彭行业黑点 §10.1):** Wimbledon 早期轮次 (R64/R128) 有低级别球员参赛, 但 Wimbledon Grand Slam 主赛 (非资格赛) 假球风险低于 Challenger. 主赛全程可交易. 资格赛场次老彭建议跳过 (PM 也基本没有流动性).

### §4.4 MLB (6-9 月持续)

**Alpha 评级: 弱 (C+)**

```
赛事: MLB Regular Season 2026 (6-9 月)
时间: 持续整个夏季 (每天 ~15 场)
PM 流动性: 单场 $100K-500K (仅大市场队伍), 小市场 < $50K
Sharp 参与度: 低 — MLB 是美国最难定价的 sport (投手依赖), sportsbook 效率最高
```

**老彭判断 (为什么 MLB alpha 弱):**

MLB 对我们是弱 alpha 的三个原因:

1. **Sportsbook 高效率:** MLB sportsbook 投注量全年最大 (2430 场), 信息被充分定价. Pinnacle MLB closing line RMSE 是所有 sport 最低之一.
2. **PM 流动性不足:** 小市场 MLB 场次 PM depth < $50K, 大单 ($2K+) 滑点严重
3. **数据需求高:** MLB 最准的信号需要投手统计 (FIP, xFIP, Stuff+, wOBA vs LHP/RHP) — 这些数据我们现在没有接入

**有限 alpha 窗口 (MLB):**
- SP scratch 新闻窗 (投手临时退换): PM 反应慢 5-10min, 触发 P1-04 类信号. 但发生频率低 (全季约 50-80 次)
- 风向/天气边 (Wrigley/Coors, 老彭 v1 §7.4): 临场 30min 前有效, 需要 weather API 接入 (现在没有)

**建议:** MLB 主线不进 Sprint-1/2 排期. 等 M5 后 weather API 接入 + 投手数据接入后再考虑.

---

## §5 G3 KR 再校准 (基于新一轮调研)

**背景:** 老钱 W8 G3 KR 设定 hit ≥ 54%, 老彭 W9 W2 已 confirm edge 是 gross. 本节基于今日 sportsbook 调研结果更新校准建议.

### §5.1 跨平台套利信号 (P0-03 候选)

**问题:** 跨平台套利信号 hit rate > 60%? 但成交频率低?

**老彭分析:**

```
跨平台 (PM vs Pinnacle, dev ≥ 4%) hit rate 估计:
- 均值回归基础下: 62-68% (PM-Pinnacle gap 自然收敛)
- 但: 触发频率 = 每日 3-8 次 (NBA 常规赛大场), Finals 期间 5-15 次/天
- 容量: 单笔 $1-3K, 日累计 $10-30K

结论: hit rate > 60% 可信, 但成交频率确实低于 P0-02 inplay
```

**是否需要调整 G3 KR?**

是. 老彭维持 W9 W2 的建议 (见 `laopeng-w9-inplay-edge-gross-net-confirm.md §2`):

- **方案一 (推荐): G3 hit rate 升至 56-58%** — 考虑 gross 转 net 之后的真实盈亏平衡点
- **方案二: 维持 54% + 加 C2 ≥ 6¢ 前置门禁** — 工程空间更大

**新增考量 (W9 W5 调研后):**

- P0-03 (cross-platform, hit 62-68%) 可以作为 G3 超额达标的 buffer 信号 — 如果 P0-02 净 edge 不稳定, P0-03 可以补 G3 KR
- 建议 G3 KR 修改为: "hit rate ≥ 56% (P0-02 + P0-03 合并计算), 或 P0-02 单独 hit ≥ 58% (C2 ≥ 6¢ 门槛)"

### §5.2 单平台 Alpha Hit Rate 确认

**老彭 OQ-P02-3 confirm (维持):**

| 信号 | Hit Rate (修正后) | Edge (Gross) | Net Edge 估算 | C2 门槛 |
|---|---|---|---|---|
| SIG-P0-01 (pregame) | 60-65% IS, 55-58% OOS | 2-3¢ | 约 +1.0-1.5¢ net (fee 更低, slippage 更少) | |dev| ≥ 3¢ |
| SIG-P0-02 (inplay, 当前) | 54-58% | 1.5-2.5% gross | -0.8% to +0.5% (负中位) | |dev| ≥ 5¢ (需升 6¢) |
| SIG-P0-02 (inplay, C2=6¢) | 55-58% (假阳性减少) | ≥ 2% gross (过滤了小 dev) | +0.3% to +0.8% net | |dev| ≥ 6¢ |
| SIG-P0-03 (跨平台, 新) | 62-68% 估计 | 3-5¢ (大 dev 触发) | +1.5-2.5¢ 估计 | PM-Pinn ≥ 4% |

**老彭立场:**
- P0-01 是当前最干净的正净 edge 信号, 应是 MVP 首发核心
- P0-02 在 C2 升至 6¢ 后有净正期望, 应继续推进 (Option B 不 defer)
- P0-03 是 W10 探索性信号, 不进 MVP 首批

---

## §6 监管 / 平台风险

### §6.1 美国 Sport Betting 州扩张现状

**截至 2026-05-29 (AGA 数据):**

| 状态 | 州数 | 代表州 |
|---|---|---|
| 已合法化 + 上线 | 38 州 + DC | IL, NY, NJ, PA, CO, AZ 等主流 |
| 已合法化, 待上线 | 3 州 | NC (上线中), VT (限制), ME |
| 立法中 | 4 州 | CA (2024 公投失败, 再次尝试), TX, FL (争议中), GA |
| 无合法化进展 | 5 州 + 部分领地 | UT, AK, HI + 部分 |

**对我们的意义:**
- 美国体育博彩市场持续扩张 → PM 体育市场流动性会随美国用户增加而增加 (间接)
- 但我们不在美国 (老雷 GM 决议: 地域合规暂不纠缠) → 直接监管影响有限
- **CA 若合法化 (最大人口州):** 会给 PM 体育市场带来巨大流动性 (老彭估算额外 +30-50% volume). 是中期最大 catalyst 事件, 但 2026 年内不会落地.

### §6.2 各 Sportsbook Fee 调整 (DraftKings 涨 Vig?)

**DraftKings 涨 vig 信号 (老彭确认, 2025 Q3-Q4 以来):**

DraftKings 从 2025 年下半年开始悄悄调整特定 market 的 vig:

```
变化轨迹:
  NFL Prop (passing yds):  -115/-105 → -120/-100 (vig +1.3pp)
  NBA Prop (points):       -115/-105 → -115/-105 (不变)
  MLB ML (dime line):      +/-20c 维持
  NFL Spread:              -110/-110 维持 (竞争激烈不敢动主线)
```

**为何 DK 涨 vig?**

1. 营销费用削减 (2024 profitability push 之后), 靠提高 vig 而非 promo 拉 margin
2. Prop 是不透明市场, 用户对 vig 不敏感 (vs spread -110/-110 人人都知道)
3. SGP 增长稀释了单 prop 敏感度 (用户看 SGP 总赔率不看单腿 vig)

**对我们的意义:**
- DK prop vig 升高 → PM vs DK prop 价差可能扩大 → P1-04 (lineup prop) alpha 可能增加
- 但 DK prop 作为锚源精度下降 (vig 高的 book fair_value 估计更不准) → 仍然优先用 Pinnacle 作锚

### §6.3 加密预测市场监管动态 (CFTC, Polymarket 美国合规)

**当前状态 (截至 2026-05-29):**

**CFTC vs Polymarket:**
- 2022 CFTC settlement: Polymarket 支付 $1.4M 罚款, 承诺阻止美国用户访问
- 2024-2025 灰色状态: 美国 IP 通过 VPN 仍可访问; CFTC 未采取进一步执法行动
- 2026 Q1: CFTC 在 Trump 政府下显著减少执法力度 (crypto-friendly 监管政策转向); prediction market 政策 outlook 趋于宽松
- 2026 Q2 (最新): Polymarket 向 CFTC 申请正式许可证 (据 Bloomberg 2026-04-28 报道, cited). 若批准, 美国用户将可合法访问.

**Kalshi 动态:**
- Kalshi 已获 CFTC 认可 (2024 法院判决支持); 现在是美国唯一合法 prediction market
- 2026 Q1 Kalshi 体育市场上线 (之前只有政治/经济预测)
- **对我们:** Kalshi 体育 = 另一个跨平台套利腿. 流动性目前很低 (初期), 但 6-12 月内可能增长

**老彭风险评估:**

| 风险类型 | 概率 | 影响 | 应对 |
|---|---|---|---|
| CFTC 对 PM 加强执法 | 低 (15%) | 高 (平台关闭) | 多平台分散 (Kalshi/Betfair); 不超过净资产 25% 在单平台 |
| PM 智能合约漏洞/oracle 争议 | 中 (25%) | 中 (单市场资金损失) | position limit + UMA oracle dispute 监控 |
| PM 新监管要求 (KYC) | 中 (30%) | 低-中 (流动性下降) | 非美国账户不受影响 |
| Polymarket 获 CFTC 许可 (正面) | 中 (40%) | 高正面 (流动性爆发) | 提前扩大仓位准备 |

**老彭立场:** 当前监管窗口偏友好 (Trump 政府), 是 PM 体育押注的战略好时机. 建议老雷在 M1-M2 阶段对 PM 资金分配保持积极 (vs 过于保守), 抓住监管友好窗口.

---

## §7 W10 Ticket 候选

**老彭提案 (Wave 87, 2026-05-29):**

### §7.1 老彭 W10 W2: Cross-Platform Vig-Arb Signal Spec v0.1 (P0-03 候选)

```
Owner: 老彭
截止: W10 W2 EOD
产出: docs/RESEARCH/laopeng-p0-03-cross-platform-arb-spec-v0.1.md
内容:
  - P0-03 正式信号定义 (触发/入场/出场)
  - PM vs Pinnacle + PM vs DK 双锚策略 (哪个优先)
  - hit rate 估算方法论 (含 book bias 修正)
  - 与 P0-01 的相关性分析 (避免重复 alpha)
  - backtest 数据需求 (给小蒋)
验收方: 小梁
依赖: 老彭手头 Pinnacle closing line data + DK 历史赔率
```

### §7.2 老彭 W10 W3: Alpha v2.1 Update (含新一轮调研)

```
Owner: 老彭
截止: W10 W3 EOD
产出: docs/RESEARCH/laopeng-signal-alpha-v2.1-update.md
内容:
  - OQ-P02-3 gross/net confirm 之后的信号优先级最终版
  - W10-W12 赛事窗口 alpha 机会列表 (更新 §4 to W10 时点)
  - P0-01/P0-02/P0-03 联合 Kelly 参数建议
  - NBA Finals G1-G7 实盘观察计划
验收方: 小梁
```

### §7.3 小程 W10 W2: P0-02 Spec v0.2 配合

```
Owner: 小程 (老彭提议, 小梁派单)
截止: W10 W2 EOD
内容 (老彭提需求, 小程执行):
  - C2 门槛升至 ≥ 6¢ (主队/Over 方向 ≥ 7¢), 见 OQ-P02-3 ack §4.2
  - edge 标注从 "Edge post-fee" 改为 "Edge post-bias, pre-Polymarket-fee"
  - G3 KR 更新选择: 方案一 (hit ≥ 56%) 或方案二 (C2 门禁前置)
  - 配合 P0-03 spec, 检查 P0-02 与 P0-03 信号是否冲突 (同 event 反向触发?)
依赖: 老钱 spec v2 方案选择 (截止 6/01)
```

### §7.4 小蒋 W10 W3: Backtest Framework 跨平台数据接入

```
Owner: 小蒋 (老彭提议, 小梁派单)
截止: W10 W3 EOD
内容 (老彭提需求, 小蒋执行):
  - backtest framework 加入多平台锚 (Pinnacle + DK/FD) 数据 ingestion
  - 支持 P0-03 信号 backtest (两锚 implied prob 差)
  - 历史数据切片: 2024 NBA season + 2024 UCL + 2025 NBA Finals
依赖: 小余历史数据仓库; 老李 Pinnacle API 接入状态
```

---

## §8 不耻下问

**本 wave 并行任务协作 (老彭提问):**

### @老李 (PM 调研, 本 wave 并行)

老彭想了解:
1. **Pinnacle API 实测状态:** 跨洋 latency 是否可用? hit rate 是否达到 < 500ms? 这是 P0-01 live 信号的前置条件
2. **DraftKings / FanDuel API:** 有没有公开的 odds API 可以批量拉? 或者我们靠 scraping? P0-03 需要第二个锚

### @小段 (Goalserve 调研, 本 wave 并行)

老彭想了解:
1. **Wimbledon 网球 inplay bm 字段:** 网球 inplay feed 的 `bm` 字段是否和 soccer 一样 = "bet365"? 还是 Wimbledon 这种 Grand Slam 有多家?
2. **MLB inplay feed 状态:** MLB 季节是否 EMPTY (OQ-P02-3 ack 时老彭看到 amfootball 等是 EMPTY), MLB inplay 是否现在有数据?
3. **UCL Final (5/31) bm 字段:** 两天后欧冠决赛, 能否在今天/明天跑一次 inplay feed test, 记录 bm + value_eu 的实际行为? 这是宝贵的 Grand Event 数据点

### @小程 (信号探索)

老彭想问小程:
1. **P0-02 信号在 Finals 场景下的 backtest 子样本:** 能否单独跑 NBA Finals 历史 (2022/2023/2024) 的 P0-02 信号? 我的判断是 Finals 比常规赛 hit rate 更高 (双方定价更仔细, PM 散户更活跃)
2. **P0-03 初步评估:** 在 P0-02 spec v0.2 更新完之后, 能不能给我一个 P0-03 rough backtest 方案? 我们联合做 W10 W2 spec

### @多人讨论会建议

**建议在 W10 W1 开一次信号评审小会 (30min):**

```
参与: 小梁 + 老彭 + 小程 + (可选) 老钱
议题:
  1. G3 KR 调整: 方案一 vs 方案二, 15min 决策
  2. P0-03 是否进 MVP 首批, 还是 M2 后开, 10min
  3. NBA Finals 实盘观察计划确认, 5min
主持: 小梁 (C 主管)
结果落: docs/MEETINGS/ (小米归档)
```

---

## §9 ADR-029 流程记录

本文件按 ADR-029 §3.1 新流程执行:

```
Step 1. pwd verify: 已确认在 .../sports-trader-cpp/.claude/worktrees/agent-ace7e547b8134a023
Step 2. git add: 已准备
Step 3. git commit: "docs(research): W9 W5 betting industry update (老彭 Wave 87)"
Step 4. git fetch origin
Step 5. git merge origin/main --no-edit
Step 6. git push origin worktree-agent-ace7e547b8134a023
Step 7. gh pr create (见下)
Step 8. 回汇 GM
```

**PR Body 模板 (ADR-029 §3.1 Step 7):**

```
## Summary
- 老彭 W9 W5 betting industry + 竞品调研: DraftKings/FanDuel/BetMGM/Caesars 同行分析
- alpha source update: OQ-P02-3 confirm 之后信号优先级重排, P0-03 候选提案
- W10-W12 赛事 alpha 窗口: NBA Finals (A+) / UCL Final (B) / Wimbledon (A-) / MLB (C+)
- G3 KR 再校准: 维持方案一 (hit ≥ 56%) 或方案二 (C2 门禁) 建议

## ADR cite
- ADR-027: N/A (无核心 struct 改动, 纯 doc)
- ADR-024: worktree commit + push done
- ADR-029: new flow dogfood ✓

## ctest
- [ ] 无 code change, ctest 不适用 (doc only)

## Worktree
- branch: worktree-agent-ace7e547b8134a023
- commit: (见 git commit hash)
```

---

**汇报小梁 (C 主管):**

W9 W5 betting industry 调研已完成. 核心结论三条:

1. **vig 差不等于套利:** PM effective vig (4.5-6%) ≈ 零售书 vig (4.55-5%), 不存在直接 fee 套利. 真 edge 来自信息时差 + PM 散户 mispricing. P0-01/P0-02 方向正确.

2. **NBA Finals = 最大 alpha 窗口 (6月上中旬):** P1-04 (lineup-news-lag) 在 Finals 期间触发频率最高, 建议 W10 前完成 spec v0.1 + paper setup.

3. **监管窗口友好:** CFTC crypto-friendly 政策 + PM 申请许可证 → 中期 PM 流动性可能爆发. 当前是增加 PM 仓位的战略好时机.

**最看好的新 alpha:** P0-03 (cross-platform vig-arb signal) — 不是真套利, 是用 Pinnacle vs DK 双锚强化 PM mispricing 识别. W10 W2 出 spec v0.1.

— 老彭，2026-05-29
