# Goalserve bet365 赔率源覆盖管线梳理 + 逐盘掉点量化

> owner: 老雷 (GM)
> last_review: 2026-06-13
> 性质: 只读研究 (live PM gamma + GS inplay feed 直采 join), 不碰交易/不改参数/不重启服务
> 数据快照: 2026-06-12 02:20 UTC (paper_server 已停, 用 log 历史 + 直采 GS/PM 双侧 join)

---

## 0. TL;DR (先给结论)

- **匹配逻辑不是瓶颈。** 逐盘 join 实证: PM 当前所有 head-to-head 盘里, 「GS 有这场比赛但队名对不上」(B 类·可救) 实测 **几乎为 0** —— 唯一真名字缺口是 **「Korea Republic」(PM) ↔「South Korea」(GS)** 一类国家别名。
- **两大掉点的真根因都是「源覆盖 / 时间窗」, 不是 matcher:**
  1. **映射命中 32.7%** ← 分母 (PM 体育池 ~600 event) 里大量是 **futures / pregame / 当下不在赛季时段** 的盘 (南美足球 bra2/bol1/chi1/mar1 = 106 盘当下 live=0; fifwc 121 盘多为小组赛未开打)。GS in-play feed 任一瞬间只装「此刻真在打」的 ~50 场。两者天然错配, 不是 bug。
  2. **匹配了但 27% 无 bet365 in-play 赔率** ← bet365 对**某些联赛/项目结构性不挂 in-play 盘** (低级别 ITF、部分电竞), + **完赛盘 feed 仍挂冻结赔率被算进匹配分母**。
- **结构性天花板 (救不了, 别投力气):** 表格网球 (WTT, 38 盘) / 板球 in-play 赔率 (33 盘) / 电竞 in-play 赔率 (43 盘) —— GS 根本没有对应 bet365 in-play feed (实测 404 / 空 events / scores-only)。
- **唯一立即可做且零风险的可救项:** 加国家别名 (Korea Republic=South Korea 等 ~10 条), 预计赛时多覆盖个位数足球国家队盘 (World Cup / 国家队赛事窗口)。

---

## 1. 管线数据流图 (代码层逐环 + 每环掉点条件)

```
[Goalserve inplay.goalserve.com]  (http 明文, key 走 GOALSERVE_PROXY)
  GET /inplay-<slug>.gz  per-sport, 1005ms/轮 (贴 ~1 req/s/sport 限速)
  slug ∈ {soccer,basket,tennis,esports,hockey,baseball,amfootball}   ← trader_daemon.cpp:1098 feed_cfg.sports
        │  掉点①: 只订 7 个 slug。tabletennis(404)/cricket(404)/volleyball(未订) 无 inplay feed
        ▼
[inplay_score_parser.cpp ParseInplayScores]
  解析 events{} → EventScore{home,away,league,score,period,ts, inplay_bet365_*_fair}
  bet365 odds: 从同 event_block 的 "odds":{} 切 → ParseInplayOddsDevigResult (单源 de-vig 赛果 fair)
        │  掉点②: event 无 "odds" 节点 / 选不到赛果盘 → inplay_bet365_*_fair = -1.0 (无 sharp)
        │  掉点③: 完赛盘 (core.finished=1) feed 仍带冻结赔率 → 仍算"有 odds" (下游 kSharpFreshNs 90s 兜)
        ▼
[ScoreSnapshotStore]  merged_map_ (inplay_match_id → EventScore)
  + InjectSupplementalScores("tennis_scores"/"cricket"/"esports"/"baseball")  ← www getfeed, 补候选池
        │  注: 补充源【无 bet365 odds】(scores-only) → 只补匹配候选 + 比分特征, 不补 sharp fair
        ▼
[market_discovery.cpp]  gamma /events?tag_id=1&closed=false&active=true (分页, ascending=false)
  → PM 体育 event 池 (~600), 每 event 拆 condition_id + team0/team1 (title 解析) + sport.sport 码
        │  掉点④: 分母含大量 futures/pregame/赛季外盘 (当下无 GS live 对应)
        ▼
[event_matcher.cpp EventMatcher::Match]  condition ↔ inplay_match_id
  PairSim 按运动分派: 团队→CanonicalTeam(别名表) 精确比 / 个人(网球)→姓锚 / 未知→token overlap
  门: team_min ≥ 0.6  +  orientation fail-closed (direct/cross 都过且接近 → 拒)  +  kickoff 窗
        │  掉点⑤(可救面): 队名/国名规范化失败 → overlap<0.6 → no_match。**实测此处掉点≈0**
        ▼
[trading_loop.cpp ResolveGameContext] (2026-06-12 重构)
  condition→map→score_store Get(inplay_match_id) → fresh 检查 (≤score_staleness_limit) → in-play 检查
  填 game_row: 比分(YES-canonical 翻转)/时钟/period/inplay_bet365_*_fair/bm_slots/live_stats
        │  掉点⑥: stale(已修, 实测 stale=0) / 非 in-play / src_home<0 (无 sharp)
        ▼
[ResolveFair → sharp 决策]   has_real_fair=true 且 src_home≥0 → 走 bet365 de-vig fair
```

