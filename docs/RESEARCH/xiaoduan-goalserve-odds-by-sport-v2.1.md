# Sport × Odds-in-Base-Feed 精细矩阵 v2.1

- Owner: 小段
- Date: 2026-05-28
- Status: Active (v2 主报告补充, P0-01 信号 fair value 锚)
- 验收人: 老雷 + 小梁 (信号 fair value), 小余 ETL
- 关联:
  - `xiaoduan-goalserve-endpoint-matrix-v2.md` (v2 主报告, 219 探针)
  - `2026-05-28-gm-policy-api-monitoring-longterm.md` (sweep 政策)
  - GM ADR `defer-onchain` (MVP 不依赖 getodds endpoint)
- 原始数据: `/tmp/xiaoduan_v21/raw/` (47 文件), `/tmp/xiaoduan_v21/analysis.json`

---

## 0. 用户原话 + 实证目标

GM 老雷原话: "inplay 看运动, 有的运动项目有赔率, 有的没有".

v2 主报告结论是 "所有 odds endpoint 0 字节 (无授权)", 但 GM 提醒 **base feed (inplay/scores/livescore/home) 本身可能内嵌 odds value**, 不必走 getodds 升级. v2.1 用我们当下 key (无 odds plan), 实证 sport × base endpoint × 三态:

- CONTAINS_VALUE: 含实际 odds 数值字段 (decimal 1.01-50)
- ID_ONLY: 仅含 `@oddsid` 引用 (要走 getodds 才解, 我们无权限)
- NO_ODDS: 完全无 odds 字段

实证窗口: 2026-05-28, 法网/IPL/PGA/MLB/WNBA 在赛, NBA/NHL/NFL/五大联赛 off-season.

---

## 1. 矩阵 (sport × endpoint × 三态)

| Sport | Endpoint | Bytes | 三态 | 证据 (字段) |
|---|---|---|---|---|
| **UK Horse Racing** | `racing/uk?json=1` | 1.5MB | **CONTAINS_VALUE** | `odd="2.1"` x 8028 + `odd_id` + 18 bookmaker (Bet365/W.Hill/Coral/Ladbrokes/BetMGM/...) |
| MLB | `baseball/usa` (XML + JSON) | 161-175 KB | **ID_ONLY** | `@oddsid="352728"` x 15 (一场比赛一 oddsid), 无 value |
| MLB | `baseball/home?json=1` | 35 KB | NO_ODDS | 无任何 odd* key |
| MLB | `baseball/inplay` | 82 B | EMPTY | 季外/无 inplay 比赛 |
| Soccer | `soccer/home` (XML, ?json=1 被忽略) | 74 KB | NO_ODDS | 仅 `us="16:00"` (时区时间, false positive) |
| Soccer | `soccernew/home?json=1` | 52 KB | NO_ODDS | 同上 |
| Soccer | `soccer/d-1?json=1` | 455 KB | NO_ODDS | 历史比分, 无 odds |
| Soccer | `soccer/worldcup?json=1` | 341 KB | NO_ODDS | qualification fixture, 无 odds |
| Soccer | `soccer/inplay` (XML/JSON) | 276 B | EMPTY | 当下无在赛比赛 |
| Soccer | `soccernew/leagues?json=1` | 51 KB | NO_ODDS | league directory master |
| Tennis (法网) | `tennis/home` (XML/JSON 同 size) | 90 KB | NO_ODDS | 仅 `us="17:00"` 时区, set 比分 `set1="6.3"` 是局比分, 无 odds |
| Tennis | `tennis/atp?json=1` | 16 KB | NO_ODDS | ATP 排名, 无 odds |
| Cricket (IPL) | `cricket/livescore` (XML/JSON) | 280 KB | NO_ODDS | overs/economy_rate 是统计, 无 odds |
| Cricket | `cricket/home?json=1` | 1.7 KB | NO_ODDS | |
| Golf (PGA) | `golf/pga` (XML/JSON) | 8.5 KB | NO_ODDS | 仅 `winner="Jon Rahm"` (post-event), 无 pre-event odds |
| F1 | `f1/drivers?json=1` | 2.8 KB | NO_ODDS | driver listing, 无 odds |
| F1 | `f1/home` | 50 B | EMPTY | 间歇期空壳 |
| MMA / UFC | `mma/schedule` (XML/JSON, **双 c**) | 72-80 KB | NO_ODDS | 仅 `winner="True/False"` (post-event), "Niko Price"/"Bet Wright" 是 false positive |
| MMA / UFC | `ufc/schedule?json=1` | 571 B | NO_ODDS | |
| Esports | `esports/home` (XML/JSON) | 31 KB | NO_ODDS | match 列表 (CS GO), 无 odds, 无 bookmaker |
| Esports | `esoccer/inplay?json=1` | 569 B | NO_ODDS | |
| WNBA | `bsktbl/wnba-scores` (XML/JSON) | 53-59 KB | NO_ODDS | `odd_name="Breanna Stewart"` 是球员官方全名 (false positive), 无 odds |
| WNBA | `bsktbl/wnba-shedule?json=1` | 188 KB | NO_ODDS | schedule, 无 odds |
| NPB (日本) | `baseball/japan?json=1` | 52 KB | NO_ODDS | |
| KBO (韩国) | `baseball/korea?json=1` | 29 KB | NO_ODDS | |
| Hockey | `hockey/home?json=1` | 589 B | NO_ODDS | small (季外) |
| Hockey | `hockey/nhl-scores?json=1` | 23 KB | NO_ODDS | 上季余韵, 仅 stats |
| Hockey | `hockey/nhl-shedule?json=1` | 3.6 MB | NO_ODDS | 新季 schedule, 巨型, 无 odds |
| Basketball | `bsktbl/home?json=1` | 3.8 KB | NO_ODDS | |
| Basketball | `bsktbl/d-1?json=1` | 24 KB | NO_ODDS | 前一天 |
| Basketball | `bsktbl/nba-shedule?json=1` | 877 KB | NO_ODDS | 新季 schedule, 无 odds |
| Football | `football/nfl-scores?json=1` | 24 KB | NO_ODDS | 上季余韵 |
| Rugby | `rugby/home?json=1` | 1.1 KB | NO_ODDS | |
| Handball | `handball/home?json=1` | 3.2 KB | NO_ODDS | |
| Volleyball | `volleyball/home?json=1` | 37 B | EMPTY | |
| Snooker | `snooker/home?json=1` | 7.8 KB | NO_ODDS | |
| Darts | `darts/home?json=1` | 8 KB | NO_ODDS | |
| Badminton | `badminton/home?json=1` | 11 KB | NO_ODDS | |
| Table Tennis | `tabletennis/home?json=1` | 567 B | NO_ODDS | |
| Boxing | `boxing/home` | 75 B | EMPTY | |