**实测 score-flow 漏斗 (paper_server.log 末行, calls=618k 累计):**

| 环 | 计数 | 占上一环 | 掉点归因 |
|---|---|---|---|
| 决策调用 | 618,000 | — | (per-tick × 全订阅 token × 数小时累计) |
| 映射命中 mapped | 201,050 | 32.5% | 掉点④ 分母含 futures/pregame/赛季外 + 掉点⑤(≈0) |
| 比分找到 score_found | 200,478 | 99.7% | 映射上基本都有比分 |
| in-play | 200,478 | 100% | — |
| 新鲜 (stale=0) | 200,478 | 100% | 掉点⑥staleness 已修 |
| **has_real_fair** | **200,478** | — | — |
| **其中有 sharp** | **145,430** | **72.5%** | 掉点②③ bet365 无 in-play 赔率 / 冻结盘 |

---

## 2. 三类掉点量化表 (逐盘 join, 2026-06-12 02:20 UTC 快照)

**方法:** 直采 PM gamma 体育池 600 event → 抽 head-to-head 盘 555 个; 直采 GS 7 个 inplay-*.gz → 解析 50 场 live (49 带 bet365 odds); 复刻 matcher (FoldDiacritics+token overlap+姓锚, 阈值 0.6) 做 join。

| 类 | 定义 | 实测盘数 | 占比 | 可救? |
|---|---|---|---|---|
| **MATCHED** | 匹配上 + 有 bet365 in-play 赔率 (真可交易) | **11** | 2.0% | 已覆盖 |
| **C: matched-no-odds** | 匹配上但 GS 该场无 bet365 in-play 赔率 | 0* | — | 难救 (天花板) |
| **B: 近失 (savable)** | best∈[0.3,0.6), GS 有相似场但名字对不上 | **5** | 0.9% | **部分可救** |
| **A1: GS 同运动在打但非此场** | best<0.3, GS 当下有该运动 live 但不是这场 (pregame/futures) | 384 | 69% | 不可救 (时间窗) |
| **A2: GS 该运动当下无 live** | best<0.3, GS 此刻不carry该运动 (赛季外/无feed) | 155 | 28% | 不可救 (时间窗/无源) |

\* C 类此快照=0 仅因 GS 当下 live 的 50 场 49 场都带 odds; 但 score-flow 27.5% 的「matched-no-sharp」是**跨时段累积**真实存在 (完赛冻结盘 + 低级别赛事), 见 §4。

**B/A 的决定性验证:** 对 384 个「GS 同运动在打但没匹配上」, 检查 PM 两队 token 是否**出现在 GS live feed 任何位置**:
- 仅 **5 个** 两队 token 都在 GS feed 里 → 且全是 World Cup 期货排列 (Czechia/Mexico、South Africa/Korea Republic, 当下只 KOR-CZE 真 live, 其余小组赛未开打)。
- **其余 379 个**: PM 队名在 GS live feed 里**根本不存在** → 证明 GS 当下没在打这场 (pregame/futures), 不是名字 bug。

**结论: matcher 名字规范化在当前样本掉点 ≈ 0。** 33% 命中率的 67% 缺口 = 分母里 futures/pregame/赛季外盘 (PM 体育池天然比任一瞬间的 in-play 集大一个数量级)。

---

## 3. 可救回方案 (按 ROI 排序)

### ROI-1 ⭐ 国家/国家队别名表 (立即可做, 零风险, 唯一真名字缺口)
- **依据:** 实测唯一真名字不匹配 = PM「Korea Republic」↔ GS「South Korea」(match-diag/near-miss 实证 score=0.50)。国家队赛事 (World Cup / 欧国联 / 美洲杯) PM 常用 FIFA 全称, GS 用通称。
- **改法:** team_alias.cpp 加一张 `NationalTeams()` 表 (kTeam 分派下, IsSoccerCode 已涵盖 fifwc): Korea Republic=South Korea, IR Iran=Iran, Côte d'Ivoire=Ivory Coast, USA=United States, China PR=China, Czechia=Czech Republic 等 ~15 条。
- **预期多覆盖:** 国家队赛事窗口个位数盘/场 (World Cup 期间 1-4 场/时段)。**非常态高频** (国家队赛事是间歇期), 但实现成本极低 (纯加性表, 不退化), ROI 正。
- **成本:** ~30 行表 + 重编译。无架构评审 (纯加性, §8.1 carve-out)。

### ROI-2 完赛冻结盘排除出「matched」分母 (观测纠偏, 已部分修)
- **依据:** 掉点③ —— 完赛盘 feed 仍挂冻结 bet365 赔率, 被算进 matched 但 sharp 已无意义。trader_daemon.cpp 已有 `is_final` 即时退订 + `odds_fresh` (kSharpFreshNs 90s) 兜底。
- **现状:** 27.5% 「matched-no-sharp」里**部分**是这类。已有机制, 但 90s 新鲜窗内冻结盘仍短暂计入。
- **预期:** 不增覆盖 (这些本就该判死), 但让 32.7%/72.5% 口径更干净, 避免误判「匹配了为何无 sharp」。**低优先, 偏观测质量。**