**汇总:**

- **CONTAINS_VALUE**: 1 sport (UK Horse Racing)
- **ID_ONLY**: 1 sport (MLB)
- **NO_ODDS**: 18 sport (含在赛: WNBA / Soccer / Tennis / Cricket / Golf / MMA / Esports / NPB / KBO / NFL余韵 / NHL余韵 / NBA-shedule / Handball / Snooker / Darts / Badminton / TableTennis / Rugby)
- **EMPTY (季外/无在赛)**: 5 endpoint (Boxing/Volleyball/F1-home/MLB-inplay/Soccer-inplay)

---

## 2. CONTAINS_VALUE 详情: UK Horse Racing 唯一案例

`racing/uk?json=1` (1.5 MB, 2026-05-28 抓样):

- 7 tournaments (Ripon / Beverley / ...) x 47 races x 446 horses x 18 bookmakers = **8028 odds entries**
- 字段路径:
  ```
  scores.tournament[].race[].odds.horse[].bookmakers.bookmaker[].{name, odd, odd_id, eachWay, bookmaker_id}
  ```
- 样本片段 (一匹马的多家报价):
  ```json
  {
    "name": "Bet 365",        "odd": "2",    "odd_id": "1144608380052"
    "name": "William Hill",   "odd": "2.1",  "odd_id": "114460838005215"
    "name": "Coral",          "odd": "2",    "odd_id": "11446083800525"
    "name": "Ladbrokes",      "odd": "2",    "odd_id": "..."
    "name": "BetMGM",         "odd": "...",  "odd_id": "..."
  }
  ```
- 18 bookmakers 全覆盖 (每 horse 18 entries):
  Bet 365 / William Hill / Coral / Betfred / Boylesports / Ladbrokes / Unibet / BetTom / BetVictor / 10Bet / BetMGM / GrosvenorSports / Virgin Bet / talkSPORT BET / BetWright / LiveScore Bet / Betano / Dragon Bet
- 特殊值: `odd="SP"` 代表 Starting Price (现场起跑价, 尚未定数值), 数量约 5% , ETL 必须显式处理.

---

## 3. ID_ONLY 详情: MLB

`baseball/usa` (XML 和 JSON 行为一致, 仅前缀符号不同):

- XML: `<match ... oddsid="352728" .../>` x 15 (今日 15 场比赛各 1 个 oddsid)
- JSON: `"@oddsid": "352728"` (XML attribute 转 JSON 加 `@` 前缀)
- **关键**: oddsid 是 odds feed 的外键, 必须通过 `getodds/baseball?oddsid=352728` 解析才能拿数值. 当前 key 无 getodds 权限.
- 空值: 14/15 有数字 oddsid, 1 个 `oddsid=""` (无关联 odds feed, 多半是延后比赛)

**ID_ONLY 意味着**: MLB 比赛存在 odds reference, 但 value 在另一 endpoint, 升级 getodds plan 后才能拉. 不升级 = NO_ODDS.

---

## 4. NO_ODDS 详情 + false positive 清单

实证排除的伪命中 (避免下次自动扫描误报):

| 字段 | 出现 endpoint | 实际含义 |
|---|---|---|
| `us="16:00"` (XML attr) | soccer/home, tennis/home | "US time" 时区时间, 不是 American odds |
| `odd_name="Breanna Stewart"` | bsktbl/wnba-scores | 球员 official display name (注意拼写 "Niko Price" 等) |
| `winner="True/False"` | mma/schedule, golf/pga | post-event boolean / winner name, 不是 pre-event odds |
| `set1="6.3"` | tennis/home | 局比分 (Roland Garros 比赛 set 1 = 6-3), 不是 odds |
| `overs="2.1"`, `er="4.25"` | cricket/livescore | overs (击球轮次) + economy rate, 不是 odds |
| `earned_runs_average="3.13"` | mlb/usa | 投手防御率, 不是 odds |
| `faceoffs_pct="33.3"` | nhl-scores | 球面争球胜率, 不是 odds |
| `yards_per_play="4.7"` | nfl-scores | per-play 推进码数, 不是 odds |

NO_ODDS sport 列表的共性: 都是 Goalserve "core data plan" 范围 — score/stats/lineup/schedule, **不包含 sportsbook odds**. 这与 UK Horse Racing 的根本区别在于: 赛马 odds 是 UK racing 行业标准数据 (PA/Press Association 公共订阅源), Goalserve 默认含; 而其他 sport 的 odds 是 sportsbook 商业数据, 单独 plan.

---

## 5. 对 P0-01 信号的影响 (Goalserve 直接可锚 vs 必须靠 Polymarket)

| Sport | Goalserve 直接 odds | 给 P0-01 的可用性 |
|---|---|---|
| UK Horse Racing | YES (18 books, 8028 entries) | **可直接做 fair value 锚** (取 Pinnacle 等价的 sharp book, 但当前列表里 Pinnacle 缺失, 看 Bet365 + Ladbrokes 多家均值) |
| MLB / NBA / NFL / NHL / Soccer (五大) | NO (ID_ONLY 或 NO_ODDS) | 必须靠 Polymarket clob best bid/ask 自身做 fair value, Goalserve 只贡献 score/stats |
| Tennis / Cricket / Golf / MMA / Esports / WNBA / NPB / KBO / F1 | NO | 同上 |

**对 P0-01 信号方案直接影响:**

1. UK Horse Racing 是当下唯一**不需要 Polymarket clob 就能算 fair value** 的盘口. 如果 Polymarket 上线 UK Racing market (例如 Royal Ascot), 信号侧可拿 18 家 book 中位数对 Polymarket mid 做 vig-free spread, 偏离即触发. **建议小梁优先做这条单源信号.**
2. 其他 19 个 sport, fair value 必须从 Polymarket clob 自身推 (best bid + best ask 加权 + 时间衰减), Goalserve 仅做 "事件锚定" (score / period / clock / lineup change), 提供事件触发, 不提供价格锚.
3. MLB 的 oddsid 有结构性升级潜力 — 一旦 Goalserve plan 升级, MLB 立即变 CONTAINS_VALUE (15 场比赛 oddsid 列表已经在结构里), ETL 改动很小. 这是 MVP 之后的 quick win.