### ROI-3 (条件性) 南美足球/低级别赛事 = 时间窗问题, 无需改代码
- **依据:** bra2/bol1/chi1/mar1 = 106 盘当下 live=0, 因 02:20 UTC 这些联赛不在赛时。GS soccer feed **确实 carry** 这些联赛 (实测 USL League Two 在打)。
- **预期:** 这些盘在**当地赛时窗口** (南美晚间 = UTC 23:00-04:00 前后) 会自动 live + 自动匹配 (队名靠 SoccerTeams 表已覆盖大部分; 南美二级队可能需补表)。**无需改 matcher 逻辑, 只是当下快照时段不对。**
- **行动:** 在南美赛时窗口重采一次 join 验证南美队名命中率, 若 <90% 再针对性补 SoccerTeams 二级队 (bra2/bol1 具体俱乐部)。

### ROI-4 (低值大工程, 不建议) 补 SoccerTeams 二级联赛俱乐部
- bra2 (巴乙)/bol1 (玻利维亚)/chi1 (智利)/mar1 (摩洛哥) 俱乐部多数不在现 SoccerTeams 表 → 回退 token overlap (姓氏/城市名通常仍能 0.6 命中, 但缩写/绰号会漏)。
- **依据:** team_alias.cpp 注释自承「足球/板球等全球联盟队数上千暂回退通用」。
- **预期:** 边际覆盖, 但需逐联赛真实样本建表 (数据组专项)。**ROI 低, 排在 ROI-1/3 之后。**

---

## 4. bet365 天花板清单 (结构性救不了, 别投力气)

| PM 项目/联赛 | 盘数 | GS in-play 赔率源状态 (实测) | 判定 |
|---|---|---|---|
| **WTT 表格网球** (wttwom/wttmen) | 38 | `inplay-tabletennis.gz` = **404**; www tabletennis/home = HTML 错误页 | **无源, 永不可交易 in-play** |
| **板球 in-play** (crint) | 33 | `inplay-cricket.gz` = **404**; www cricket/livescore = 251KB 但 **无 bet365 无 odds** (scores-only) | **有比分无赔率** (只能 score-prior, 非 sharp) |
| **电竞 in-play** (es2/lol/dota2/codmw/r6siege/ow/mlbb) | 43 | `inplay-esports.gz` = **空 events{}**; www esports/home = Finished + 无 odds | **bet365 不挂电竞 in-play / 当下无 live** |
| **低级别 ITF 部分场** | 部分 64 | inplay-tennis.gz 只覆盖有 bet365 盘的赛事; M15/W15 部分场无 in-play 盘 | **bet365 选择性挂盘** |
| **PM 独有期货/Outright** | (fifwc 多数) | 单边/Outright 市场, 无对应单场 in-play | **PM 独有市场, 非掉点** |

**关键: 不要把以上算进「可改进项」。** WTT/电竞/板球 in-play 赔率合计 ~114 PM 盘, 是 GS bet365 产品边界, 不是我们管线缺陷。补充源 (cricket/esports/tennis_scores www) 已接, 但它们**只有比分没有赔率**, 作用是补匹配候选 + 比分特征, 不产 sharp fair。

---

## 5. 立即可做 vs 需开发

| 项 | 类型 | 工时 | 风险 | 预期 |
|---|---|---|---|---|
| **ROI-1 国家别名表** | 立即可做 | <1h | 零 (纯加性) | 国家队赛事窗口 +个位数盘 |
| ROI-2 冻结盘口径纠偏 | 立即可做 (观测) | <1h | 零 | 口径干净, 不增覆盖 |
| ROI-3 南美赛时重采验证 | 立即可做 (只读) | <0.5h | 零 | 确认时间窗假设 |
| ROI-4 二级足球俱乐部表 | 需开发 (数据组专项) | 数天/联赛 | 低 | 边际, 待样本 |
| 表格网球/板球/电竞 in-play | **不做** | — | — | 无源, 天花板 |

---

## 6. 数据来源 (可复现)

- PM: `gamma-api.polymarket.com/events?tag_id=1&closed=false&active=true&limit=100&order=startDate&ascending=false&offset={0..500}` (600 event)
- GS: `http://inplay.goalserve.com/inplay-{soccer,basket,tennis,esports,hockey,baseball,amfootball}.gz` (走 GOALSERVE_PROXY); www getfeed 补充源
- 漏斗: `/tmp/paper_server.log` score-flow / cov-diag / map-diag / map-unmatched / match-diag 行
- join 脚本: 服务器 `/tmp/joinc.py` (复刻 event_matcher.cpp 的 fold+overlap+姓锚, 阈值 0.6)
- 端点 (服务跑时): `GET /api/v1/mapping/status` → MappingStatusReport{no_match, matched_with_sharp, matched_no_sharp, no_sharp[], games[]} 是同口径金矿 (state_provider.hpp:605)