---

## 6. 给小梁 + 老彭的建议 (信号优先级)

### 给小梁 (信号 fair value)
1. **第一个 P0-01 实证目标: UK Horse Racing**. 当下 1.5MB / refresh 5min, ETL 单 endpoint 拿全数据, 18 books 中位数 vs Polymarket mid 偏离监控. 不依赖任何升级.
2. 其他 sport (MLB / NBA / NFL / 五大 / Tennis / Cricket / Golf / MMA) 用 **Polymarket clob 内部锚** (best bid/ask 加权), Goalserve 只供事件流 (touchdown / wicket / set 结束) 做事件驱动二次触发.
3. **不要假设 MLB / Soccer 有 base-feed odds**. v2 报告 + v2.1 都实证无, 不要在信号设计里 fallback 到 "从 base feed 拉 odds".
4. F1 / MMA / Esports / Golf outright 市场 (Polymarket 有这些), 短期无 Goalserve odds 锚, 信号侧只能跟 Polymarket clob 自己做.

### 给老彭 (betting industry sport 优先级)
1. UK Horse Racing 是 Polymarket 体育矩阵里**唯一能用 Goalserve 直接做 cross-source arbitrage signal** 的 sport. 如果 Polymarket 这块流动性可观, 优先做.
2. 其他 sport 需要先升级 Goalserve odds plan 才能多源 (升级清单见 v2 主报告 §10.2). 当前 MVP 阶段不投入.
3. MLB oddsid 结构已就位, 升级 ROI 最高 (29 场/天 x 15+ 市场 = 巨量盘口).

---

## 7. 开放问题

1. **UK Racing 的 bookmaker 列表里没有 Pinnacle / Betfair Exchange**, 都是 UK retail books (普遍含 margin 5-12%). 老彭判断: 取 18 家中位数后 implied prob 加权能近似 vig-free? 还是需要先做 Shin/de-vigging 算法? 留给小梁建模.
2. **`odd="SP"` (Starting Price) 处理**: 5% 左右的 entries 是 SP 而非数值. ETL 必须 skip-or-defer, 不能 parse 成 0.
3. **MLB oddsid 与 v2 主报告 §10.2 升级清单的关系**: 一旦升级, oddsid 解析路径 `getodds/baseball?oddsid=...` 是否 per-game 单查还是批量, 待 Goalserve sales 答复.
4. **NBA / NFL / Soccer (五大联赛) 复测**: 这次实证主力在赛季外, 当 EPL 8 月开赛 / NBA 10 月开赛后, base feed 可能挂载 oddsid (但不一定). 建议长期 sweep job 包含 "oddsid 出现率" 监控.
5. **`hockey/nhl-shedule` 3.6 MB 巨型**, 当前 NO_ODDS. 但 schedule 是 master data, 即便升级 odds plan 也不会挂 odds 到 schedule (而是挂到 scores/inplay). 这条不需要重测.
6. **Esports**: 当下 esports/home 只 CS GO, 升级 odds plan 是否覆盖 esports? 建议老雷在 sales 清单里单列 (相对其他 sport, esports odds 价格在 Goalserve 通常另算).

---

## 8. 测试方法学

- 探针 wrapper: `/tmp/xiaoduan_v21/probe.sh` + `retry.sh` (key 从 .env 读, 输出 url 中 key 已 `<KEY>` redact)
- 三态分析器: `/tmp/xiaoduan_v21/analyze.py` (XML attr + JSON key 双扫, decimal 1.01-50 范围过滤)
- 原始抓样: `/tmp/xiaoduan_v21/raw/*.body` (47 文件)
- 分析输出: `/tmp/xiaoduan_v21/analysis.json` + `/tmp/xiaoduan_v21/analysis.txt`
- 走代理: `GOALSERVE_PROXY` (.env), 偶发 SSL_ERROR_SYSCALL 串行重试解决
- false positive 二次过滤: 人工 grep context, 排除 `us="HH:MM"` / `odd_name="Player"` / `winner="True"` / `set1="6.3"` 等
- v2.1 是 v2 主报告补丁, 不替代 — endpoint matrix / 命名陷阱 / TTL 仍以 v2 为准
